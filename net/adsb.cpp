#include "adsb.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QPointer>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrentRun>
#include <algorithm>
#include <map>

namespace stars::net::adsb {
namespace {
QDate dayAt(qint64 ms) {
  return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::utc()).date();
}
constexpr qint64 kDay = 86400000;
struct Interest {
  QString address;
  qint64 begin{}, end{};
  Priority priority{};
  bool operator==(const Interest&) const = default;
};
struct Job {
  QString key, address;
  QDate day;
  qint64 begin{}, end{}, size{};
  std::vector<Span> spans;
  size_t next{};
  QByteArray body;
  QPointer<Transfer> transfer;
  QElapsedTimer clock;
  QDeadlineTimer deadline;
};
struct Decoded {
  std::shared_ptr<const Index> index;
  std::shared_ptr<const Slice> slice;
  QString error;
  qint64 bytes{}, ms{};
  bool hit = false, writeFailed = false;
};
struct Failure {
  Availability state;
  qint64 until;
  int attempts;
};
struct Stored {
  std::shared_ptr<const Slice> slice;
  quint64 access;
};
struct Request {
  Interest interest;
  QList<QDate> days;
  quint64 served{};
  qint64 queuedMs{};
  QObject* pane{};
  QMetaObject::Connection close;
};
}  // namespace
struct Service::Impl {
  Service& owner;
  Options opts;
  QNetworkAccessManager* network;
  QThreadPool pool;
  QMap<Consumer, Request> consumers;
  QHash<QObject*, quint64> paneServed;
  QMap<QDate, std::shared_ptr<const Index>> indexes;
  QSet<QDate> refresh;
  QMap<QString, std::shared_ptr<Job>> jobs;
  QMap<QString, Failure> failures;
  QMap<QUrl, qint64> cooldown;
  std::multimap<QString, Stored> memory;
  qint64 retainedBytes{};
  Statistics stats;
  quint64 nextId{}, revision{}, clock{};
  bool scheduled = false;
  QTimer retry;
  Impl(Service& service, Options options, QNetworkAccessManager* transport)
      : owner(service),
        opts(std::move(options)),
        network(transport ? transport : new QNetworkAccessManager(&service)) {
    pool.setMaxThreadCount(2);
    if (opts.disk.root.isEmpty()) {
      const auto root =
          QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
      if (QDir::isAbsolutePath(root))
        opts.disk.root = QDir(root).filePath("adsb/v1");
    }
    retry.setSingleShot(true);
    QObject::connect(&retry, &QTimer::timeout, &owner, [this] { schedule(); });
  }
  ~Impl() {
    for (const auto& j : jobs)
      if (j->transfer) delete j->transfer.data();
    pool.clear();
    pool.waitForDone();
  }
  void schedule() {
    if (scheduled) return;
    scheduled = true;
    QTimer::singleShot(0, &owner, [this] {
      scheduled = false;
      pump();
    });
  }
  static QString indexId(QDate day) { return indexKey(day); }
  static QString traceId(const Index& idx, const Interest& i) {
    const auto* e = idx.find(i.address);
    return e ? idx.traceKey(*e) : QString{};
  }
  static std::pair<qint64, qint64> bounds(const Interest& i, QDate day) {
    auto start = day.startOfDay(QTimeZone::utc()).toMSecsSinceEpoch();
    return {std::max(i.begin, start), std::min(i.end, start + kDay)};
  }
  std::shared_ptr<const Slice> lookup(const QString& key, qint64 begin,
                                      qint64 end) {
    auto [first, last] = memory.equal_range(key);
    for (auto it = first; it != last; ++it) {
      auto& item = it->second;
      if (item.slice->beginMs <= begin && item.slice->endMs >= end) {
        item.access = ++clock;
        return item.slice;
      }
    }
    return {};
  }
  Availability failure(const QString& key) const {
    const auto it = failures.constFind(key);
    return it != failures.cend() &&
                   it->until > QDateTime::currentMSecsSinceEpoch()
               ? it->state
               : Availability::Pending;
  }
  Availability status(const Request& r, QDate day) {
    if (!r.days.contains(day)) return Availability::Cancelled;
    auto idx = indexes.value(day);
    if (!idx) return failure(indexId(day));
    if (r.interest.address.isEmpty()) return Availability::Ready;
    auto e = idx->find(r.interest.address);
    if (!e) return Availability::Absent;
    if (e->size > kCompressedLimit) return Availability::Rejected;
    auto [begin, end] = bounds(r.interest, day);
    auto key = idx->traceKey(*e);
    return lookup(key, begin, end) ? Availability::Ready : failure(key);
  }
  void fail(const std::shared_ptr<Job>& j, Availability state,
            int seconds = 0) {
    auto& f = failures[j->key];
    f.state = state;
    constexpr int delays[]{5, 15, 60, 300};
    if (!seconds) seconds = delays[std::min(f.attempts, 3)];
    f.attempts = std::min(f.attempts + 1, 4);
    const auto interval = seconds * 1000 + int(qHash(j->key) % 800);
    f.until = QDateTime::currentMSecsSinceEpoch() + interval;
    if (!retry.isActive() || interval < retry.remainingTime())
      retry.start(interval);
    ++stats.failures;
    finish(j);
  }
  void finish(const std::shared_ptr<Job>& j) {
    jobs.remove(j->key);
    ++revision;
    schedule();
  }
  bool retain(std::shared_ptr<const Slice> slice) {
    const auto cost = [](const Slice& s) {
      return qint64(sizeof(Slice) + s.key.size() * 2 +
                    s.points.capacity() * sizeof(Observation) + 128);
    };
    const auto needed = cost(*slice);
    if (needed > opts.memoryBytes) return false;
    if (retainedBytes + needed > opts.memoryBytes || memory.size() >= 1024) {
      // Protect both current subscribers and immutable snapshots retained by a
      // caller across seeks. Accounting follows allocation capacity.
      std::multimap<QString, std::pair<qint64, qint64>> interests;
      for (const auto& r : consumers)
        for (auto day : r.days)
          if (auto idx = indexes.value(day);
              idx && !r.interest.address.isEmpty())
            interests.emplace(traceId(*idx, r.interest),
                              bounds(r.interest, day));
      std::vector<decltype(memory)::iterator> victims;
      for (auto it = memory.begin(); it != memory.end(); ++it) {
        const auto& held = it->second.slice;
        if (held.use_count() != 1) continue;
        const auto [first, last] = interests.equal_range(it->first);
        if (std::none_of(first, last, [&](const auto& request) {
              return held->beginMs <= request.second.first &&
                     held->endMs >= request.second.second;
            }))
          victims.push_back(it);
      }
      std::ranges::sort(victims, {}, [](auto it) { return it->second.access; });
      for (auto it : victims) {
        if (retainedBytes + needed <= opts.memoryBytes && memory.size() < 1024)
          break;
        retainedBytes -= cost(*it->second.slice);
        memory.erase(it);
      }
      if (retainedBytes + needed > opts.memoryBytes || memory.size() >= 1024)
        return false;
    }
    retainedBytes += needed;
    const auto key = slice->key;
    memory.emplace(key, Stored{std::move(slice), ++clock});
    return true;
  }
  void decode(const std::shared_ptr<Job>& j, bool diskRead) {
    auto watcher = new QFutureWatcher<Decoded>(&owner);
    QObject::connect(
        watcher, &QFutureWatcher<Decoded>::finished, &owner,
        [this, watcher, j, diskRead] {
          auto result = watcher->result();
          watcher->deleteLater();
          if (jobs.value(j->key) != j) return;
          stats.decodeMs += result.ms;
          stats.corruptions += !result.error.isEmpty();
          stats.writeFailures += result.writeFailed;
          if (diskRead && result.hit && j->address.isEmpty()) ++stats.indexHits;
          if (diskRead) {
            if (result.hit)
              ++stats.diskHits;
            else
              ++stats.diskMisses;
          }
          if (result.index) {
            indexes[j->day] = result.index;
            stats.indexBytes = result.bytes;
          } else if (result.slice) {
            if (!retain(std::move(result.slice)))
              return fail(j, Availability::TemporaryFailure, 5);
          } else if (diskRead && !opts.offline) {
            for (const auto& r : consumers)
              if (r.days.contains(j->day) &&
                  (j->address.isEmpty() ||
                   (r.interest.address == j->address &&
                    indexes.contains(j->day) &&
                    traceId(*indexes.value(j->day), r.interest) == j->key)))
                return fetch(j);
            return finish(j);
          } else
            return fail(j,
                        diskRead ? (j->address.isEmpty()
                                        ? Availability::IndexUnavailable
                                        : Availability::TraceUnavailable)
                                 : Availability::Rejected,
                        900);
          failures.remove(j->key);
          finish(j);
        });
    const auto disk = opts.disk;
    // Workers own all inputs. Neither panes nor mutable scheduler state cross
    // the thread boundary, including when a consumer replaces its interest.
    watcher->setFuture(QtConcurrent::run(
        &pool, [disk, day = j->day, address = j->address, key = j->key,
                size = j->size, begin = j->begin, end = j->end,
                bytes = std::move(j->body), diskRead]() mutable {
          QElapsedTimer timer;
          timer.start();
          Decoded result;
          if (diskRead) bytes = disk.read(key, size ? size : kCompressedLimit);
          if (!bytes.isEmpty()) {
            try {
              if (size && bytes.size() != size)
                throw std::runtime_error("wrong cache size");
              if (address.isEmpty())
                result.index = std::make_shared<Index>(decodeIndex(bytes, day));
              else
                result.slice = std::make_shared<Slice>(
                    decodeTrace(bytes, address, day, key, begin, end));
              result.hit = diskRead;
              result.bytes = bytes.size();
              if (!diskRead) result.writeFailed = !disk.store(key, day, bytes);
            } catch (const std::exception& e) {
              result.error = QString::fromLatin1(e.what());
              if (diskRead) disk.discard(key, bytes);
            }
          }
          result.ms = timer.elapsed();
          return result;
        }));
    stats.peakDecodes = std::max(stats.peakDecodes, pool.activeThreadCount());
  }
  void fetch(const std::shared_ptr<Job>& j) {
    std::optional<Span> span;
    QUrl url = indexUrl(j->day);
    if (!j->spans.empty()) {
      span = j->spans.at(j->next);
      url = span->part.url;
      if (cooldown.value(url) > QDateTime::currentMSecsSinceEpoch())
        return fail(j, Availability::TemporaryFailure, 300);
      ++stats.ranges;
    }
    if (j->next == 0) j->deadline = QDeadlineTimer(opts.http.deadlineMs);
    if (j->deadline.hasExpired())
      return fail(j, Availability::TemporaryFailure);
    auto timeout = opts.http;
    timeout.deadlineMs = int(j->deadline.remainingTime());
    ++stats.requests;
    j->clock.start();
    j->transfer = new Transfer(
        *network, url, span,
        [this, j, url](Response r) {
          j->transfer = nullptr;
          if (jobs.value(j->key) != j) return;
          stats.downloadMs += j->clock.elapsed();
          stats.downloadBytes += r.body.size();
          if (!r.error.isEmpty()) {
            if (r.status == 404)
              return fail(j,
                          j->address.isEmpty() ? Availability::IndexUnavailable
                                               : Availability::TraceUnavailable,
                          900);
            if (!j->address.isEmpty() && r.status == 200)
              cooldown[url] = QDateTime::currentMSecsSinceEpoch() + 300000;
            if (r.invalid || r.status == 416)
              return fail(j, Availability::Rejected, 300);
            return fail(j, Availability::TemporaryFailure, r.retryAfter);
          }
          if (j->body.isEmpty())
            j->body = std::move(r.body);
          else
            j->body += r.body;
          if (++j->next < j->spans.size()) return fetch(j);
          decode(j, false);
        },
        timeout);
  }
  bool setInterest(Consumer id, std::optional<Interest> next) {
    auto it = consumers.find(id);
    if (it == consumers.end()) return false;
    if (next && it->interest == *next && !it->days.isEmpty()) return true;
    QList<QDate> dates;
    if (next) {
      dates.append(dayAt(next->begin));
      auto end = dayAt(next->end - 1);
      if (end != dates.first()) dates.append(end);
      if (dates != it->days) {
        QSet<QDate> active(dates.begin(), dates.end());
        for (auto i = consumers.cbegin(); i != consumers.cend(); ++i)
          if (i.key() != id)
            for (auto day : i->days) active.insert(day);
        if (active.size() > 8) next.reset();
      }
    }
    if (!next && it->days.isEmpty()) return false;
    it->interest = next.value_or(Interest{});
    it->days = next ? dates : QList<QDate>{};
    it->queuedMs = QDateTime::currentMSecsSinceEpoch();
    ++revision;
    schedule();
    return bool(next);
  }
  void pump() {
    QSet<QString> wanted;
    QSet<QDate> days;
    for (const auto& r : consumers)
      for (auto day : r.days) {
        days.insert(day);
        wanted.insert(indexId(day));
        if (auto idx = indexes.value(day); idx && !r.interest.address.isEmpty())
          wanted.insert(traceId(*idx, r.interest));
      }
    refresh.intersect(days);
    // Cancel network work only after the final subscriber leaves. Bounded
    // disk/CPU work can finish, but cannot publish to obsolete subscriptions.
    for (const auto& j : jobs.values())
      if (!wanted.contains(j->key) && j->transfer) {
        delete j->transfer.data();
        jobs.remove(j->key);
      }
    if (indexes.size() >= 8)
      indexes.removeIf([&](auto i) { return !days.contains(i.key()); });
    failures.removeIf([&](auto i) {
      return !wanted.contains(i.key()) &&
             i.value().until < QDateTime::currentMSecsSinceEpoch();
    });
    cooldown.removeIf(
        [](auto i) { return i.value() < QDateTime::currentMSecsSinceEpoch(); });
    // Keep transient/negative history bounded even during rapid date changes.
    while (failures.size() > opts.maxConsumers * 4 + 16)
      failures.erase(failures.begin());
    QList<Consumer> order = consumers.keys();
    const auto now = QDateTime::currentMSecsSinceEpoch();
    const auto rank = [now](const Request& r) {
      return std::max<qint64>(
          0, int(r.interest.priority) - (now - r.queuedMs) / 5000);
    };
    std::ranges::sort(order, [&](Consumer a, Consumer b) {
      const auto& l = consumers.constFind(a).value();
      const auto& r = consumers.constFind(b).value();
      const auto paneTurn = [&](const Request& request) {
        return request.pane ? paneServed.value(request.pane) : request.served;
      };
      return std::tuple(rank(l), paneTurn(l), l.served, a) <
             std::tuple(rank(r), paneTurn(r), r.served, b);
    });
    int aircraft = 0, indexJobs = 0;
    for (const auto& j : jobs) j->address.isEmpty() ? ++indexJobs : ++aircraft;
    for (auto id : order) {
      auto& r = consumers[id];
      for (auto day : r.days) {
        if (status(r, day) != Availability::Pending) continue;
        auto idx = indexes.value(day);
        auto j = std::make_shared<Job>();
        j->day = day;
        if (!idx) {
          if (indexJobs >= 1) continue;
          j->key = indexId(day);
        } else {
          if (aircraft >= 4) continue;
          const auto& e = *idx->find(r.interest.address);
          j->address = r.interest.address;
          j->key = idx->traceKey(e);
          j->size = e.size;
          j->spans = spans(*idx, e.offset, e.size);
          std::tie(j->begin, j->end) = bounds(r.interest, day);
        }
        if (jobs.contains(j->key)) continue;

        jobs.insert(j->key, j);
        j->address.isEmpty() ? ++indexJobs : ++aircraft;
        stats.peakJobs = std::max(stats.peakJobs, aircraft);
        r.served = ++clock;
        if (r.pane) paneServed[r.pane] = clock;
        r.queuedMs = now;
        if (j->address.isEmpty() && refresh.remove(day))
          fetch(j);
        else
          decode(j, true);
        // Re-evaluate pane turns before assigning the next available slot.
        schedule();
        return;
      }
    }
    for (auto it = failures.cbegin(); it != failures.cend(); ++it) {
      auto delay = it->until - now;
      if (delay > 0 && wanted.contains(it.key()) &&
          (!retry.isActive() || delay < retry.remainingTime()))
        retry.start(int(delay));
    }
  }
};
Service::Service(Options opts, QNetworkAccessManager* transport)
    : impl_(std::make_unique<Impl>(*this, std::move(opts), transport)) {}
