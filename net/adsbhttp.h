#pragma once
#include <QNetworkAccessManager>
#include <QTimer>
#include <functional>

#include "adsbindex.h"
namespace stars::net::adsb {
struct Response {
  QByteArray body;
  int status{};
  QString error;
  int retryAfter{};
  bool invalid{};
};
struct HttpOptions {
  int inactivityMs = 20000, deadlineMs = 60000;
};
class Transfer final : public QObject {
 public:
  using Done = std::function<void(Response)>;
  Transfer(QNetworkAccessManager& network, QUrl url, std::optional<Span> span,
           Done done, HttpOptions options = {});
  ~Transfer() override;

 private:
  void request(QUrl url);
  void inspect();
  void finish(QString error = {}, bool invalid = false);
  QNetworkAccessManager& network_;
  QNetworkReply* reply_{};
  std::optional<Span> span_;
  Done done_;
  HttpOptions options_;
  Response response_;
  QTimer idle_;
  int redirects_{};
};
}  // namespace stars::net::adsb
