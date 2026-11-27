#include "archive.h"

#include <zstd.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimeZone>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace stars::net {
namespace {
// Spool rotates at 64 MiB after appending a complete delivery. A final
// JSON-escaped 10 MiB text body can grow sixfold before rotation.
// ITWS permits 32 MiB payloads; account for worst-case JSON escaping.
void check(bool ok, const QString& reason) {
  if (!ok) throw std::runtime_error(reason.toStdString());
}
void checkCancelled(const std::atomic_bool& cancelled) {
  check(!cancelled.load(), "Read cancelled");
}

// Use the same CLI credential chain as the recorder, including profiles and
// short-lived credentials. No shell, credentials or bucket policy in the app.
QByteArray aws(QStringList arguments, const std::atomic_bool& cancelled) {
  QProcess process;
  process.start("aws", QStringList{"s3api"} + arguments +
                           QStringList{"--output", "json", "--no-cli-pager"});
  check(process.waitForStarted(),
        "Cannot start AWS CLI: " + process.errorString());
  QDeadlineTimer timeout(90000);
  while (!process.waitForFinished(100) &&
         process.state() != QProcess::NotRunning) {
    if (cancelled.load() || timeout.hasExpired()) {
      process.kill();
      process.waitForFinished();
      checkCancelled(cancelled);
      throw std::runtime_error("S3 request timed out");
    }
  }
  check(
      process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
      "S3 request failed: " + QString::fromUtf8(process.readAllStandardError())
                                  .trimmed()
                                  .left(1024));
  return process.readAllStandardOutput();
}

Delivery parse(const QByteArray& line, const QString& facility, qint64 start,
               bool itws) {
  QJsonParseError err;
  const auto json = QJsonDocument::fromJson(line, &err).object();
  check(err.error == QJsonParseError::NoError &&
            json.value("version").toInteger() == 1,
        "Invalid TAIS envelope");
  Delivery d;
  d.facility = json.value("facility").toString();
  if (itws) {
    check(json["sites"].toArray().contains(facility),
          "ITWS site does not match its object key");
    d.facility = facility;
  }
  check(d.facility == facility, "TAIS facility does not match its object key");
  const auto stamp = json.value("receivedAt").toString();
  static const QRegularExpression instant(
      R"(^([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2})(?:\.([0-9]{1,9}))?Z$)");
  const auto match = instant.match(stamp);
  check(match.hasMatch(), "Invalid TAIS receipt time");
  const auto date = QDateTime::fromString(match.captured(1) + "Z", Qt::ISODate);
  const auto second = date.toSecsSinceEpoch();
  check(date.isValid() && second >= start && second - start < windowSeconds,
        "TAIS receipt time outside object window");
  const int ns = match.captured(2).leftJustified(9, '0').toInt();
  check(second >= 0 &&
            second <= (std::numeric_limits<qint64>::max() - ns) / 1000000000,
        "TAIS receipt time out of range");
  d.receivedNs = second * 1000000000 + ns;
  d.sequence = json.value("sequence").toInteger(-1);
  d.writer = json.value("writer").toString();
  d.queue = json.value("queue").toString();
  check(d.sequence >= 0 && !d.writer.isEmpty() && !d.queue.isEmpty(),
        "Invalid TAIS writer metadata");
  d.messageId = json.value("messageId").toString();
  d.jmsTimestamp = json.value("jmsTimestamp").toInteger();
  d.redelivered = json.value("redelivered").toBool();
  d.root = json.value("root").toString();
  d.encoding = json.value("encoding").toString();
  const auto body = json.value("body");
  if (d.encoding == "text") {
    check(body.isString() || body.isNull(), "Invalid TAIS text body");
    d.body = body.toString().toUtf8();
  } else {
    check(d.encoding == "base64" && body.isString(), "Invalid TAIS encoding");
    auto decoded = QByteArray::fromBase64Encoding(
        body.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    check(bool(decoded), "Invalid TAIS base64 body");
    d.body = std::move(decoded.decoded);
  }
  check(d.body.size() <= ((itws ? 32 : 10) << 20), "Oversized TAIS delivery");
  return d;
}

void decode(const QString& path, const QString& facility, qint64 start,
            const std::atomic_bool& cancelled, QList<Delivery>& events,
            bool itws) {
  const qint64 maxPart = (itws ? 258LL : 128LL) << 20;
  QFile file(path);
  check(file.open(QIODevice::ReadOnly),
        "Cannot open TAIS part: " + file.errorString());
  check(file.size() > 0 && file.size() <= maxPart + (1 << 20),
        "Invalid compressed TAIS part size");
  using Context = std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)>;
  Context ctx(ZSTD_createDCtx(), ZSTD_freeDCtx);
  check(bool(ctx), "Cannot allocate zstd decoder");
  check(
      !ZSTD_isError(ZSTD_DCtx_setParameter(ctx.get(), ZSTD_d_windowLogMax, 27)),
      "Cannot configure zstd decoder");
  QByteArray block(65536, Qt::Uninitialized), line;
  qint64 total = 0;
  size_t remaining = 1;
  while (!file.atEnd()) {
    const auto input = file.read(65536);
    check(!input.isEmpty(), "Cannot read TAIS part");
    ZSTD_inBuffer in{input.constData(), size_t(input.size()), 0};
    bool drain = false;
    do {
      checkCancelled(cancelled);
      ZSTD_outBuffer out{block.data(), size_t(block.size()), 0};
      remaining = ZSTD_decompressStream(ctx.get(), &out, &in);
      check(!ZSTD_isError(remaining), "Invalid TAIS zstd frame");
      drain = remaining != 0 && out.pos == out.size;
      total += qint64(out.pos);
      check(total <= maxPart, "Oversized decoded TAIS part");
      qsizetype pos = 0;
      const QByteArrayView bytes(block.constData(), qsizetype(out.pos));
      while (pos < bytes.size()) {
        const auto lf = bytes.indexOf('\n', pos);
        const auto end = lf < 0 ? bytes.size() : lf;
        line.append(bytes.data() + pos, end - pos);
        check(line.size() <= ((itws ? 194LL : 64LL) << 20),
              "Oversized TAIS envelope");
        if (lf >= 0) {
          events.append(parse(line, facility, start, itws));
          line.clear();
        }
        pos = end + 1;
      }
    } while (in.pos < in.size || drain);
  }
  check(remaining == 0 && total > 0 && line.isEmpty(), "Incomplete TAIS part");
}
}  // namespace

