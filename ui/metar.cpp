#include "metar.h"

#include <QDateTime>
#include <QNetworkReply>
#include <QTimeZone>
#include <QUrlQuery>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

namespace stars::ui {
namespace {
constexpr qint64 daySeconds = 24 * 60 * 60;
constexpr int maxBytes = 4 * 1024 * 1024;
QString timestamp(qint64 second) {
  return QDateTime::fromSecsSinceEpoch(second, QTimeZone::utc())
      .toString("yyyy-MM-dd'T'HH:mm:ss'Z'");
}
}  // namespace

MetarArchive::MetarArchive(const QStringList& stations, QUrl endpoint)
    : endpoint_(std::move(endpoint)) {
  for (const auto& station : stations) {
    if (station.isEmpty()) continue;
    const auto id = station.size() == 4 && station.startsWith('K')
                        ? station.mid(1)
                        : station;
    stations_.insert(id, station);
  }
}

MetarArchive::~MetarArchive() {
  if (pending_) pending_->disconnect(this);
}

void MetarArchive::fetch(qint64 day) {
  if (pending_) {
    auto* old = pending_;
    pending_ = nullptr;
    old->abort();
  }
  requestedDay_ = day;
  retryAt_ = 0;
  QUrl url = endpoint_;
  QUrlQuery query;
  for (auto it = stations_.cbegin(); it != stations_.cend(); ++it)
    query.addQueryItem("station", it.key());
  query.addQueryItem("sts", timestamp(day - daySeconds));
  query.addQueryItem("ets", timestamp(day + daySeconds));
  query.addQueryItem("data", "alti");
  query.addQueryItem("tz", "UTC");
  query.addQueryItem("format", "onlycomma");
  query.addQueryItem("report_type", "3");  // Routine METAR
  query.addQueryItem("report_type", "4");  // SPECI
  url.setQuery(query);
  QNetworkRequest request(url);
  request.setTransferTimeout(20000);
  auto* reply = network_.get(request);
  pending_ = reply;
  connect(reply, &QNetworkReply::readyRead, this, [reply] {
    if (reply->bytesAvailable() > maxBytes) reply->abort();
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply, day] {
    reply->deleteLater();
    if (reply != pending_) return;
    pending_ = nullptr;
    const auto now = QDateTime::currentSecsSinceEpoch();
    if (reply->error() != QNetworkReply::NoError ||
        reply->bytesAvailable() > maxBytes ||
        reply->readLine().trimmed() != "station,valid,alti") {
      qWarning() << "IEM altimeter archive request failed"
                 << reply->errorString();
      retryAt_ = now + 60;
      return;
    }
    auto result = std::make_unique<Day>();
    while (!reply->atEnd()) {
      const auto fields = reply->readLine().trimmed().split(',');
      if (fields.size() != 3) continue;
      const auto station = stations_.constFind(QString::fromLatin1(fields[0]));
      if (station == stations_.cend()) continue;
      const auto time = QDateTime::fromString(
          QString::fromLatin1(fields[1]) + 'Z', Qt::ISODate);
      bool ok = false;
      const double pressure = fields[2].toDouble(&ok);
      if (time.isValid() && ok && std::isfinite(pressure) && pressure >= 20 &&
          pressure <= 35)
        result->pressures[*station].insert(time.toSecsSinceEpoch(),
                                           qRound(pressure * 100));
    }
    // Recently ingested reports can still arrive after UTC midnight.
    result->refresh = day + daySeconds + 3600 > now
                          ? now + 600
                          : std::numeric_limits<qint64>::max();
    days_.insert(day, result.release());
    sampledSecond_ = -1;
  });
}

aviation::Altimeters MetarArchive::sample(qint64 second) {
  if (stations_.isEmpty()) return {};
  const auto day = second - second % daySeconds;
  const auto now = QDateTime::currentSecsSinceEpoch();
  const auto* data = days_.object(day);
  if ((!data || data->refresh <= now) &&
      (requestedDay_ != day || (!pending_ && retryAt_ <= now)))
    fetch(day);
  if (!data) return {};
  if (second == sampledSecond_) return sampled_;
  sampledSecond_ = second;
  sampled_.clear();
  for (const auto& station : std::as_const(stations_)) {
    auto& reading = sampled_[station];
    const auto reports = data->pressures.constFind(station);
    if (reports == data->pressures.cend()) continue;
    auto latest = reports->upperBound(second);
    if (latest == reports->cbegin()) continue;
    --latest;
    if (second - latest.key() <= daySeconds)
      reading = {latest.value(), latest.key()};
  }
  return sampled_;
}
}  // namespace stars::ui
