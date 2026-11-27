#pragma once
#include <QCache>
#include <QFutureWatcher>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <functional>

#include "../stars/nexrad.h"

namespace stars::ui {

// Per-pane, nonblocking reader of NOAA's timestamped MRMS objects. The host
// polls with its playback clock, including while a selected time is paused.
class Weather : public QObject {
 public:
  explicit Weather(
      QString artcc,
      QUrl endpoint = QUrl("https://noaa-mrms-pds.s3.amazonaws.com/"));
  ~Weather() override;
  using Frame = std::shared_ptr<const NexradFrame>;
  struct Sample {
    WeatherPictures pictures{};
    bool ready =
        false;  // A completed archive lookup can also confirm there is no scan.
  };
  Sample sample(qint64 second, QRectF visible, bool playing);

 private:
  struct Hour {
    QMap<qint64, QString> scans;
    qint64 refresh;
  };
  QString domain_;
  QUrl endpoint_;
  QNetworkReply* pending_{};
  QUrl failedUrl_;
  qint64 retryAt_{};
  QFutureWatcher<Frame> decoder_;
  bool decoding_{};
  QCache<qint64, Hour> hours_{6};
  QCache<QString, QByteArray> downloads_{32 * 1024};  // KiB
  QCache<qint64, Frame> frames_{
      4};  // Three pictures plus the upcoming playback scan.
  void request(QUrl url, int limit, std::function<void(QByteArray)> receive);
  Hour* hour(qint64 start);
  bool picture(qint64 second, QRectF visible, Frame& result);
  Frame load(qint64 observed, const QString& key, QRectF visible);
  void failed(const QUrl& url, const QString& reason);
};
}  // namespace stars::ui
