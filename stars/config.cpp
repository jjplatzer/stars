#include "config.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "../util/resources.h"
#include "stars.h"

namespace stars {
using util::readCompressedResource;
namespace {
QPointF point(const QJsonObject& p) {
  return {p["lon"].toDouble(p["lng"].toDouble(p["longitude"].toDouble())),
          p["lat"].toDouble(p["latitude"].toDouble())};
}
bool valid(QPointF p) {
  return std::isfinite(p.x()) && std::isfinite(p.y()) &&
         std::abs(p.x()) <= 180 && std::abs(p.y()) <= 90 && !p.isNull();
}
struct Airports {
  QMap<QString, QString> icao, display;
  QMap<QString, QPointF> locations;
  Airports() {
    const auto data = QJsonDocument::fromJson(
                          readCompressedResource(":/resources/geo/apt.json.zst",
                                                 64 * 1024 * 1024))
                          .object();
    for (const auto& value : data["airports"].toArray()) {
      const auto a = value.toObject();
      const auto faa = a["faa_id"].toString(),
                 international = a["icao_id"].toString();
      if (a["latitude_deg"].isDouble() && a["longitude_deg"].isDouble()) {
        const QPointF location(a["longitude_deg"].toDouble(),
                               a["latitude_deg"].toDouble());
        if (!faa.isEmpty()) locations[faa] = location;
        if (!international.isEmpty()) locations[international] = location;
      }
      if (international.isEmpty()) continue;
      icao[international] = international;
      display[international] = faa.isEmpty() ? international.mid(1) : faa;
      if (!faa.isEmpty()) icao[faa] = international;
    }
  }
  QString normalize(QString code) const { return code.trimmed().toUpper(); }
  QString key(QString code) const {
    code = normalize(code);
    return icao.value(code, code);
  }
  QString label(QString code) const {
    code = normalize(code);
    return code.size() == 4 ? display.value(code, code.mid(1)) : code;
  }
};
const Airports& airportDatabase() {
  static const Airports airports;
  return airports;
}
}  // namespace
QString STARSPane::airportKey(const QString& raw) const {
  return airportDatabase().key(raw);
}
QString STARSPane::adaptedAltimeterStation(const QString& code) const {
  const auto id = airportDatabase().key(code);
  return facilityAltimeterStations().contains(id) ? id : QString{};
}

QJsonObject parseJson(const QByteArray& bytes) {
  QJsonParseError error;
  auto doc = QJsonDocument::fromJson(bytes, &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject())
    throw std::runtime_error("Invalid radar resource JSON");
  return doc.object();
}
STARSPane::STARSPane() { dcb_.radar(this); }
STARSPane::STARSPane(const QString& position, const QString& tcp)
    : STARSPane() {
  if (!position.isEmpty())
    configure(
        parseJson(readCompressedResource(
            ":/resources/config/" + position.section('/', 0, 0) + ".json.zst")),
        position.section('/', 1), tcp);
}
STARSPane::STARSPane(const QJsonObject& config, const QString& position,
                     const QString& tcp)
    : STARSPane() {
  configure(config, position, tcp);
}
STARSPane STARSPane::load(const QString& position) {
  return STARSPane(position);
}
void STARSPane::configure(const QJsonObject& config, const QString& position,
                          const QString& tcp) {
  if (config["version"].toInt() != 1)
    throw std::runtime_error("Unsupported STARS configuration version");
  artcc_ = config["artcc"].toString();
  facility_ = config["facility"].toString();
  if (QFile::exists(":/resources/mva/" + facility_ + ".geojson.zst")) {
    try {
      msawAirspace_ = MsawAirspace::load(facility_);
    } catch (const std::exception& error) {
      qWarning() << "MSAW unavailable for" << facility_ << error.what();
    }
  }
  const auto stars = config["starsConfiguration"].toObject();
  QJsonObject selected, settings, area;
  QString physicalFacility;
  for (auto value : config["positions"].toArray()) {
    auto p = value.toObject()["position"].toObject();
    if (p["id"].toString() == position) {
      selected = p;
      physicalFacility = value.toObject()["facility"].toString();
      settings = p["starsConfiguration"].toObject();
      break;
    }
  }
  positionFacility_ = physicalFacility;
  QPointF mean;
  int count = 0;
  for (auto value : stars["areas"].toArray()) {
    auto a = value.toObject();
    auto p = point(a["visibilityCenter"].toObject());
    if (valid(p)) {
      mean += p;
      ++count;
    }
    if (a["id"] == settings["areaId"]) area = a;
  }
  tcp_ = tcp.trimmed().toUpper();
  for (const auto& v : stars["tcps"].toArray()) {
    const auto entry = v.toObject();
    auto code =
        QString::number(entry["subset"].toInt()) + entry["sectorId"].toString();
    localTCPs_.insert(code);
    if (tcp_.isEmpty() && entry["id"] == settings["tcpId"]) tcp_ = code;
  }
  configureAlertSite();
  restrictions_.configure(artcc_ + '/' + facility_, config["dodOnly"].toBool());
  crda_.configure(stars["rpcs"].toArray());
  atpa_.configure(stars, artcc_ + '/' + facility_, tcp_);
  vfrSymbol_ = area["useVfrPositionSymbol"].toBool(true);
  const auto& airports = airportDatabase();
  for (const auto& v : area["underlyingAirports"].toArray())
    areaAirports_.insert(airports.key(v.toString()));
  for (const auto& v : area["towerListConfigurations"].toArray()) {
    const auto config = v.toObject();
    const auto code = airports.normalize(config["airportId"].toString());
    towerLists_.append({airports.label(code), airports.locations.value(code),
                        config["range"].toDouble()});
    if (towerLists_.size() == 3) break;
  }
  QMap<QString, QString> stations;
  for (const auto& v : stars["areas"].toArray())
    for (const auto& station : v.toObject()["ssaAirports"].toArray()) {
      const auto id = airports.key(station.toString());
      if (!id.isEmpty()) stations[id] = airports.label(id);
    }
  for (auto it = stations.cbegin(); it != stations.cend(); ++it)
    facilityStations_.append({it.key(), it.value()});
  std::sort(facilityStations_.begin(), facilityStations_.end(),
            [](const auto& a, const auto& b) { return a.label < b.label; });
  QStringList candidates{physicalFacility,
                         selected["callsign"].toString().section('_', 0, 0)};
  QSet<QString> seen;
  for (const auto& value : area["ssaAirports"].toArray()) {
    const auto code = airports.normalize(value.toString());
    const auto id = airports.key(code);
    if (id.isEmpty() || seen.contains(id)) continue;
    seen.insert(id);
    ssaAirports_.append({id, airports.label(code)});
    candidates.append(code);
  }
  for (const auto& candidate : candidates) {
    const auto known = airports.icao.constFind(airports.normalize(candidate));
    if (known != airports.icao.cend()) {
      primaryAirport_ = *known;
      break;
    }
  }
  if (count) mean /= count;
  defaultCenter_ = point(area["visibilityCenter"].toObject());
  if (!valid(defaultCenter_)) defaultCenter_ = mean;
  userCenter_ = defaultCenter_;
  longitudeScale_ = radar::longitudeScaleFactorForLat(
      valid(mean) ? mean.y() : defaultCenter_.y());
  const auto color = settings["colorSet"].toString().toLower();
  const bool military =
      color == "dod" || config["dodOnly"].toBool() ||
      (physicalFacility == facility_ &&
       config["type"].toString().endsWith("Rapcon", Qt::CaseInsensitive));
  const bool tower =
      selected["callsign"].toString().endsWith("_TWR", Qt::CaseInsensitive);
  defaultPalette_ = military                    ? &colors::dod
                    : (tower || color == "tdw") ? &colors::tdw
                                                : &colors::tcw;
  palette_ = *defaultPalette_;
  for (auto value : config["videoMaps"].toArray()) {
    auto m = value.toObject();
    int id = m["starsId"].toInt();
    if (id > 0) {
      maps_[id] = {m["id"].toString(),
                   m["name"].toString(),
                   m["shortName"].toString(),
                   m["starsBrightnessCategory"].toString(),
                   {}};
      listing_.emplace_back(id, maps_[id]);
    }
  }
  // REDS resolves duplicate IDs to the last map for drawing, while its
  // reference list retains metadata entries in stable numeric order.
  std::stable_sort(
      listing_.begin(), listing_.end(),
      [](const auto& a, const auto& b) { return a.first < b.first; });
  dcb_ = Dcb(config, position, tcp);
  dcb_.radar(this);
}
Dcb Dcb::load(const QString& position, const QString& tcp) {
  const auto adaptation = position.section('/', 0, 0);
  if (adaptation.isEmpty()) return {};
  const auto data =
      readCompressedResource(":/resources/config/" + adaptation + ".json.zst");
  QJsonParseError error;
  auto doc = QJsonDocument::fromJson(data, &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject())
    throw std::runtime_error("Invalid STARS adaptation");
  return Dcb(doc.object(), position.section('/', 1), tcp);
}

Dcb::Dcb(const QJsonObject& configuration, const QString& position,
         const QString& tcp) {
  const auto config = configuration["starsConfiguration"].toObject();
  QString code = tcp.trimmed();
  QJsonObject selected, area;
  QString physicalFacility;
  for (const auto& value : configuration["positions"].toArray()) {
    auto p = value.toObject()["position"].toObject();
    if (p["id"].toString() == position) {
      selected = p;
      physicalFacility = value.toObject()["facility"].toString();
      break;
    }
  }
  const auto settings = selected["starsConfiguration"].toObject();
  if (code.isEmpty())
    for (const auto& value : config["tcps"].toArray()) {
      const auto t = value.toObject();
      if (t["id"] == settings["tcpId"]) {
        code = QString::number(t["subset"].toInt()) + t["sectorId"].toString();
        break;
      }
    }
  for (const auto& value : config["areas"].toArray()) {
    auto a = value.toObject();
    if (a["id"] == settings["areaId"]) {
      area = a;
      break;
    }
  }
  atpa_ = !config["atpaVolumes"].toArray().isEmpty();
  QMap<int, QString> labels;
  for (const auto& value : configuration["videoMaps"].toArray()) {
    auto m = value.toObject();
    const int id = m["starsId"].toInt();
    if (id == 0) continue;
    auto label = m["shortName"].toString().trimmed();
    if (label.isEmpty()) label = m["name"].toString().trimmed();
    labels[id] = QString::number(id) + "\n" + label;
  }
  for (const auto& value : config["mapGroups"].toArray()) {
    const auto group = value.toObject();
    bool matches = false;
    for (const auto& candidate : group["tcps"].toArray())
      if (!code.isEmpty() && candidate.toString().trimmed().compare(
                                 code, Qt::CaseInsensitive) == 0)
        matches = true;
    if (!matches) continue;
    const auto ids = group["mapIds"].toArray();
    for (int i = 0; i < std::min(int(maps_.size()), int(ids.size())); ++i)
      maps_[i] = labels.value(ids[i].toInt());
    break;
  }
  // MAIN uses the position's primary airport, followed by distinct SSA
  // airports in adaptation order, matching REDS initializeGIText().
  const auto& airports = airportDatabase();
  QSet<QString> seen;
  int next = 0;
  const auto add = [&](QString code) {
    code = airports.normalize(code);
    const auto key = airports.key(code);
    if (code.isEmpty() || seen.contains(key) || next == int(airports_.size()))
      return;
    seen.insert(key);
    airports_[next++] = airports.label(code);
  };
  for (auto code :
       {physicalFacility, selected["callsign"].toString().section('_', 0, 0)}) {
    code = airports.normalize(code);
    if (airports.icao.contains(code)) {
      add(code);
      break;
    }
  }
  for (const auto& value : area["ssaAirports"].toArray()) add(value.toString());
}

}  // namespace stars