std::shared_ptr<const TaisWindow> loadWindow(
    const ArchiveSource& source, const QString& facility, qint64 start,
    std::shared_ptr<const TaisWindow> cached,
    const std::atomic_bool& cancelled) {
  static const QRegularExpression facilityId("^[A-Z0-9]{1,12}$");
  check(facilityId.match(facility).hasMatch(), "Invalid TAIS facility");
  const auto utc = QDateTime::fromSecsSinceEpoch(start, QTimeZone::utc());
  const auto prefix = (source.itws ? "itws/v1/" : "tais/v1/") + facility + "/" +
                      utc.toString("yyyy/MM/dd/HH/HHmm-");
  QStringList keys;
  const bool local = !source.directory.isEmpty();
  const QDir root(source.directory);
  if (local) {
    check(root.exists() && root.isReadable(),
          "TAIS archive directory unavailable");
    const auto parent = prefix.left(prefix.lastIndexOf('/') + 1);
    const QDir dir(root.filePath(parent));
    check(!dir.exists() || dir.isReadable(),
          "TAIS window directory unreadable");
    for (const auto& name : dir.entryList(
             {prefix.mid(parent.size()) + "*.jsonl.zst"}, QDir::Files))
      keys.append(parent + name);
  } else {
    const auto bytes =
        aws({"list-objects-v2", "--bucket", source.bucket, "--prefix", prefix},
            cancelled);
    const auto doc = QJsonDocument::fromJson(bytes);
    check(doc.isObject(), "Invalid S3 listing");
    const auto objects = doc.object().value("Contents");
    check((objects.isUndefined() || objects.isArray()) &&
              !doc.object().value("IsTruncated").toBool(),
          "Incomplete S3 listing");
    for (const auto& item : objects.toArray())
      keys.append(item.toObject().value("Key").toString());
  }
  static const QRegularExpression part(
      "^[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}\\.jsonl\\.zst$");
  keys.removeIf([&](const QString& key) {
    return !key.startsWith(prefix) ||
           !part.match(key.mid(prefix.size())).hasMatch() ||
           (cached && cached->parts.contains(key));
  });
  keys.removeDuplicates();
  if (cached && keys.isEmpty()) return cached;
  keys.sort();
  auto window = std::make_shared<TaisWindow>();
  if (cached) *window = *cached;
  QTemporaryDir tmp;
  for (const auto& key : keys) {
    checkCancelled(cancelled);
    QString path = root.filePath(key);
    if (!local) {
      check(tmp.isValid(), "Cannot create temporary archive directory");
      path = tmp.filePath("part.zst");
      aws({"get-object", "--bucket", source.bucket, "--key", key, path},
          cancelled);
    }
    decode(path, facility, start, cancelled, window->events, source.itws);
    window->parts.insert(key);
  }
  std::stable_sort(window->events.begin(), window->events.end(),
                   [](const Delivery& a, const Delivery& b) {
                     return std::tie(a.receivedNs, a.writer, a.sequence) <
                            std::tie(b.receivedNs, b.writer, b.sequence);
                   });
  return window;
}
}  // namespace stars::net
