#include "msaw.h"

#include <QJsonArray>
#include <QLineF>
#include <QPolygonF>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "../aviation/finals.h"
#include "../util/resources.h"
#include "config.h"
#include "net/track.h"
#include "stars.h"

namespace stars {
namespace {
constexpr double radians = std::numbers::pi / 180;
constexpr double earthRadiusNM = 3440.065;
constexpr double airportRadiusNM = 5;
constexpr int airportHeightFeet = 2000;

struct Airport {
  QPointF position;
  int elevation;
  QString faa, icao;
  QVector<QLineF> runways;
};

QVector<Airport> loadAirports() {
  const auto bytes = util::readCompressedResource(
      ":/resources/geo/apt.json.zst", 64 * 1024 * 1024);
  QVector<Airport> airports;
  for (const auto& entry : parseJson(bytes)["airports"].toArray()) {
    const auto airport = entry.toObject();
    const auto lat = airport["latitude_deg"];
    const auto lon = airport["longitude_deg"];
    const auto elevation = airport["elevation_ft_msl"];
    if (!lat.isDouble() || !lon.isDouble() || !elevation.isDouble()) continue;
    Airport site{{lon.toDouble(), lat.toDouble()},
                 int(std::lround(elevation.toDouble())),
                 airport["faa_id"].toString(),
                 airport["icao_id"].toString(),
                 {}};
    for (const auto& runway : airport["runways"].toArray()) {
      const auto ends = runway.toObject()["ends"].toArray();
      if (ends.size() != 2) continue;
      QVector<QPointF> points;
      for (const auto& end : ends) {
        const auto fields = end.toObject();
        if (fields["longitude_deg"].isDouble() &&
            fields["latitude_deg"].isDouble())
          points.append({fields["longitude_deg"].toDouble(),
                         fields["latitude_deg"].toDouble()});
      }
      if (points.size() == 2 && points.first() != points.last())
        site.runways.append(QLineF(points.first(), points.last()));
    }
    airports.append(std::move(site));
  }
  std::ranges::sort(airports, {},
                    [](const Airport& a) { return a.position.y(); });
  return airports;
}

const QVector<Airport>& airportIndex() {
  static const auto airports = loadAirports();
  return airports;
}

double distanceNM(QPointF a, QPointF b) {
  const QPointF halfAngle = (b - a) * (radians / 2);
  const double h = std::pow(std::sin(halfAngle.y()), 2) +
                   std::cos(a.y() * radians) * std::cos(b.y() * radians) *
                       std::pow(std::sin(halfAngle.x()), 2);
  return 2 * earthRadiusNM * std::asin(std::sqrt(std::clamp(h, 0., 1.)));
}

double runwayDistance(QPointF point, QLineF runway) {
  const auto local = [point](QPointF end) {
    return QPointF(std::remainder(end.x() - point.x(), 360.) *
                       std::cos(point.y() * radians),
                   end.y() - point.y());
  };
  const QPointF first = local(runway.p1());
  const QPointF segment = local(runway.p2()) - first;
  const double lengthSquared = QPointF::dotProduct(segment, segment);
  if (lengthSquared < 1e-16) return std::numeric_limits<double>::infinity();
  const double t =
      std::clamp(-QPointF::dotProduct(first, segment) / lengthSquared, 0., 1.);
  return distanceNM(
      point, runway.p1() +
                 t * QPointF(std::remainder(runway.dx(), 360.), runway.dy()));
}

std::optional<QPointF> climbOrigin(const net::RadarTrack& track) {
  const auto& report = track.report;
  if (!report.altitude || !report.location) return {};
  const int altitude = *report.altitude;
  int count = 1, oldestAltitude = altitude, recentAltitude = altitude;
  double oldestAge = 0, recentAge = 0, sumTime = 0, sumHeight = altitude;
  double sumTimeSquared = 0, sumTimeHeight = 0;
  QPointF oldest;
  for (const auto& sample : track.history) {
    const double age = (report.mrtMs - sample.time) / 1000.;
    if (age <= 0 || age > 30 || !sample.altitude) continue;
    const int height = *sample.altitude;
    if (height < -1500 || height > 60000) continue;
    if (age <= 6 && height > altitude + 100) return {};
    ++count;
    sumTime -= age;
    sumHeight += height;
    sumTimeSquared += age * age;
    sumTimeHeight -= age * height;
    if (age > oldestAge) {
      oldestAge = age;
      oldestAltitude = height;
      oldest = sample.point;
    }
    if (age >= 2 && age <= 8 && age > recentAge) {
      recentAge = age;
      recentAltitude = height;
    }
  }
  if (count < 3 || oldestAge < 8 || altitude - oldestAltitude < 120 ||
      recentAge == 0 || altitude <= recentAltitude)
    return {};
  const double slope = (count * sumTimeHeight - sumTime * sumHeight) /
                       (count * sumTimeSquared - sumTime * sumTime);
  if (60 * slope < 300 || distanceNM(oldest, *report.location) < .08) return {};
  return oldest;
}

bool contains(const QVector<QPointF>& ring, QPointF point) {
  bool inside = false;
  QPointF a = ring.last();
  for (QPointF b : ring) {
    const auto edge = b - a;
    const auto delta = point - a;
    const double cross = delta.x() * edge.y() - delta.y() * edge.x();
    if (std::abs(cross) < 1e-10 && QRectF(a, b)
                                       .normalized()
                                       .adjusted(-1e-10, -1e-10, 1e-10, 1e-10)
                                       .contains(point))
      return true;
    if ((a.y() > point.y()) != (b.y() > point.y()) &&
        point.x() < a.x() + edge.x() * delta.y() / edge.y())
      inside = !inside;
    a = b;
  }
  return inside;
}
}  // namespace

bool withinMsawAirportVolume(QPointF position, int altitude) {
  const auto& airports = airportIndex();
  constexpr double latRadius = airportRadiusNM / earthRadiusNM / radians;
  const auto latitude = [](const Airport& a) { return a.position.y(); };
  auto it = std::ranges::lower_bound(airports, position.y() - latRadius, {},
                                     latitude);
  for (; it != airports.end() && latitude(*it) <= position.y() + latRadius;
       ++it) {
    if (altitude < it->elevation ||
        altitude > it->elevation + airportHeightFeet)
      continue;
    const QPointF halfAngle = (it->position - position) * (radians / 2);
    const double haversine = std::pow(std::sin(halfAngle.y()), 2) +
                             std::cos(position.y() * radians) *
                                 std::cos(latitude(*it) * radians) *
                                 std::pow(std::sin(halfAngle.x()), 2);
    if (haversine <=
        std::pow(std::sin(airportRadiusNM / (2 * earthRadiusNM)), 2))
      return true;
  }
  return false;
}

bool msawInitialClimb(const net::RadarTrack& track) {
  if (!track.report.plan) return false;
  const auto first = climbOrigin(track);
  if (!first) return false;
  const auto current = *track.report.location;
  const auto& plan = *track.report.plan;
  QString departure = plan.origin, destination = plan.destination;
  if (departure.isEmpty() &&
      QStringList{"P", "D", "DEP", "DEPARTURE"}.contains(plan.type))
    departure = plan.airport;
  if (destination.isEmpty() &&
      QStringList{"A", "ARR", "ARRIVAL"}.contains(plan.type))
    destination = plan.airport;
  const bool unknown = departure.isEmpty() && destination.isEmpty();
  for (const auto& airport : airportIndex()) {
    const auto matches = [&](const QString& id) {
      return !id.isEmpty() && (id == airport.faa || id == airport.icao);
    };
    if (!unknown && !matches(departure) && !matches(destination)) continue;
    for (const auto& runway : airport.runways) {
      if (std::abs(current.y() - runway.y1()) > .051 &&
          std::abs(current.y() - runway.y2()) > .051 &&
          (current.y() < runway.y1()) == (current.y() < runway.y2()))
        continue;
      const double end = runwayDistance(current, runway);
      if (end > 3) continue;
      if (!unknown) return true;
      const double start = runwayDistance(*first, runway);
      if (start <= .75 && end - start >= .12) return true;
    }
  }
  return false;
}

MsawAirspace MsawAirspace::load(const QString& facility) {
  const auto path = QString(":/resources/mva/%1.geojson.zst").arg(facility);
  const auto data =
      parseJson(util::readCompressedResource(path, 64 * 1024 * 1024));
  MsawAirspace result;
  for (const auto& entry : data["features"].toArray()) {
    const auto feature = entry.toObject();
    const auto geometry = feature["geometry"].toObject();
    const auto coordinates = geometry["coordinates"].toArray();
    const auto type = geometry["type"].toString();
    const auto polygons =
        type == "Polygon" ? QJsonArray{coordinates} : coordinates;
    if (type != "Polygon" && type != "MultiPolygon")
      throw std::runtime_error("Unexpected MVA geometry");
    for (const auto& polygon : polygons) {
      Sector sector{};
      sector.minimum = feature["properties"].toObject()["minimum_ft"].toInt();
      for (const auto& points : polygon.toArray()) {
        QPolygonF ring;
        for (const auto& value : points.toArray()) {
          const auto xy = value.toArray();
          if (xy.size() < 2 || !xy[0].isDouble() || !xy[1].isDouble())
            throw std::runtime_error("Invalid MVA coordinate");
          ring.append({xy[0].toDouble(), xy[1].toDouble()});
        }
        if (ring.size() < 3) throw std::runtime_error("Invalid MVA ring");
        if (sector.rings.empty()) sector.bounds = ring.boundingRect();
        sector.rings.append(ring);
      }
      if (sector.rings.empty() || sector.minimum <= 0)
        throw std::runtime_error("Invalid MVA sector");
      result.sectors_.append(std::move(sector));
    }
  }
  return result;
}

std::optional<int> MsawAirspace::minimumAt(QPointF position) const {
  for (const auto& sector : sectors_) {
    if (!sector.bounds.contains(position) ||
        !contains(sector.rings.front(), position))
      continue;
    if (std::none_of(
            sector.rings.begin() + 1, sector.rings.end(),
            [&](const auto& hole) { return contains(hole, position); }))
      return sector.minimum;
  }
  return std::nullopt;
}
}  // namespace stars

