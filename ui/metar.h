#pragma once
#include <QCache>
#include <QNetworkAccessManager>
#include <QUrl>

#include "../aviation/altimeter.h"

namespace stars::ui {
class MetarArchive : public QObject {
 public:
  explicit MetarArchive(
      const QStringList& stations,
      QUrl endpoint =
          QUrl("https://mesonet.agron.iastate.edu/cgi-bin/request/asos.py"));
  ~MetarArchive() override;
  aviation::Altimeters sample(qint64 second);

 private:
  struct Day {
    QMap<QString, QMap<qint64, int>> pressures;
    qint64 refresh{};
  };
  QMap<QString, QString> stations_;  // IEM identifier to adapted station
  QUrl endpoint_;
  QNetworkAccessManager network_;
  QCache<qint64, Day> days_{4};
  QNetworkReply* pending_{};
  qint64 requestedDay_ = -1, retryAt_ = 0, sampledSecond_ = -1;
  aviation::Altimeters sampled_;
  void fetch(qint64 day);
};
}  // namespace stars::ui
