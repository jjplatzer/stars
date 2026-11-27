#include "datablock.h"

#include <cmath>

namespace stars {
QString specialCondition(const QString& beacon) {
  static const QMap<QString, QString> codes{{"7400", "LL"},
                                            {"7500", "HJ"},
                                            {"7600", "RF"},
                                            {"7700", "EM"},
                                            {"7777", "MI"}};
  return codes.value(beacon);
}
namespace {
QString digits(int n, int width) {
  return QString::number(n).rightJustified(width, '0');
}
}  // namespace
QString symbolTCP(const QString& s) {
  if (s.isEmpty() || s == "UNKNOWN" || s == "UNASSIGNED" ||
      s == "UNAVAILABLE" || s == "NONE" || s == "PENDING")
    return {};
  return s.back().isLetterOrNumber() ? s.right(1) : QString{};
}
Datablock datablock(const net::RadarTrack& t, const QString& tcp,
                    const QSet<QString>& localTCPs, const Preferences& prefs,
                    const colors::Palette& palette, bool vfrSymbol,
                    bool beaconReadout, bool requestedAltitude, qint64 ms,
                    AtpaDisplay atpa, SlewDisplay slew,
                    bool hideAssignedAltitudes,
                    const QStringList& subsetSymbols) {
  const auto& r = t.report;
  const auto& p = r.plan;
  const bool owned = p && !tcp.isEmpty() && t.owner == tcp;
  const bool pending = p && p->ocr == "PENDING";
  const bool inbound = pending && !tcp.isEmpty() && p->cps == tcp;
  const bool accepted = p && !tcp.isEmpty() && t.formerOwner == tcp;
  const bool attention = owned || inbound || accepted;
  const auto spc = specialCondition(r.beacon);
  const bool quick = p && !attention &&
                     (slew.quickLook || prefs.quickLook.contains(t.owner) ||
                      prefs.quickLook.contains("ALL"));
  const bool plus =
      quick && prefs.quickLook.value(t.owner, prefs.quickLook.value("ALL"));
  const bool full =
      p && (attention || quick || !spc.isEmpty() || beaconReadout);
  const auto& br = prefs.brightness;
  const bool dim =
      (inbound || (accepted && ms < t.acceptedMs + 5000)) && ms / 500 % 2 == 0;
  const int level = slew.dwelled ? 100
                                 : (attention ? br.fullDatablocks
                                    : full    ? br.otherTracks
                                              : br.limitedDatablocks) /
                                       (dim ? 2 : 1);
  const auto color =
      (attention || plus) ? palette.OwnedDatablock : palette.UnownedDatablock;
  Datablock d;
  d.full = full;
  const int reported =
      p ? int(leaderDirections.indexOf(p->direction.trimmed().toUpper())) : -1;
  d.direction = owned ? prefs.leaderDirection : 0;
  if (reported >= 0 && (!owned || slew.taisLeaderDirection))
    d.direction = reported;
  if (p && slew.direction) d.direction = *slew.direction;
  d.color = colors::brightness(color, level);
  const auto owner = t.owner.isEmpty() && p ? p->cps : t.owner;
  d.symbol = symbolTCP(owner);
  if (!d.symbol.isEmpty() && subsetSymbols.contains(owner))
    d.symbol = owner.right(2);
  if (d.symbol.isEmpty()) {
    if (vfrSymbol && r.beacon == "1200")
      d.symbol = "V";
    else if (r.beacon.size() == 4 &&
             (prefs.selectedBeacons.contains(r.beacon) ||
              prefs.selectedBeacons.contains(r.beacon.left(2))))
      d.symbol = QChar(129);
    else
      d.symbol = "*";
  }
  const int symbolLevel =
      attention && !slew.dwelled ? br.positions / (dim ? 2 : 1) : level;
  d.symbolColor =
      colors::brightness((attention || plus) ? palette.PositionSymbolOwned
                                             : palette.UnownedDatablock,
                         symbolLevel);
  d.showSymbol = symbolLevel > 0;
  const int leaderLevel = slew.dwelled ? 100
                                       : (attention ? br.fullDatablocks
                                          : (quick || (beaconReadout && p))
                                              ? br.otherTracks
                                              : br.limitedDatablocks) /
                                             (dim ? 2 : 1);
  d.leaderColor = colors::brightness(color, leaderLevel);
  d.showLeader = leaderLevel > 0;
  const auto& limits =
      p ? prefs.associatedAltitude : prefs.unassociatedAltitude;
  d.visible = (!p || !p->suspended || beaconReadout) &&
              (full || beaconReadout || !spc.isEmpty() || !r.altitude ||
               (*r.altitude >= limits[0] && *r.altitude <= limits[1]));
  if (!d.visible || !level) return d;

  const auto altitude =
      r.status.startsWith("coast") ? QString("CST")
      : !r.altitude                ? QString("   ")
      : *r.altitude < 0
          ? "N" + digits(int(std::round(-double(*r.altitude) / 100)), 2)
          : digits(int(std::round(*r.altitude / 100.)), 3);
  const auto speed = digits(int(std::round(r.speed / 10)), 2);
  const auto phaseMs = ms % 6000;
  const int phase = phaseMs < 2000 || (phaseMs >= 3000 && phaseMs < 5000) ? 1
                    : phaseMs < 3000                                      ? 2
                                                                          : 3;
  if (!spc.isEmpty() && (slew.acknowledged || ms / 500 % 2 != 0)) {
    d.alertRow = p ? 1 : 0;
    d.alertColor = colors::brightness(
        spc == "MI" ? palette.CautionDatablock : palette.AlertDatablock, level);
    d.rows[d.alertRow] = spc;
  }
  if (slew.lowAltitude) {
    if (!spc.isEmpty()) {
      d.alertSuffix = "/" + d.rows[1].leftJustified(spc.size());
      d.alertSuffixColor = colors::brightness(
          spc == "MI" ? palette.CautionDatablock : palette.AlertDatablock,
          level);
    }
    d.alertRow = 1;
    d.alertColor = colors::brightness(palette.AlertDatablock, level);
    d.rows[1] =
        (slew.lowAltitudeAcknowledged || ms / 500 % 2 != 0) ? "LA" : "  ";
  }
  if (!p) {
    if (beaconReadout || slew.beacon.value_or(prefs.ldbBeaconCodes) ||
        !spc.isEmpty())
      d.rows[1] = r.beacon;
    d.rows[2] = altitude + " " + speed;
    return d;
  }
  QString primary = p->primary;
  if (primary.isEmpty() &&
      (p->type == "P" || p->type == "D" || p->type.startsWith("DEP")) &&
      p->exitFix != "UNASSIGNED")
    primary = p->exitFix.section('.', 0, 0).left(3);
  QString left = altitude;
  if (phase != 1 && !primary.isEmpty()) left = primary;
  if (!full) {
    d.rows[2] = left.leftJustified(3, ' ', true) + " " + speed;
    return d;
  }
  d.rows[2] = beaconReadout && !r.beacon.isEmpty() ? r.beacon
              : !p->acid.isEmpty()                 ? p->acid
              : !p->assignedBeacon.isEmpty()       ? p->assignedBeacon
                                                   : r.beacon;
  if (!beaconReadout && !p->acid.isEmpty() &&
      (slew.msawInhibit || (!p->ifr && (p->vfr || r.beacon == "1200"))))
    d.rows[2] += "*";
  QString handoff = " ";
  if (pending && localTCPs.contains(t.owner) && localTCPs.contains(p->cps) &&
      t.owner != p->cps)
    handoff = p->cps.right(1);
  if (accepted && ms < t.acceptedMs + 5000 && localTCPs.contains(t.owner))
    handoff = t.owner.right(1);
  if ((phase == 3 || (phase == 2 && primary.isEmpty())) &&
      !p->secondary.isEmpty()) {
    left = p->secondary;
    handoff = "+";
  }
  QString right =
      speed + p->rules.leftJustified(1) + p->category.left(1).leftJustified(1);
  if (phase != 1)
    right =
        p->aircraftType + (p->rnav && !p->aircraftType.isEmpty() ? "^" : "");
  if (phase == 3 && requestedAltitude && p->requestedAltitude)
    right = "R" + digits(p->requestedAltitude / 100, 3);
  d.rows[3] =
      left.leftJustified(3, ' ', true) + handoff + right.leftJustified(5);
  d.rows[4] = atpa.field.left(5);
  d.fieldLength = d.rows[4].size();
  d.fieldColor = atpa.fieldStatus == AtpaStatus::Warning
                     ? colors::brightness(palette.ATPAWarning, level)
                 : atpa.fieldStatus == AtpaStatus::Alert
                     ? colors::brightness(palette.ATPAAlert, level)
                     : d.color;
  if (p->assignedAltitude && !hideAssignedAltitudes)
    d.rows[4] = d.rows[4].leftJustified(d.direction >= 4 ? 5 : 6) + "A" +
                digits(p->assignedAltitude / 100, 3);
  return d;
}
}  // namespace stars
