#include "weather.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QNetworkDiskCache>
#include <QPointer>
#include <QStandardPaths>
#include <QTimeZone>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <QtConcurrentRun>
#include <cmath>
#include <limits>
#include <numbers>

namespace stars::ui {
namespace {
constexpr qint64 hourSeconds = 3600, maxAge = 10 * 60;
constexpr auto product = "MergedReflectivityQCComposite_00.50";

// A shared manager reuses TLS connections and coordinates the on-disk cache
// when multiple panes open the same archive. Qt owns it for the app lifetime.
QNetworkAccessManager& network() {
  static QPointer<QNetworkAccessManager> manager;
  if (!manager) {
    manager = new QNetworkAccessManager(QCoreApplication::instance());
    const auto root =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (!root.isEmpty()) {
      auto* cache = new QNetworkDiskCache(manager);
      cache->setCacheDirectory(root + "/mrms");
      cache->setMaximumCacheSize(128 * 1024 * 1024);
      manager->setCache(cache);
    }
  }
  return *manager;
}

QString domainFor(const QString& artcc) {
  if (artcc == "ZAN") return "ALASKA";
  if (artcc == "ZHN") return "HAWAII";
  if (artcc == "ZSU") return "CARIB";
  if (artcc == "ZUA") return "GUAM";
  return "CONUS";
}
}  // namespace

Weather::Weather(QString artcc, QUrl endpoint)
    : domain_(domainFor(artcc)), endpoint_(std::move(endpoint)) {}

Weather::~Weather() {
  if (pending_) {
    auto* reply = pending_;
    pending_ = nullptr;
    reply->abort();
    reply->deleteLater();
  }
}

void Weather::failed(const QUrl& url, const QString& reason) {
  failedUrl_ = url;
  retryAt_ = QDateTime::currentSecsSinceEpoch() + 30;
  qWarning().noquote() << "NEXRAD:" << reason << url.toDisplayString();
}

void Weather::request(QUrl url, int limit,
                      std::function<void(QByteArray)> receive) {
  if (failedUrl_ == url && QDateTime::currentSecsSinceEpoch() < retryAt_)
    return;
  if (pending_) {
    if (pending_->url() == url) return;
    auto* old = pending_;
    pending_ = nullptr;
    old->abort();
  }
  QNetworkRequest request(url);
  request.setTransferTimeout(20000);
  // Scan keys are immutable. Hourly listings may receive late arrivals.
  if (!url.hasQuery())
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::PreferCache);
  else
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
  auto* reply = network().get(request);
  pending_ = reply;
  connect(reply, &QNetworkReply::readyRead, this, [reply, limit] {
    if (reply->bytesAvailable() > limit) reply->abort();
  });
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, limit, receive = std::move(receive)] {
            reply->deleteLater();
            if (pending_ != reply) return;
            pending_ = nullptr;
            if (reply->error() != QNetworkReply::NoError ||
                reply->bytesAvailable() > limit) {
              failed(reply->url(), reply->errorString());
              return;
            }
            receive(reply->readAll());
          });
}

