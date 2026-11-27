#include "adsbhttp.h"

#include <QNetworkReply>
#include <QTimer>
namespace stars::net::adsb {
namespace {
bool allowed(const QUrl& url) {
  static const QStringList hosts{
      "github.com", "release-assets.githubusercontent.com",
      "objects.githubusercontent.com", "github-releases.githubusercontent.com"};
  return url.isValid() && url.scheme() == "https" &&
         hosts.contains(url.host()) && url.userInfo().isEmpty() &&
         url.port(443) == 443;
}
bool redirect(int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 ||
         status == 308;
}
}  // namespace
Transfer::Transfer(QNetworkAccessManager& network, QUrl url,
                   std::optional<Span> span, Done done, HttpOptions opts)
    : QObject(&network),
      network_(network),
      span_(std::move(span)),
      done_(std::move(done)),
      options_(opts) {
  idle_.setSingleShot(true);
  idle_.setInterval(opts.inactivityMs);
  connect(&idle_, &QTimer::timeout, this,
          [this] { finish("transfer stalled"); });
  QTimer::singleShot(opts.deadlineMs, this,
                     [this] { finish("request deadline exceeded"); });
  QTimer::singleShot(0, this, [this, url] { request(url); });
}
Transfer::~Transfer() {
  if (reply_) {
    reply_->disconnect(this);
    reply_->abort();
    reply_->deleteLater();
  }
}
void Transfer::request(QUrl url) {
  if (!done_) return;
  idle_.start();
  if (!allowed(url)) return finish("invalid asset destination", true);
  QNetworkRequest req(url);
  req.setTransferTimeout(options_.inactivityMs);
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::ManualRedirectPolicy);
  req.setAttribute(QNetworkRequest::CookieLoadControlAttribute,
                   QNetworkRequest::Manual);
  req.setAttribute(QNetworkRequest::CookieSaveControlAttribute,
                   QNetworkRequest::Manual);
  req.setAttribute(QNetworkRequest::AuthenticationReuseAttribute,
                   QNetworkRequest::Manual);
  req.setRawHeader("User-Agent", "stars-adsb-client/1");
  req.setRawHeader("Accept-Encoding", "identity");
  if (span_) req.setRawHeader("Range", span_->range());
  reply_ = network_.get(req);
  reply_->setReadBufferSize(65536);
  connect(reply_, &QNetworkReply::metaDataChanged, this, &Transfer::inspect);
  connect(reply_, &QIODevice::readyRead, this, &Transfer::inspect);
  connect(reply_, &QNetworkReply::finished, this, [this] {
    inspect();
    if (!reply_ || !done_) return;
    if (reply_->error() != QNetworkReply::NoError)
      return finish("network request failed");
    if (!response_.status ||
        (span_ && response_.body.size() != span_->last - span_->first + 1))
      return finish("incomplete response", true);
    finish();
  });
}
void Transfer::inspect() {
  const int status =
      reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (!status) return;
  response_.status = status;
  if (redirect(status)) {
    const auto next = reply_->url().resolved(
        reply_->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
    if (++redirects_ > 4 || !allowed(next))
      return finish("invalid redirect", true);
    reply_->disconnect(this);
    reply_->abort();
    reply_->deleteLater();
    reply_ = nullptr;
    response_ = {};
    QTimer::singleShot(0, this, [this, next] { request(next); });
    return;
  }
  if (status != (span_ ? 206 : 200)) {
    auto header = reply_->rawHeader("Retry-After");
    bool numeric = false;
    auto seconds = header.toLongLong(&numeric);
    if (!numeric) {
      const auto after =
          QDateTime::fromString(QString::fromLatin1(header), Qt::RFC2822Date);
      seconds =
          after.isValid() ? QDateTime::currentDateTimeUtc().secsTo(after) : 0;
    }
    response_.retryAfter = int(std::clamp<qint64>(seconds, 0, 900));
    return finish("unexpected HTTP status", span_ && status == 200);
  }
  const qint64 cap = span_ ? span_->last - span_->first + 1 : kCompressedLimit;
  const auto length = reply_->rawHeader("Content-Length");
  bool ok;
  const auto bytes = length.toLongLong(&ok);
  const auto encoding = reply_->rawHeader("Content-Encoding").toLower();
  if ((!length.isEmpty() &&
       (!ok || bytes < 0 || bytes > cap || (span_ && bytes != cap))) ||
      (!encoding.isEmpty() && encoding != "identity") ||
      reply_->rawHeader("Content-Type").toLower().startsWith("multipart/") ||
      (span_ && reply_->rawHeader("Content-Range") != span_->contentRange()))
    return finish("invalid response headers", true);
  while (reply_->bytesAvailable() > 0) {
    const auto available = reply_->bytesAvailable();
    if (available > cap - response_.body.size())
      return finish("oversized response", true);
    auto chunk = reply_->read(std::min<qint64>(available, 65536));
    if (chunk.isEmpty()) break;
    idle_.start();
    response_.body += chunk;
  }
}
void Transfer::finish(QString error, bool invalid) {
  if (!done_) return;
  idle_.stop();
  response_.error = std::move(error);
  response_.invalid = invalid;
  if (reply_) {
    reply_->disconnect(this);
    if (!reply_->isFinished()) reply_->abort();
    reply_->deleteLater();
    reply_ = nullptr;
  }
  auto done = std::move(done_);
  deleteLater();
  done(std::move(response_));
}
}  // namespace stars::net::adsb