namespace stars {
const STARSPane::LowAltitudeEvent* STARSPane::msawAlert(
    const net::RadarTrack& track) const {
  if (!archiveAvailable_ || !msawSync_ || trafficMs_ - *msawSync_ < 30000 ||
      track.owner != tcp_ || !alertEnabled("MSAW"))
    return nullptr;
  const auto event = alertSite_->lowAltitude.constFind(track.report.number);
  if (event == alertSite_->lowAltitude.cend() || !event->matches(track) ||
      !event->since || event->suppressed || event->disabled)
    return nullptr;
  return &event.value();
}

bool STARSPane::msawAudioWanted() const {
  for (const auto& track : traffic_.tracks()) {
    const auto* event = msawAlert(track);
    if (event && !event->acknowledged && trafficMs_ >= *event->since &&
        trafficMs_ - *event->since < 10000)
      return true;
  }
  return false;
}

bool STARSPane::establishedOnFinal(const net::RadarTrack& track) {
  const auto& report = track.report;
  const auto& plan = *report.plan;
  if (!plan.ifr || plan.type == "P" || plan.type == "D" ||
      plan.type.startsWith("DEP")) {
    msawApproaches_.remove(report.number);
    return false;
  }
  QString airport =
      airportKey(plan.destination.isEmpty() ? plan.airport : plan.destination);
  if (airport.size() == 3) airport.prepend('K');
  if (airport.size() != 4) {
    msawApproaches_.remove(report.number);
    return false;
  }
  auto runway = normalizedRunway(plan.runway);
  if (!runway.isEmpty() && (runway.size() == 1 || !runway[1].isDigit()))
    runway.prepend('0');
  const QPointF position = *report.location;
  const auto sample =
      aviation::matchFinal(airport, runway, position, *report.altitude);
  if (!sample) {
    msawApproaches_.remove(report.number);
    return false;
  }
  const auto aligned = [&](QPointF from) {
    QPointF movement = position - from;
    movement.rx() *= std::cos((position.y() + from.y()) * radians / 2);
    const double length = std::hypot(movement.x(), movement.y());
    return length > 1e-9 && QPointF::dotProduct(movement, sample->tangent) >=
                                length * std::cos(20 * radians);
  };
  auto& state = msawApproaches_[report.number];
  const auto when = report.mrtMs;
  if (state.firstMrt != track.firstMrtMs ||
      state.association != track.associationSinceMs ||
      state.address != report.address || state.procedure != sample->procedure ||
      state.airport != airport || when < state.last ||
      when - state.last > 30000) {
    state = {sample->procedure,
             airport,
             report.address,
             track.firstMrtMs,
             track.associationSinceMs,
             when,
             when,
             position,
             sample->along,
             sample->along};
    for (auto it = track.history.crbegin(); it != track.history.crend(); ++it) {
      if (when - it->time < 7000 || when - it->time > 30000 || !it->altitude ||
          !aligned(it->point))
        continue;
      const auto previous = aviation::matchFinal(
          airport, runway, it->point, *it->altitude, sample->procedure);
      if (previous && sample->along - previous->along >= .12) {
        state.start = it->time;
        state.startPosition = it->point;
        state.startAlong = previous->along;
        break;
      }
    }
  } else if (when > state.last) {
    if (sample->along < state.lastAlong - .08) {
      state.start = when;
      state.startPosition = position;
      state.startAlong = sample->along;
    }
    state.last = when;
    state.lastAlong = sample->along;
  }
  return when - state.start >= 7000 &&
         sample->along - state.startAlong >= .12 &&
         aligned(state.startPosition);
}

bool STARSPane::lowAltitudeCondition(const net::RadarTrack& track) {
  const auto& report = track.report;
  const auto& plan = report.plan;
  // Only an uninterrupted sequence of applicable reports can establish a final.
  const auto clearObservation = [&] {
    msawApproaches_.remove(report.number);
    return false;
  };
  if (!plan || plan->deleted || plan->suspended || !report.altitude ||
      !report.location)
    return clearObservation();
  if (report.mrtMs > trafficMs_ + 2000 || trafficMs_ - report.mrtMs > 30000)
    return clearObservation();
  if (!plan->ifr && (plan->vfr || report.beacon == "1200"))
    return clearObservation();

  const auto minimum = msawAirspace_->minimumAt(*report.location);
  if (!minimum || *report.altitude >= *minimum) return clearObservation();
  if (withinMsawAirportVolume(*report.location, *report.altitude))
    return clearObservation();
  if (msawInitialClimb(track)) return clearObservation();
  return !establishedOnFinal(track);
}

void STARSPane::updateMsaw(bool changed) {
  if (!msawAirspace_ || !archiveAvailable_) {
    msawEvaluated_ = false;
    msawSync_.reset();
    msawApproaches_.clear();
    return;
  }
  if (!msawSync_) msawSync_ = trafficMs_;
  if (trafficMs_ - *msawSync_ < 30000) return;
  if (msawEvaluated_ && !changed) return;
  msawEvaluated_ = true;

  auto& events = alertSite_->lowAltitude;
  const auto& tracks = traffic_.tracks();
  msawApproaches_.removeIf([&](auto it) { return !tracks.contains(it.key()); });
  events.removeIf([&](auto it) {
    const auto track = tracks.constFind(it.key());
    return track == tracks.cend() || !it.value().matches(*track);
  });
  for (const auto& track : tracks) {
    const auto& report = track.report;
    if (tcp_.isEmpty() || track.owner != tcp_) continue;
    const bool low = lowAltitudeCondition(track);
    auto event = events.find(report.number);
    if (low && event == events.end()) {
      event = events.insert(report.number, {report.address,
                                            track.owner,
                                            track.firstMrtMs,
                                            track.associationSinceMs,
                                            {}});
    }
    if (event == events.end()) continue;
    if (low && !event->disabled) {
      if (!event->since) event->since = trafficMs_;
    } else {
      event->since.reset();
      event->acknowledged = false;
      event->suppressed = false;
      if (!event->disabled) events.erase(event);
    }
  }
}

bool STARSPane::msawCommand(const QString& key) {
  if (input_ != "Q" && input_ != "V") return false;
  const auto track = traffic_.tracks().constFind(key);
  if (track == traffic_.tracks().cend()) {
    response_ = "NO TRK";
    return true;
  }
  if (!track->report.plan || track->report.frozen || tcp_.isEmpty() ||
      track->owner != tcp_) {
    response_ = "ILL TRK";
    return true;
  }
  auto& events = alertSite_->lowAltitude;
  auto event = events.find(key);
  if (input_ == "Q") {
    if (!msawAlert(*track)) {
      response_ = "ILL TRK";
      return true;
    }
    event->suppressed = true;
  } else {
    if (!msawAirspace_ || !alertEnabled("MSAW")) {
      response_ = "ILL FNCT - MSAW OFF";
      return true;
    }
    if (event == events.end() || !event->matches(*track))
      event = events.insert(key, {track->report.address,
                                  track->owner,
                                  track->firstMrtMs,
                                  track->associationSinceMs,
                                  {}});
    if (event->disabled)
      events.erase(event);
    else {
      event->disabled = true;
      event->suppressed = false;
    }
  }
  msawEvaluated_ = false;
  response_.clear();
  return true;
}
}  // namespace stars
