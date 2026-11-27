#include "itws.h"

#include <QMap>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace stars {
namespace {
struct Field {
  QString text, unit, precision;
};
using Fields = QMap<QString, Field>;
void fields(QXmlStreamReader& xml, Fields& out, bool nested = false) {
  while (xml.readNextStartElement()) {
    const auto key = xml.name().toString();
    if (!nested && (key == "product_header_msg_id" || key == "prcp_nws_levels"))
      fields(xml, out, true);
    else if (key.startsWith("prcp_") || key.startsWith("product_")) {
      const auto attributes = xml.attributes();
      out[key] = {xml.readElementText(), attributes.value("unit").toString(),
                  attributes.value("precision").toString()};
    } else
      xml.skipCurrentElement();
  }
}
}  // namespace
std::optional<ItwsGrid> decodeItws(QByteArray body) {
  if (body.startsWith("\x1f\x8b")) body = inflateWeatherGzip(body);
  QXmlStreamReader xml(body);
  if (!xml.readNextStartElement() || xml.name() != u"itws_msg") return {};
  Fields data;
  while (xml.readNextStartElement()) {
    if (xml.name() == u"product_header") {
      fields(xml, data);
      if (data.value("product_msg_id").text.trimmed() != "9850") return {};
    } else if (xml.name() == u"precip")
      fields(xml, data);
    else
      xml.skipCurrentElement();
  }
  if (xml.hasError() ||
      data.value("prcp_grid_compression_encoding_scheme").text.trimmed() !=
          "Simple Run-Length Encoding")
    return {};
  bool valid = true;
  auto integer = [&](QString key) {
    bool ok;
    const auto n = data.value(key).text.toLongLong(&ok);
    valid &= ok;
    return n;
  };
  auto timestamp = [&](QString key) -> qint64 {
    const auto seconds = integer(key + "_seconds");
    const auto ms = data.contains(key + "_milliseconds")
                        ? integer(key + "_milliseconds")
                        : 0;
    if (seconds < 0 || seconds > 9000000000LL || ms < 0 || ms >= 1000) {
      valid = false;
      return 0;
    }
    return seconds * 1000 + ms;
  };
  auto scaled = [&](QString key, QString unit) {
    const auto field = data.value("prcp_" + key);
    bool ok = true;
    const double scale =
        field.precision.isEmpty() ? 1 : field.precision.toDouble(&ok);
    valid &= ok && scale > 0 && field.unit == unit;
    const double value = field.text.toDouble(&ok) * scale;
    valid &= ok && std::isfinite(value);
    return value;
  };
  ItwsGrid g;
  g.source = data.value("product_header_source_id").text.trimmed().toUpper();
  g.generated = timestamp("product_header_generation_time");
  g.expires = timestamp("product_header_expiration_time");
  g.latitude = scaled("TRP_latitude", "degrees");
  g.longitude = scaled("TRP_longitude", "degrees");
  g.rotation = scaled("rotation", "degrees");
  g.x = scaled("xoffset", "meters");
  g.y = scaled("yoffset", "meters");
  g.dx = scaled("dx", "meters");
  g.dy = scaled("dy", "meters");
  const auto w = integer("prcp_grid_max_x"), h = integer("prcp_grid_max_y");
  const auto rows = integer("prcp_nrows"), cols = integer("prcp_ncols");
  if (integer("prcp_nlevels") != 1 || !valid || g.source.isEmpty() ||
      g.expires <= g.generated || std::abs(g.latitude) > 90 ||
      std::abs(g.longitude) > 180 || std::abs(g.rotation) > 360 || g.dx <= 0 ||
      g.dy <= 0 || h <= 0 || w <= 0 || w > 4000000 / h || rows < 1 ||
      rows > h || cols < 1 || cols > w)
    return {};
  g.width = w;
  g.rows = rows;
  g.columns = cols;
  std::array<bool, 16> allowed{};
  for (int n = 0; n <= 6; ++n) allowed[n] = true;
  for (auto key : {"prcp_attenuated", "prcp_ap_detected", "prcp_bad_value",
                   "prcp_no_coverage"}) {
    const auto status = integer(key);
    if (!valid || status < 7 || status > 15 || allowed[status]) return {};
    allowed[status] = true;
  }
  const auto noCoverage = integer("prcp_no_coverage");
  const auto encoded = data.value("prcp_grid_compressed").text.trimmed();
  static const QRegularExpression run(R"(\G\s*(\d+)\s*,\s*(\d+))");
  qsizetype offset = 0;
  auto runs = run.globalMatch(encoded);
  while (runs.hasNext()) {
    const auto m = runs.next();
    bool ok;
    const auto level = m.captured(1).toInt(&ok);
    if (!ok || level < 0 || level > 15 || !allowed[level]) return {};
    const auto count = m.captured(2).toInt(&ok);
    if (!ok || count < 1 || count > w * h - g.cells.size()) return {};
    g.cells.append(count, char(level));
    offset = m.capturedEnd();
  }
  if (offset != encoded.size() || g.cells.size() != w * h) return {};
  int maxLevel = 0;
  for (int row = 0; row < h; ++row)
    for (int col = 0; col < w; ++col) {
      const int value = g.cells[row * w + col];
      if (row >= rows || col >= cols) {
        if (value != noCoverage) return {};
      } else if (value <= 6)
        maxLevel = std::max(maxLevel, value);
    }
  if (maxLevel != integer("prcp_grid_max_precip_level") || !valid) return {};
  return g;
}

std::shared_ptr<const NexradFrame> ItwsGrid::render() const {
  using renderer::PointVertex;
  const double angle = qDegreesToRadians(rotation),
               lat = qDegreesToRadians(latitude);
  const double cs = std::cos(angle), sn = std::sin(angle), cl = std::cos(lat),
               sl = std::sin(lat);
  const auto vertex = [&](int col, int row) -> PointVertex {
    const double a = x + col * dx, b = y + (rows - row) * dy;
    const double east = cs * a + sn * b, north = -sn * a + cs * b;
    const double rho = std::hypot(east, north);
    if (rho == 0) return {float(longitude), float(latitude)};
    const double c = 2 * std::atan2(rho, 2 * 6371008.8), sc = std::sin(c),
                 cc = std::cos(c);
    return {float(longitude + qRadiansToDegrees(std::atan2(
                                  east * sc, rho * cl * cc - north * sl * sc))),
            float(qRadiansToDegrees(std::asin(
                std::clamp(cc * sl + north * sc * cl / rho, -1., 1.))))};
  };
  std::array<renderer::TrianglesBuilder, 6> meshes;
  std::vector<PointVertex> top(columns + 1), bottom(columns + 1);
  for (int i = 0; i <= columns; ++i) top[i] = vertex(i, 0);
  for (int row = 0; row < rows; ++row) {
    for (int i = 0; i <= columns; ++i) bottom[i] = vertex(i, row + 1);
    for (int col = 0; col < columns; ++col) {
      const int level = cells[row * width + col];
      if (level >= 1 && level <= 6)
        meshes[level - 1].addQuad(top[col], top[col + 1], bottom[col + 1],
                                  bottom[col]);
    }
    top.swap(bottom);
  }
  auto frame = std::make_shared<NexradFrame>();
  frame->observed = generated / 1000;
  buildWeatherLevels(*frame, meshes);
  return frame;
}
}  // namespace stars
