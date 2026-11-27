#include "atpa.h"

#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <tuple>

#include "radar/latlon.h"
#include "radar/magnetic.h"

namespace stars {
QString normalizedRunway(QString text) {
  text = text.trimmed().toUpper().remove(' ').remove('-');
  static const QRegularExpression re(
      "^(?:RUNWAY|RWY|RW)?0?([1-9]|[12][0-9]|3[0-6])([LCR]?)$");
  const auto m = re.match(text);
  return m.hasMatch() ? m.captured(1) + m.captured(2) : QString{};
}

namespace {
QString code(const QJsonValue& v) { return v.toString().trimmed().toUpper(); }
bool secondEnum(const QJsonValue& v, const QString& name) {
  return v.toInt() == 1 || code(v) == "1" || code(v) == name;
}
double magnitude(QPointF p) { return std::hypot(p.x(), p.y()); }
double separation(const QString& front, const QString& back, bool reduced) {
  // Consolidated wake turbulence approach categories A through I. Zero uses the
  // radar minimum.
  static constexpr double wake[9][9] = {{0, 5, 6, 6, 7, 7, 7, 8, 8},
                                        {0, 3, 4, 4, 5, 5, 5, 5, 6},
                                        {0, 0, 0, 0, 3.5, 3.5, 3.5, 5, 6},
                                        {0, 3, 4, 4, 5, 5, 5, 6, 6},
                                        {0, 0, 0, 0, 0, 0, 0, 0, 4},
                                        {0, 0, 0, 0, 0, 0, 0, 0, 4},
                                        {},
                                        {},
                                        {}};
  const auto index = [](const QString& c) {
    return c.size() == 1 ? int(c[0].unicode()) - 'A' : -1;
  };
  const int a = index(front), b = index(back);
  if (a < 0 || a > 8 || b < 0 || b > 8) return 0;
  return wake[a][b] > 0 ? wake[a][b] : reduced ? 2.5 : 3;
}
struct Model {
  QPointF position, direction;
  double speed;
  Model(QPointF p, QPointF velocity)
      : position(p),
        direction(velocity / magnitude(velocity)),
        speed(magnitude(velocity)) {}
  void advance() {
    double gs = speed;
    const auto remaining = magnitude(position);
    if (remaining < 2)
      gs = std::min(speed, 120.);
    else if (remaining < 5)
      gs = 120 + (remaining - 2) / 3 * (speed - 120);
    position += direction * (gs / 3600);
  }
};
AtpaStatus predicted(double distance, double minimum, Model front, Model back) {
  if (distance < minimum) return AtpaStatus::Alert;
  for (int second = 1; second <= 45; ++second) {
    front.advance();
    back.advance();
    if (magnitude(front.position - back.position) < minimum)
      return second <= 24 ? AtpaStatus::Alert : AtpaStatus::Warning;
  }
  return AtpaStatus::Monitor;
}
QString chop(QString line) {
  if (line.size() <= 32) return line;
  line.truncate(32);
  return line.left(line.lastIndexOf(' ')) + '+';
}
}  // namespace
QPointF Atpa::Volume::offset(QPointF p) const {
  return {math::normalizeLongitude(p.x() - threshold.x()) * scale,
          (p.y() - threshold.y()) * 60};
}
QPointF Atpa::Volume::coordinates(QPointF p) const {
  const auto nm = offset(p);
  return {QPointF::dotProduct(nm, axis),
          -nm.x() * axis.y() + nm.y() * axis.x()};
}
void Atpa::configure(const QJsonObject& adaptation, const QString& siteKey,
                     const QString& tcp) {
  // Radar panes and operator commands run on the UI thread. Supervisor state
  // lasts for the site session.
  static QMap<QString, std::shared_ptr<Site>> sites;
  *this = Atpa{};
  tcp_ = tcp;
  QMap<QString, QString> codes;
  for (const auto& item : adaptation["tcps"].toArray()) {
    const auto t = item.toObject();
    codes.insert(code(t["id"]),
                 QString::number(t["subset"].toInt()) + code(t["sectorId"]));
  }
  for (const auto& item : adaptation["atpaVolumes"].toArray()) {
    const auto obj = item.toObject(),
               threshold = obj["runwayThreshold"].toObject();
    Volume v;
    v.key = code(obj["id"]);
    v.id = code(obj["volumeId"]);
    v.airport = code(obj["airportId"]);
    if (v.id.isEmpty()) v.id = v.key;
    if (v.id.isEmpty() || !threshold["lat"].isDouble() ||
        !threshold["lon"].isDouble())
      continue;
    v.threshold = {threshold["lon"].toDouble(), threshold["lat"].toDouble()};
    if (v.threshold.isNull() || std::abs(v.threshold.y()) > 90 ||
        std::abs(v.threshold.x()) > 180)
      continue;
    v.runway = normalizedRunway(
        v.id.startsWith(v.airport) ? v.id.mid(v.airport.size()) : v.id);
    v.heading = obj["magneticHeading"].toDouble();
    const auto radians = math::toRadians(
        v.heading + 180 - radar::magneticVariationAt(v.threshold));
    v.axis = {std::sin(radians), std::cos(radians)};
    v.scale = 60 * radar::longitudeScaleFactorForLat(v.threshold.y());
    v.length = obj["length"].toDouble(24);
    v.left = std::max(0., obj["widthLeft"].toDouble(1200)) / 6076.12;
    v.right = std::max(0., obj["widthRight"].toDouble(1200)) / 6076.12;
    v.floor = obj["floor"].toInt();
    v.ceiling = obj["ceiling"].toInt(10000);
    v.deviation = obj["maximumHeadingDeviation"].toDouble(90);
    v.reduced = obj["twoPointFiveApproachEnabled"].toBool()
                    ? obj["twoPointFiveApproachDistance"].toDouble()
                    : 0;
    for (const auto& value : obj["scratchpads"].toArray()) {
      const auto entry = value.toObject();
      if (!code(entry["entry"]).isEmpty())
        v.scratch.append({code(entry["entry"]),
                          secondEnum(entry["scratchPadNumber"], "TWO"),
                          secondEnum(entry["type"], "INELIGIBLE")});
    }
    for (const auto& id : obj["excludedTcpIds"].toArray()) {
      const auto owner = codes.value(code(id));
      if (!owner.isEmpty()) v.excluded.insert(owner);
    }
    for (const auto& value : obj["tcps"].toArray()) {
      const auto display = value.toObject();
      if (tcp.isEmpty() || codes.value(code(display["tcpId"])) != tcp) continue;
      v.alerts = true;
      v.monitor |= secondEnum(display["coneType"], "ALERTANDMONITOR");
    }
    volumes_.append(std::move(v));
  }
  std::stable_sort(volumes_.begin(), volumes_.end(),
                   [](const Volume& a, const Volume& b) {
                     return std::tie(a.airport, a.id, a.key) <
                            std::tie(b.airport, b.id, b.key);
                   });
  if (!volumes_.isEmpty()) {
    auto& site = sites[siteKey];
    if (!site) site = std::make_shared<Site>();
    site_ = site;
  }
}
bool Atpa::enabled() const { return site_ && site_->enabled; }
void Atpa::resetTracks() {
  pairs_.clear();
  overrides_.clear();
  revision_ = 0;
}

void Atpa::update(const QMap<QString, net::RadarTrack>& tracks, bool changed) {
  if (!changed && (!site_ || revision_ == site_->revision)) return;
  if (site_ &&
      (allReset_ != site_->allReset || alertReset_ != site_->alertReset)) {
    for (auto& o : overrides_) {
      o.alert.reset();
      if (allReset_ != site_->allReset) {
        o.distance.reset();
        o.monitor.reset();
      }
    }
    allReset_ = site_->allReset;
    alertReset_ = site_->alertReset;
  }
  overrides_.removeIf([&](auto it) {
    const auto t = tracks.constFind(it.key());
    return t == tracks.cend() || t->firstMrtMs != it.value().firstMrt;
  });
  revision_ = site_ ? site_->revision : 0;
  pairs_.clear();
  if (!enabled()) return;
  struct Member {
    const net::RadarTrack* track;
    QPointF position;
    double thresholdDistance;
    bool ineligible, synchronized = false;
  };
  QList<QList<Member>> groups(volumes_.size());
  for (const auto& target : tracks) {
    const auto& r = target.report;
    if (!r.plan || !r.location || !r.altitude || !r.velocity ||
        r.velocity->isNull())
      continue;
    const auto& p = *r.plan;
    if (p.deleted || p.suspended || p.vfr ||
        p.flightRules.contains("VFR ON TOP") || p.type == "P" ||
        p.type == "D" || p.type == "DEP" || p.type == "DEPARTURE")
      continue;
    const auto assigned = normalizedRunway(p.runway);
    const double heading =
        std::atan2(r.velocity->x(), r.velocity->y()) * 180 / std::numbers::pi +
        radar::magneticVariationAt(*r.location);
    int best = -1;
    double lateralBest = 0, distanceBest = 0;
    bool ineligible = false;
    for (int i = 0; i < volumes_.size(); ++i) {
      const auto& v = volumes_[i];
      if (site_->disabled.contains(v.id) || v.excluded.contains(target.owner) ||
          (!assigned.isEmpty() && assigned != v.runway))
        continue;
      if (*r.altitude < v.floor || *r.altitude > v.ceiling ||
          std::abs(std::remainder(heading - v.heading, 360.)) > v.deviation)
        continue;
      const auto c = v.coordinates(*r.location);
      if (c.x() < 0 || c.x() > v.length || c.y() < -v.left || c.y() > v.right)
        continue;
      bool excluded = false, filter = false;
      for (const auto& rule : v.scratch) {
        if ((rule.secondary ? p.secondary : p.primary) != rule.entry) continue;
        if (rule.ineligible)
          filter = true;
        else
          excluded = true;
      }
      if (excluded) continue;
      const auto lateral = std::abs(c.y()),
                 distance = radar::distanceNM(*r.location, v.threshold);
      if (best < 0 || lateral < lateralBest - 1e-6 ||
          (std::abs(lateral - lateralBest) <= 1e-6 &&
           distance < distanceBest)) {
        best = i;
        lateralBest = lateral;
        distanceBest = distance;
        ineligible = filter;
      }
    }
    if (best >= 0)
      groups[best].append({&target, *r.location, distanceBest, ineligible});
  }
  for (int i = 0; i < groups.size(); ++i) {
    auto& group = groups[i];
    const auto& v = volumes_[i];
    qint64 latest = 0;
    for (const auto& m : group)
      if (!m.track->report.frozen)
        latest = std::max(latest, m.track->report.mrtMs);
    for (auto& m : group) {
      const auto& r = m.track->report;
      const auto age = latest - r.mrtMs;
      if (r.frozen || r.mrtMs <= 0 || age < 0 || age > 30000) continue;
      const auto move = *r.velocity * (age / 3600000.);
      m.position = {math::normalizeLongitude(
                        m.position.x() +
                        move.x() / (60 * radar::longitudeScaleFactorForLat(
                                             m.position.y()))),
                    m.position.y() + move.y() / 60};
      m.synchronized = std::abs(m.position.y()) <= 90;
      if (m.synchronized)
        m.thresholdDistance = radar::distanceNM(m.position, v.threshold);
    }
    std::stable_sort(group.begin(), group.end(),
                     [](const Member& a, const Member& b) {
                       return a.thresholdDistance < b.thresholdDistance;
                     });
    for (int j = 0; j < group.size(); ++j) {
      const auto& m = group[j];
      const auto& r = m.track->report;
      auto& pair = pairs_[r.number];
      pair.volume = i;
      pair.ineligible = m.ineligible;
      if (j == 0 || m.ineligible || !m.synchronized ||
          !group[j - 1].synchronized)
        continue;
      const auto& lead = group[j - 1];
      const auto& front = lead.track->report;
      pair.lead = front.number;
      pair.distance = radar::distanceNM(m.position, lead.position);
      const auto c = v.coordinates(m.position);
      const bool reduced = v.reduced > 0 && !site_->reducedOff.contains(v.id) &&
                           c.x() < v.reduced && std::abs(c.y()) <= .2 &&
                           std::abs(v.coordinates(lead.position).y()) <= .2;
      pair.minimum =
          separation(front.plan->category, r.plan->category, reduced);
      if (pair.minimum > 0)
        pair.status = predicted(pair.distance, pair.minimum,
                                {v.offset(lead.position), *front.velocity},
                                {v.offset(m.position), *r.velocity});
    }
  }
}

AtpaDisplay Atpa::display(const net::RadarTrack& t,
                          const Preferences& prefs) const {
  const auto o = overrides_.value(t.report.number);
  AtpaDisplay d;
  d.mileage = o.mileage.value_or(prefs.tpaMileage);
  d.ring = o.ring;
  d.manualCone = o.cone;
  const auto it = pairs_.constFind(t.report.number);
  if (!enabled() || it == pairs_.cend() || it->ineligible) return d;
  const auto& v = volumes_.at(it->volume);
  const bool owned = !tcp_.isEmpty() && t.owner == tcp_;
  const bool monitor =
      (owned || v.monitor) && o.monitor.value_or(prefs.atpaMonitorCones);
  const bool warn = prefs.atpaAlertCones && o.alert.value_or(true) &&
                    (owned || v.alerts || o.alert.value_or(false));
  const bool distance = o.distance.value_or(prefs.atpaDistance);
  const bool conflict =
      it->status == AtpaStatus::Warning || it->status == AtpaStatus::Alert;
  if (conflict && warn)
    d.cone = it->status;
  else if (monitor &&
           (it->status == AtpaStatus::Monitor || (conflict && !distance)))
    d.cone = AtpaStatus::Monitor;
  if (d.manualCone > 0) {
    const bool actualDistance =
        distance && !(prefs.atpaAlertCones && o.alert == false);
    d.manualCone = (d.cone != AtpaStatus::None || (conflict && actualDistance))
                       ? 0
                       : std::max(d.manualCone, it->minimum);
  }
  if (prefs.atpaAlertCones && o.alert == false)
    d.field = "*TPA";
  else if (it->distance > 0 && (distance || (conflict && warn))) {
    d.field = it->minimum > 0 ? QString::number(it->distance, 'f', 2)
                              : QStringLiteral("NOWGT");
    d.fieldStatus = it->status;
  }
  return d;
}
QString Atpa::disabledVolumes() const {
  QStringList ids;
  if (enabled())
    for (const auto& v : volumes_)
      if (site_->disabled.contains(v.id)) ids.append(v.id);
  return ids.join(' ');
}
QString Atpa::statusLine(bool reduced) const {
  if (!enabled()) return {};
  QStringList ids;
  bool interested = false;
  for (const auto& v : volumes_) {
    if (!v.alerts) continue;
    interested = true;
    const bool off = site_->disabled.contains(v.id);
    if (reduced ? !off && v.reduced > 0 && !site_->reducedOff.contains(v.id)
                : off)
      ids.append(v.id);
  }
  if (ids.isEmpty())
    return interested && !reduced ? QStringLiteral("INTRAIL ON") : QString{};
  return chop((reduced ? QStringLiteral("INTRAIL 2.5 ON: ")
                       : QStringLiteral("INTRAIL OFF: ")) +
              ids.join(' '));
}
QString Atpa::supervisor(QString command) {
  const bool reduced = command.startsWith("2.5");
  command = command.mid(reduced ? 3 : 5).trimmed();
  if (command.isEmpty() || (!command.endsWith('E') && !command.endsWith('I')))
    return "FORMAT";
  const bool on = command.back() == 'E';
  command.chop(1);
  static const QRegularExpression id("^[A-Z0-9]{1,5}$");
  if ((reduced || !command.isEmpty()) && !id.match(command).hasMatch())
    return "FORMAT";
  if (!site_) return "ILL FNCT";
  if (command.isEmpty() && !reduced) {
    if (on == enabled()) return "NO CHANGE";
    site_->enabled = on;
    if (on) {
      site_->disabled.clear();
      site_->reducedOff.clear();
      ++site_->allReset;
    }
  } else {
    if (!enabled()) return "ILL FNCT";
    auto it = std::find_if(
        volumes_.cbegin(), volumes_.cend(),
        [&](const Volume& v) { return v.id == command || v.key == command; });
    if (it == volumes_.cend()) {
      const auto r = normalizedRunway(command);
      if (r.isEmpty()) return "ILL VOL";
      it = std::find_if(volumes_.cbegin(), volumes_.cend(),
                        [&](const Volume& v) { return v.runway == r; });
      if (it == volumes_.cend() ||
          std::any_of(it + 1, volumes_.cend(),
                      [&](const Volume& v) { return v.runway == r; }))
        return "ILL VOL";
    }
    if (reduced && it->reduced <= 0) return "ILL FNCT";
    if (reduced && site_->disabled.contains(it->id)) return "ILL VOL";
    auto& inhibited = reduced ? site_->reducedOff : site_->disabled;
    if (on != inhibited.contains(it->id)) return {};
    if (on)
      inhibited.remove(it->id);
    else
      inhibited.insert(it->id);
    if (!reduced && on) site_->reducedOff.remove(it->id);
    if (!reduced && !on) ++site_->alertReset;
  }
  ++site_->revision;
  return {};
}
void Atpa::setDisplay(QChar control, bool on, Preferences& p) {
  if (control == 'D') p.atpaDistance = on;
  if (control == 'A') p.atpaAlertCones = on;
  if (control == 'B') p.atpaMonitorCones = on;
  if (control == '+') p.tpaMileage = on;
  for (auto& o : overrides_) {
    if (control == 'A') o.alert.reset();
    if (control == 'B') o.monitor.reset();
    if (control == '+') o.mileage.reset();
  }
}
void Atpa::clearManual(QChar control) {
  for (auto& o : overrides_) {
    if (control == 'J')
      o.ring = 0;
    else
      o.cone = 0;
  }
}
QString Atpa::setTrackDisplay(const QString& command, const net::RadarTrack& t,
                              bool full, const Preferences& prefs) {
  const auto control = command.startsWith("*D+") ? QChar('+') : command.at(1);
  const bool on = command.endsWith('E');
  const auto it = pairs_.constFind(t.report.number);
  if (control == 'J' || control == 'P') {
    // TI 6191.409 6.21.2–9. Manual J-rings and cones share 50 slots per
    // workstation.
    const auto sizeText = command.mid(2);
    static const QRegularExpression size(
        R"(^(?:[1-9](?:\.[0-9])?|[12][0-9]|30)$)");
    if (!sizeText.isEmpty() && !size.match(sizeText).hasMatch())
      return "FORMAT";
    const double nm = sizeText.toDouble();
    const auto old = overrides_.value(t.report.number);
    if (!nm && !(control == 'J' ? old.ring : old.cone)) return "ILL FNCT";
    if (nm && !old.ring && !old.cone &&
        std::count_if(overrides_.cbegin(), overrides_.cend(),
                      [](const Override& o) { return o.ring || o.cone; }) >= 50)
      return "CAPACITY";
    auto& o = overrides_[t.report.number];
    o.firstMrt = t.firstMrtMs;
    o.ring = control == 'J' ? nm : 0;
    o.cone = control == 'P' ? nm : 0;
    return {};
  }
  if (control == '+') {
    const auto old = overrides_.value(t.report.number);
    if (!old.ring && !old.cone && (it == pairs_.cend() || !it->minimum))
      return "ILL FNCT";
  } else {
    if (!enabled() ||
        ((control == 'A' || control == 'B') && !prefs.atpaAlertCones))
      return "ILL FNCT";
    if (it == pairs_.cend() || it->ineligible) return "ILL TRK";
    if ((control == 'A' || control == 'D') && !full) return "ILL TRK";
    const bool owned = !tcp_.isEmpty() && t.owner == tcp_;
    if (control == 'B' &&
        (!it->minimum || (!owned && !volumes_[it->volume].monitor)))
      return "ILL TRK";
  }
  auto& o = overrides_[t.report.number];
  o.firstMrt = t.firstMrtMs;
  if (control == 'A') o.alert = on;
  if (control == 'B') o.monitor = on;
  if (control == 'D') o.distance = on;
  if (control == '+')
    o.mileage =
        command.endsWith('+') ? !o.mileage.value_or(prefs.tpaMileage) : on;
  return {};
}
}  // namespace stars
