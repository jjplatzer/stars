#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <algorithm>
#include <stdexcept>

#include "../util/resources.h"
#include "config.h"
#include "stars.h"

namespace stars {
using util::readCompressedResource;
std::shared_ptr<const renderer::CmdBuffer> STARSPane::geometry(
    const QJsonObject& geojson) {
  if (geojson["type"].toString() != "FeatureCollection" ||
      !geojson["features"].isArray())
    throw std::runtime_error("Invalid video-map FeatureCollection");
  renderer::LinesBuilder lines;
  QString defaultStyle;
  auto append = [&](const QJsonArray& coordinates) {
    std::vector<renderer::PointVertex> points;
    for (auto value : coordinates) {
      auto p = value.toArray();
      if (p.size() < 2 || !p[0].isDouble() || !p[1].isDouble()) {
        // A missing vertex breaks the line. Joining its neighbors would draw a
        // false segment.
        if (points.size() > 1) lines.addLineStrip(points);
        points.clear();
        continue;
      }
      points.push_back({float(p[0].toDouble()), float(p[1].toDouble())});
    }
    if (points.size() > 1) lines.addLineStrip(points);
  };
  for (auto value : geojson["features"].toArray()) {
    auto f = value.toObject(), p = f["properties"].toObject(),
         g = f["geometry"].toObject();
    if (p["isLineDefaults"].toBool()) {
      defaultStyle = p["style"].toString();
      continue;
    }
    const auto type = g["type"].toString();
    if (type != "LineString" && type != "MultiLineString") continue;
    auto style = p["style"].toString();
    if (style.isEmpty()) style = defaultStyle;
    if (!style.isEmpty() && style.compare("solid", Qt::CaseInsensitive) != 0)
      continue;
    if (!g["coordinates"].isArray())
      throw std::runtime_error("Invalid video-map line geometry");
    if (type == "LineString")
      append(g["coordinates"].toArray());
    else
      for (auto line : g["coordinates"].toArray()) append(line.toArray());
  }
  auto commands = std::make_shared<renderer::CmdBuffer>();
  lines.generateCommands(*commands);
  return commands;
}
void STARSPane::loadBundle(const QJsonObject& bundle) {
  if (bundle["version"].toInt() != 1 || !bundle["videoMaps"].isArray() ||
      bundle["tracon"].toString() != facility_)
    throw std::runtime_error("Invalid video-map bundle or facility mismatch");
  // Build once in geographic coordinates. Frames only change the projection;
  // discard JSON after compilation, and retain no large feature trees.
  auto loaded = maps_;
  for (auto value : bundle["videoMaps"].toArray()) {
    auto m = value.toObject();
    auto it = loaded.find(m["starsId"].toInt());
    if (it != loaded.end() && it->id == m["id"].toString())
      it->commands = geometry(m["geojson"].toObject());
  }
  maps_ = std::move(loaded);
  bundleLoaded_ = true;
}
void STARSPane::ensureBundle() {
  if (bundleLoaded_) return;
  auto root = qEnvironmentVariable("STARS_RESOURCES");
  if (root.isEmpty()) {
    root = QCoreApplication::applicationDirPath() + "/resources";
#ifdef __APPLE__
    if (!QDir(root).exists())
      root = QCoreApplication::applicationDirPath() + "/../Resources/resources";
#endif
  }
  loadBundle(parseJson(readCompressedResource(
      root + "/videomaps/" + facility_ + ".json.zst", 512 * 1024 * 1024)));
}
void STARSPane::toggleMap(int id) {
  if (visible_.remove(id)) return;
  if (!maps_.contains(id)) return;
  if (visible_.size() >= 50) throw std::runtime_error("CAPACITY-MAX MAPS");
  ensureBundle();
  if (!maps_[id].commands)
    throw std::runtime_error(QString("Missing video map %1 (%2)")
                                 .arg(id)
                                 .arg(maps_[id].name)
                                 .toStdString());
  visible_.insert(id);
}
renderer::RGB STARSPane::mapColor(int id) const {
  const bool groupB =
      maps_.value(id).category.compare("B", Qt::CaseInsensitive) == 0;
  return colors::brightness(
      groupB ? palette_.MapBDefault : palette_.MapADefault,
      groupB ? prefs_.brightness.videoGroupB : prefs_.brightness.videoGroupA);
}
void STARSPane::drawVideoMaps(renderer::CmdBuffer& cb, float dpi) const {
#ifdef __APPLE__
  cb.lineWidth(1);  // Native REDS OpenGL clamps GL_LINES to one physical pixel.
#else
  cb.lineWidth(std::max(1.f, dpi));
#endif
  for (auto it = maps_.begin(); it != maps_.end(); ++it)
    if (visible(it.key()) && it->commands) {
      cb.setRGB(mapColor(it.key()));
      cb.call(it->commands);
    }
}
}  // namespace stars
