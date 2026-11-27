#include "adsbtrace.h"

#include <zlib.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <array>
#include <cmath>
#include <stdexcept>
namespace stars::net::adsb {
namespace {
QByteArray unpack(const QByteArray& gzip) {
  if (gzip.isEmpty() || gzip.size() > kCompressedLimit)
    throw std::runtime_error("compressed trace size");
  z_stream z{};
  if (inflateInit2(&z, MAX_WBITS + 16) != Z_OK)
    throw std::runtime_error("gzip initialization");
  auto cleanup = qScopeGuard([&] { inflateEnd(&z); });
  z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(gzip.constData()));
  z.avail_in = gzip.size();
  std::array<char, 65536> chunk;
  QByteArray json;
  int status;
  do {
    z.next_out = reinterpret_cast<Bytef*>(chunk.data());
    z.avail_out = chunk.size();
    status = inflate(&z, Z_NO_FLUSH);
    if (status != Z_OK && status != Z_STREAM_END)
      throw std::runtime_error("invalid gzip");
    const auto n = chunk.size() - z.avail_out;
    if (json.size() + qint64(n) > 64 * kMiB)
      throw std::runtime_error("trace JSON size");
    json.append(chunk.data(), n);
  } while (status != Z_STREAM_END);
  if (z.avail_in) throw std::runtime_error("trailing gzip data");
  return json;
}
Source source(const QJsonValue& value) {
  if (value == "adsb_icao") return Source::Adsb;
  if (value == "adsr_icao") return Source::Adsr;
  if (value == "tisb_icao") return Source::Tisb;
  if (value == "mlat") return Source::Mlat;
  return value.toString().isEmpty() || value == "unknown" ? Source::Unknown
                                                          : Source::Other;
}
}  // namespace
Slice decodeTrace(const QByteArray& gzip, const QString& icao, QDate date,
                  const QString& key, qint64 begin, qint64 end) {
  auto doc = QJsonDocument::fromJson(unpack(gzip));
  auto obj = doc.object();
  if (!normalizeIcao(icao) || normalizeIcao(obj["icao"].toString()) != icao ||
      !obj["timestamp"].isDouble() || !obj["trace"].isArray() || begin >= end)
    throw std::runtime_error("invalid trace header or identity");
  const long double base = obj["timestamp"].toDouble();
  Slice result{key, date, begin, end, {}};
  std::optional<qint64> previous;
  bool gap = true, successor = false;
  for (const auto& item : obj["trace"].toArray()) {
    auto row = item.toArray();
    if (!row.at(0).isDouble())
      throw std::runtime_error("missing observation time");
    const long double ms = std::round((base + row.at(0).toDouble()) * 1000.L);
    // Use the supported civil time domain, through year 9999.
    if (!(ms >= 0 && ms < 253402300800000.L))
      throw std::runtime_error("invalid observation time");
    const auto time = qint64(ms);
    if (previous && time < *previous)
      throw std::runtime_error("unordered trace");
    const bool duplicate = previous && time == *previous;
    previous = time;
    auto lat = row.at(1), lon = row.at(2);
    auto flags = row.at(6);
    const qint64 bits =
        flags.isNull() || flags.isUndefined() ? 0 : flags.toInteger(-1);
    if (bits < 0 || bits > UINT32_MAX)
      throw std::runtime_error("invalid trace flags");
    const bool valid = lat.isDouble() && lon.isDouble() &&
                       std::abs(lat.toDouble()) <= 90 &&
                       std::abs(lon.toDouble()) <= 180;
    Observation p{time,
                  {lon.toDouble(), lat.toDouble()},
                  quint32(bits),
                  source(row.at(9)),
                  gap || !valid || duplicate || bool(bits & 3),
                  valid};
    if (duplicate && !result.points.empty() &&
        result.points.back().timeMs == time)
      result.points.back().breakBefore = true;
    gap = !valid || duplicate || (bits & 1);
    if (time < begin) {
      if (result.points.empty())
        result.points.push_back(p);
      else
        result.points.back() = p;
    } else if (time < end || !successor) {
      result.points.push_back(p);
      successor = time >= end;
    }
  }
  return result;
}
}  // namespace stars::net::adsb
