#include <QDebug>
#include <QtConcurrentRun>
#include <algorithm>
#include <limits>
#include <stdexcept>

#include "archive.h"
#include "track.h"

namespace stars::net {
ArchiveSource ArchiveSource::configured() {
  return {qEnvironmentVariable("STARS_TAIS_DIR"),
          qEnvironmentVariable("TAIS_BUCKET")};
}

TaisCache::TaisCache(QString facility, ArchiveSource source)
    : facility_(facility.trimmed().toUpper()), source_(std::move(source)) {
  connect(&worker_, &QFutureWatcher<Result>::finished, this, [this] {
    const auto start = *active_;
    active_.reset();
    if (!cancelled_->load() && windows_.contains(start)) {
      auto& entry = windows_[start];
      const auto result = worker_.result();
      if (result.data) {
        if (entry.data && entry.data != result.data) ++revision_;
        entry.data = result.data;
      }
      if (!result.error.isEmpty() && result.error != entry.error)
        qWarning().noquote() << (source_.itws ? "ITWS" : "TAIS") << facility_
                             << start << result.error;
      entry.error = result.error;
      // S3 parts are immutable, but another part can arrive after an empty
      // or partial listing. A refresh retains the last complete snapshot.
      entry.retry.setRemainingTime(result.data ? 60000 : 5000);
    }
    schedule();
  });
}
TaisCache::~TaisCache() {
  if (cancelled_) cancelled_->store(true);
}
bool TaisCache::configured() const {
  return !facility_.isEmpty() &&
         (!source_.directory.isEmpty() || !source_.bucket.isEmpty());
}
void TaisCache::prepare(qint64 second, int lookbehindSeconds) {
  if (!configured()) return;
  if (second < 0 || second > std::numeric_limits<qint64>::max() / 1000000000 -
                                 lookaheadSeconds)
    throw std::out_of_range("TAIS playback time outside Unix nanosecond range");
  const auto begin = std::max<qint64>(
      0, second - std::clamp(lookbehindSeconds, 0,
                             Traffic::ownershipLookbehindSeconds));
  const auto first = begin - begin % windowSeconds;
  const auto end = second + lookaheadSeconds - 1;
  last_ = end - end % windowSeconds;
  first_ = first;
  for (auto it = windows_.begin(); it != windows_.end();) {
    // Retain a loaded predecessor briefly for the ten-second rewind button.
    if (it.key() > last_ || it.key() + windowSeconds <= begin - 10)
      it = windows_.erase(it);
    else
      ++it;
  }
  if (active_ && (*active_ < first || *active_ > last_))
    cancelled_->store(true);
  for (auto start = first; start <= last_; start += windowSeconds)
    windows_[start];
  schedule();
}
void TaisCache::schedule() {
  if (active_ || !first_) return;
  std::optional<qint64> next;
  for (auto start = *first_; start <= last_; start += windowSeconds) {
    const auto& entry = windows_[start];
    if (!entry.retry.hasExpired()) continue;
    if (!next) next = start;
    if (!entry.data) {
      next = start;
      break;
    }
  }
  if (!next) return;
  active_ = next;
  cancelled_ = std::make_shared<std::atomic_bool>(false);
  worker_.setFuture(QtConcurrent::run(
      [source = source_, facility = facility_, start = *next,
       cached = windows_[*next].data, cancelled = cancelled_] {
        try {
          return Result{loadWindow(source, facility, start, cached, *cancelled),
                        {}};
        } catch (const std::exception& e) {
          return Result{{}, QString::fromUtf8(e.what())};
        }
      }));
}
bool TaisCache::availableAt(qint64 second) const {
  const auto it = windows_.constFind(second - second % windowSeconds);
  return it != windows_.cend() && it->data && !it->data->parts.isEmpty() &&
         it->error.isEmpty();
}
TaisCache::Snapshots TaisCache::snapshots() const {
  Snapshots result;
  for (auto it = windows_.cbegin(); it != windows_.cend(); ++it)
    result.insert(it.key(), it->error.isEmpty() ? it->data : nullptr);
  return result;
}
bool TaisCache::ready() const {
  if (!first_) return false;
  for (auto start = *first_; start <= last_; start += windowSeconds)
    if (!windows_.constFind(start)->data) return false;
  return true;
}
QString TaisCache::error() const {
  if (first_)
    for (auto start = *first_; start <= last_; start += windowSeconds) {
      const auto& reason = windows_.constFind(start)->error;
      if (!reason.isEmpty()) return reason;
    }
  return {};
}
std::optional<QList<Delivery>> TaisCache::between(qint64 from,
                                                  qint64 until) const {
  if (from < 0 || until < from) return {};
  QList<Delivery> result;
  if (until == from) return result;
  constexpr qint64 windowNs = windowSeconds * 1000000000;
  for (auto window = from / windowNs; window <= (until - 1) / windowNs;
       ++window) {
    const auto it = windows_.constFind(window * windowSeconds);
    if (it == windows_.cend() || !it->data) return {};
    const auto& events = it->data->events;
    auto hit = std::lower_bound(
        events.begin(), events.end(), from,
        [](const Delivery& d, qint64 ns) { return d.receivedNs < ns; });
    for (; hit != events.end() && hit->receivedNs < until; ++hit)
      result.append(*hit);
  }
  return result;
}
}  // namespace stars::net