Weather::Hour* Weather::hour(qint64 start) {
  const auto now = QDateTime::currentSecsSinceEpoch();
  auto* cached = hours_.object(start);
  if (cached && cached->refresh > now) return cached;
  const auto date = QDateTime::fromSecsSinceEpoch(start, QTimeZone::utc());
  const QString prefix = domain_ + "/" + product + "/" +
                         date.toString("yyyyMMdd") + "/MRMS_" + product + "_" +
                         date.toString("yyyyMMdd-HH");
  QUrl url = endpoint_;
  QUrlQuery query;
  query.addQueryItem("list-type", "2");
  query.addQueryItem("prefix", prefix);
  url.setQuery(query);
  if (cached) cached->refresh = now + 30;
  request(url, 1024 * 1024, [this, start, prefix, url](QByteArray body) {
    QXmlStreamReader xml(body);
    auto result = std::make_unique<Hour>();
    while (!xml.atEnd()) {
      if (xml.readNext() != QXmlStreamReader::StartElement ||
          xml.name() != u"Key")
        continue;
      const QString key = xml.readElementText();
      if (!key.startsWith(prefix) || !key.endsWith(".grib2.gz") ||
          key.size() != prefix.size() + 13)
        continue;
      const auto stamp =
          QTime::fromString(key.mid(prefix.size() - 2, 6), "hhmmss");
      if (stamp.isValid())
        result->scans.insert(start + stamp.minute() * 60 + stamp.second(), key);
    }
    if (xml.hasError()) {
      failed(url, xml.errorString());
      return;
    }
    const auto now = QDateTime::currentSecsSinceEpoch();
    // Recently published hours can still receive late-arriving scans.
    result->refresh = start + hourSeconds + maxAge > now
                          ? now + 60
                          : std::numeric_limits<qint64>::max();
    hours_.insert(start, result.release());
  });
  return cached;
}

Weather::Frame Weather::load(qint64 observed, const QString& key,
                             QRectF visible) {
  auto cached = frames_.object(observed);
  if (cached && (*cached)->coverage.contains(visible)) return *cached;
  const auto url = endpoint_.resolved(QUrl(key));
  if (failedUrl_ == url && QDateTime::currentSecsSinceEpoch() < retryAt_)
    return {};
  auto bytes = downloads_.object(key);
  if (!bytes) {
    request(url, 32 * 1024 * 1024, [this, key](QByteArray data) {
      const int cost = std::max(1, int((data.size() + 1023) / 1024));
      downloads_.insert(key, new QByteArray(std::move(data)), cost);
    });
    return {};
  }
  if (!decoding_) {
    // A margin lets normal panning reuse the same geometry and AVL state.
    const double latMargin = 100. / 60;
    const double lonMargin =
        latMargin / std::max(.1, std::abs(std::cos(visible.center().y() *
                                                   std::numbers::pi / 180)));
    const auto area =
        visible.adjusted(-lonMargin, -latMargin, lonMargin, latMargin);
    decoding_ = true;
    connect(
        &decoder_, &QFutureWatcher<Frame>::finished, this,
        [this, observed, url, key] {
          decoding_ = false;
          try {
            auto frame = decoder_.result();
            if (frame->observed != observed)
              throw std::runtime_error("Archive and GRIB timestamps disagree");
            frames_.insert(observed, new Frame(std::move(frame)));
          } catch (const std::exception& error) {
            downloads_.remove(key);
            if (auto* cache = network().cache()) cache->remove(url);
            failed(url, QString::fromUtf8(error.what()));
          }
        },
        Qt::SingleShotConnection);
    decoder_.setFuture(QtConcurrent::run(
        [data = *bytes, area] { return decodeNexrad(data, area); }));
  }
  return {};
}

bool Weather::picture(qint64 second, QRectF visible, Frame& result) {
  const qint64 start = second - second % hourSeconds;
  auto* index = hour(start);
  if (!index) return false;
  auto scan = index->scans.upperBound(second);
  if (scan == index->scans.begin()) {
    if (second - start >= maxAge) return true;
    index = hour(start - hourSeconds);
    if (!index) return false;
    if (index->scans.empty()) return true;
    scan = index->scans.end();
  }
  --scan;
  if (second - scan.key() > maxAge) return true;
  result = load(scan.key(), scan.value(), visible);
  return bool(result);
}

Weather::Sample Weather::sample(qint64 second, QRectF visible, bool playing) {
  Sample sample;
  auto time = second;
  for (auto& frame : sample.pictures) {
    if (!picture(time, visible, frame)) return sample;
    if (!frame) break;
    time = frame->observed - 1;
  }
  sample.ready = true;
  if (playing && sample.pictures[0]) {
    Frame upcoming;
    picture(second + 15, visible, upcoming);
  }
  return sample;
}
}  // namespace stars::ui
