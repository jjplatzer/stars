#include "adsbindex.h"

#include <zstd.h>

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>

namespace stars::net::adsb {
namespace {
void require(bool valid, const char* reason) {
  if (!valid) throw std::runtime_error(reason);
}
bool hex(QStringView s) {
  return s.size() == 6 && std::ranges::all_of(s, [](QChar c) {
           return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f');
         });
}
QByteArray unpack(const QByteArray& data) {
  require(!data.isEmpty() && data.size() <= kCompressedLimit,
          "compressed index size");
  std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> ctx(ZSTD_createDCtx(),
                                                           ZSTD_freeDCtx);
  require(ctx && !ZSTD_isError(ZSTD_DCtx_setParameter(ctx.get(),
                                                      ZSTD_d_windowLogMax, 27)),
          "zstd initialization");
  ZSTD_inBuffer in{data.constData(), size_t(data.size()), 0};
  std::array<char, 65536> chunk;
  QByteArray out;
  size_t more;
  do {
    ZSTD_outBuffer buf{chunk.data(), chunk.size(), 0};
    const auto before = in.pos;
    more = ZSTD_decompressStream(ctx.get(), &buf, &in);
    require(!ZSTD_isError(more) && (more == 0 || buf.pos || in.pos != before),
            "invalid zstd index");
    require(out.size() + qint64(buf.pos) <= 128 * kMiB, "index JSON size");
    out.append(chunk.data(), buf.pos);
  } while (more);
  require(in.pos == in.size, "trailing index data");
  return out;
}
}  // namespace
std::optional<QString> normalizeIcao(QStringView input) {
  auto s = input.trimmed().toString().toLower();
  if (!hex(s) || s == "000000" || s == "ffffff") return {};
  return s;
}
QUrl indexUrl(QDate date) {
  if (!date.isValid()) return {};
  return QUrl(
      "https://github.com/jjplatzer/stars/releases/download/adsb-index-" +
      date.toString(Qt::ISODate) + "/index.json.zst");
}
Index decodeIndex(const QByteArray& data, QDate date) {
  const auto doc = QJsonDocument::fromJson(unpack(data));
  const auto obj = doc.object(), src = obj["source"].toObject();
  Index idx;
  idx.date = date;
  idx.repository = src["repository"].toString();
  idx.tag = src["tag"].toString();
  idx.releaseId = src["release_id"].toInteger(-1);
  idx.attribution = obj["attribution"].toString();
  idx.license = obj["license"].toString();
  static const QRegularExpression tag("^[A-Za-z0-9._-]{1,128}$");
  require(obj["version"].toInteger(-1) == 1 && obj["trace_encoding"] == "gzip",
          "unsupported ADS-B index");
  require(date.isValid() &&
              src["date"].toString() == date.toString(Qt::ISODate) &&
              idx.repository ==
                  "adsblol/globe_history_" + QString::number(date.year()) &&
              idx.tag.startsWith(date.toString("'v'yyyy.MM.dd-")) &&
              tag.match(idx.tag).hasMatch() && idx.tag.size() > 12 &&
              idx.releaseId > 0,
          "invalid archive source");
  require(!idx.attribution.trimmed().isEmpty() &&
              !idx.license.trimmed().isEmpty() &&
              idx.attribution.size() <= 4096 && idx.license.size() <= 4096,
          "invalid attribution");
  const auto parts = src["parts"].toArray();
  require(!parts.isEmpty() && parts.size() <= 676, "invalid archive parts");
  QSet<qint64> ids;
  // A fixed-order array gives a stable fingerprint independent of JSON key
  // order.
  QJsonArray identity{date.toString(Qt::ISODate), idx.repository, idx.tag,
                      idx.releaseId};
  for (int i = 0; i < parts.size(); ++i) {
    const auto p = parts[i].toObject();
    const auto id = p["id"].toInteger(-1), size = p["size"].toInteger(-1);
    QString name = idx.tag + ".tar";
    if (parts.size() > 1 || p["name"].toString() != name)
      name += QString(".") + QChar('a' + i / 26) + QChar('a' + i % 26);
    const QString url = "https://github.com/" + idx.repository +
                        "/releases/download/" + idx.tag + "/" + name;
    require(id > 0 && !ids.contains(id) && p["name"] == name &&
                p["browser_download_url"] == url &&
                p["start"].toInteger(-1) == idx.bytes && size > 0 &&
                size <= 32768 * kMiB - idx.bytes,
            "invalid part metadata");
    const QString digest = p["digest"].toString();
    static const QRegularExpression hash("^sha256:[0-9a-f]{64}$");
    require((!p.contains("digest") ||
             (p["digest"].isString() && hash.match(digest).hasMatch())) &&
                (!p.contains("state") || p["state"] == "uploaded"),
            "invalid asset digest/state");
    identity.append(QJsonArray{id, name, idx.bytes, size, digest});
    ids.insert(id);
    idx.parts.push_back({QUrl(url), idx.bytes, size});
    idx.bytes += size;
  }
  idx.fingerprint = QString::fromLatin1(
      QCryptographicHash::hash(
          QJsonDocument(identity).toJson(QJsonDocument::Compact),
          QCryptographicHash::Sha256)
          .toHex());
  auto entries = obj["entries"].toArray();
  require(obj["entries"].isArray() && entries.size() <= 1000000,
          "invalid entry array");
  idx.entries.reserve(entries.size());
  for (const auto& item : entries) {
    const auto e = item.toObject();
    const auto address = e["icao"].toString();
    require(hex(address), "invalid index ICAO");
    const auto key = address.toUInt(nullptr, 16);
    const auto offset = e["offset"].toInteger(-1),
               size = e["size"].toInteger(-1);
    require(offset >= 0 && size > 0 && offset <= idx.bytes - size &&
                (idx.entries.empty() || idx.entries.back().icao < key),
            "invalid index entry");
    idx.entries.push_back({key, offset, size});
  }
  return idx;
}
const Entry* Index::find(QStringView address) const {
  const auto s = normalizeIcao(address);
  if (!s) return nullptr;
  const auto key = s->toUInt(nullptr, 16);
  const auto it = std::ranges::lower_bound(entries, key, {}, &Entry::icao);
  return it != entries.end() && it->icao == key ? &*it : nullptr;
}
QString Index::traceKey(const Entry& e) const {
  return QString("traces/%1/%2/%3-%4-%5.json.gz")
      .arg(date.toString(Qt::ISODate), fingerprint)
      .arg(e.icao, 6, 16, QChar('0'))
      .arg(e.offset)
      .arg(e.size);
}
std::vector<Span> spans(const Index& idx, qint64 offset, qint64 size) {
  require(offset >= 0 && size > 0 && size <= idx.bytes &&
              offset <= idx.bytes - size,
          "range outside archive");
  std::vector<Span> result;
  for (const auto& part : idx.parts) {
    const auto lo = std::max(offset, part.start),
               hi = std::min(offset + size, part.start + part.size);
    // TAR payloads are half open. HTTP's final byte is inclusive.
    if (lo < hi) result.push_back({part, lo - part.start, hi - part.start - 1});
  }
  return result;
}
QByteArray Span::range() const {
  return "bytes=" + QByteArray::number(first) + "-" + QByteArray::number(last);
}
QByteArray Span::contentRange() const {
  return "bytes " + range().mid(6) + "/" + QByteArray::number(part.size);
}
}  // namespace stars::net::adsb