Service::~Service() = default;
Service& Service::shared() {
  static QPointer<Service> instance;
  if (!instance) {
    instance = new Service;
    instance->setParent(QCoreApplication::instance());
  }
  return *instance;
}
Consumer Service::subscribe(QObject* pane) {
  if (impl_->consumers.size() >= impl_->opts.maxConsumers) return 0;
  auto id = ++impl_->nextId;
  auto& request = impl_->consumers[id];
  request.pane = pane;
  if (pane)
    request.close =
        connect(pane, &QObject::destroyed, this, [this, id] { release(id); });
  return id;
}
void Service::release(Consumer id) {
  auto it = impl_->consumers.find(id);
  if (it != impl_->consumers.end()) {
    auto pane = it->pane;
    disconnect(it->close);
    impl_->consumers.erase(it);
    if (pane && std::ranges::none_of(
                    impl_->consumers,
                    [pane](const Request& r) { return r.pane == pane; }))
      impl_->paneServed.remove(pane);
    ++impl_->revision;
    impl_->schedule();
  }
}
bool Service::request(Consumer id, QStringView address, qint64 begin,
                      qint64 end, Priority priority) {
  auto key = normalizeIcao(address);
  if (!key || begin < 0 || end <= begin || end > 253402300800000LL ||
      end - begin > kDay)
    return impl_->setInterest(id, {});
  // A moving playhead may submit interest every frame. Equivalent minute
  // blocks share a subscription revision and do not wake the scheduler.
  begin -= begin % 60000;
  end = ((end + 59999) / 60000) * 60000;
  return impl_->setInterest(id, Interest{*key, begin, end, priority});
}
bool Service::requestIndex(Consumer id, QDate day) {
  if (!day.isValid() || day.year() < 1970 || day.year() > 9999)
    return impl_->setInterest(id, {});
  const auto ms = day.startOfDay(QTimeZone::utc()).toMSecsSinceEpoch();
  return impl_->setInterest(id, Interest{{}, ms, ms + 1, Priority::Current});
}
void Service::refreshIndex(QDate day) {
  if (impl_->opts.offline || !day.isValid() || impl_->refresh.contains(day) ||
      impl_->jobs.contains(indexKey(day)))
    return;
  for (const auto& r : impl_->consumers)
    if (r.days.contains(day)) {
      impl_->indexes.remove(day);
      impl_->failures.remove(indexKey(day));
      impl_->refresh.insert(day);

      ++impl_->revision;
      impl_->schedule();
      break;
    }
}
Availability Service::status(Consumer id, QDate day) const {
  const auto it = impl_->consumers.constFind(id);
  return it == impl_->consumers.cend() ? Availability::Cancelled
                                       : impl_->status(*it, day);
}
std::shared_ptr<const Index> Service::index(QDate date) const {
  return impl_->indexes.value(date);
}
std::shared_ptr<const Slice> Service::available(Consumer id, QDate day) const {
  auto it = impl_->consumers.constFind(id);
  auto idx = index(day);
  if (!idx || it == impl_->consumers.cend() || !it->days.contains(day))
    return {};
  auto [begin, end] = Impl::bounds(it->interest, day);
  return impl_->lookup(Impl::traceId(*idx, it->interest), begin, end);
}
quint64 Service::revision() const { return impl_->revision; }
Statistics Service::statistics() const {
  auto s = impl_->stats;
  s.jobs = impl_->jobs.size();
  return s;
}
}  // namespace stars::net::adsb
