#include "stars.h"

namespace stars {
namespace {
QString beaconCode(const QString& raw) {
  const auto code = raw.trimmed();
  return code.size() == 4 && Preferences::validBeacon(code) ? code : QString{};
}

QString beaconReadout(const net::RadarTrack& track) {
  const auto& plan = *track.report.plan;
  const auto acid = plan.acid.trimmed().toUpper();
  if (acid.isEmpty()) return {};
  const auto received = beaconCode(track.report.beacon);
  const auto assigned = beaconCode(plan.assignedBeacon);
  QString result =
      acid + ' ' + (received.isEmpty() ? QString(5, ' ') : received);
  if (!assigned.isEmpty()) result += ' ' + assigned;
  return result;
}
}  // namespace

Datablock STARSPane::trackDatablock(const net::RadarTrack& track) const {
  const auto& report = track.report;
  SlewDisplay display;
  const auto event = alertSite_->lowAltitude.constFind(report.number);
  if (event != alertSite_->lowAltitude.cend() && event->matches(track)) {
    display.msawInhibit = event->disabled || event->suppressed;
    display.lowAltitude = msawAlert(track) != nullptr;
    display.lowAltitudeAcknowledged = event->acknowledged;
  }
  display.taisLeaderDirection = taisLeaderDirection_;
  display.dwelled = dwellTrack_ == report.number && !report.number.isEmpty();
  bool requested = requestedAltitude_;
  const auto selection = selections_.constFind(report.number);
  const auto ack = alertSite_->acknowledged.constFind(report.number);
  display.acknowledged =
      ack != alertSite_->acknowledged.cend() && ack->applies(track, trafficMs_);
  if (selection != selections_.cend() &&
      selection->firstMrt == track.firstMrtMs) {
    if (selection->acknowledged)
      display.acknowledged |=
          selection->acknowledged->applies(track, trafficMs_);
    display.beacon = selection->beacon;
    if (!selection->readout.hasExpired()) display.beacon = true;
    display.direction = selection->direction;
    display.quickLook = selection->quickLook;
    requested = selection->requestedAltitude.value_or(requested);
  }
  return datablock(track, tcp_, localTCPs_, prefs_, palette_, vfrSymbol_,
                   beaconReadout_, requested, trafficMs_,
                   atpa_.display(track, prefs_), display,
                   hideAssignedAltitudes_, subsetSymbols_);
}

void STARSPane::clearBeaconOverrides() {
  for (auto& selection : selections_) selection.beacon.reset();
}

void STARSPane::pruneSelections() {
  const auto& tracks = traffic_.tracks();
  for (int i = 0; i < 2 && !minPair_[0].isEmpty(); ++i) {
    const auto t = tracks.constFind(minPair_[i]);
    if (t == tracks.cend() || t->firstMrtMs != minGeneration_[i]) minPair_ = {};
  }
  for (auto it = selections_.begin(); it != selections_.end();) {
    const auto t = tracks.constFind(it.key());
    if (t == tracks.cend() || t->firstMrtMs != it->firstMrt ||
        t->report.address != it->address) {
      it = selections_.erase(it);
      continue;
    }
    if (it->associationSince != t->associationSinceMs) {
      it->direction.reset();
      it->singlePtl = false;
      it->beacon.reset();
      it->readout = QDeadlineTimer(0);
      it->quickLook = false;
      it->associationSince = t->associationSinceMs;
    }
    if (!t->report.plan || t->report.plan->sfpn != it->planId ||
        t->report.plan->acid != it->acid)
      it->requestedAltitude.reset();
    if (t->report.status != "active") it->singlePtl = false;
    if (it->acknowledged && (it->acknowledged->code != t->report.beacon ||
                             t->beaconSinceMs > it->acknowledged->at))
      it->acknowledged.reset();
    if (!it->direction && !it->requestedAltitude && !it->singlePtl &&
        !it->beacon && !it->quickLook && !it->acknowledged &&
        it->readout.hasExpired())
      it = selections_.erase(it);
    else
      ++it;
  }
  if (archiveAvailable_)
    alertSite_->acknowledged.removeIf([&](auto it) {
      if (trafficMs_ < it->at) return false;
      const auto track = tracks.constFind(it.key());
      return track == tracks.cend() || !it->applies(*track, trafficMs_);
    });
}

void STARSPane::selectTrack(const QString& key) {
  const auto track = traffic_.tracks().constFind(key);
  const bool explicitBeacon = mode_ == Mode::Multi;
  if (explicitBeacon && input_ != "B") {
    response_ = "FORMAT";
    return;
  }
  if (track == traffic_.tracks().cend()) {
    if (explicitBeacon) response_ = "NO TRK";
    return;
  }
  const auto& r = track->report;
  const auto& plan = r.plan;
  response_.clear();
  // The explicit command is repetitive and ignores ownership. A bare slew
  // first releases an accepted outbound handoff, then acknowledges alerts.
  if (explicitBeacon && plan) {
    response_ = beaconReadout(*track);
    if (response_.isEmpty()) response_ = "ILL TRK";
    return;
  }
  if (!explicitBeacon && traffic_.acknowledgeHandoff(key, tcp_)) return;
  bool acknowledgedLA = false;
  auto event = alertSite_->lowAltitude.find(key);
  if (!explicitBeacon && !track->pending && msawAlert(*track) &&
      !event->acknowledged) {
    event->acknowledged = true;
    acknowledgedLA = true;
  }
  auto& selection = selections_[key];
  selection.firstMrt = track->firstMrtMs;
  selection.address = r.address;
  selection.associationSince = track->associationSinceMs;
  if (explicitBeacon) {
    selection.beacon = !selection.beacon.value_or(prefs_.ldbBeaconCodes);
    return;
  }
  const bool owned = plan && !tcp_.isEmpty() && track->owner == tcp_;
  if (!track->pending && !specialCondition(r.beacon).isEmpty()) {
    // An already steady SPC leaves the click available for the implied readout.
    const auto shared = alertSite_->acknowledged.constFind(key);
    const bool sharedActive = shared != alertSite_->acknowledged.cend() &&
                              shared->applies(*track, trafficMs_);
    if (!selection.acknowledged && !sharedActive) {
      selection.acknowledged =
          Acknowledgement{r.beacon, r.address, track->firstMrtMs, trafficMs_};
      if (owned) alertSite_->acknowledged.insert(key, *selection.acknowledged);
      return;
    }
  }
  if (acknowledgedLA) return;
  if (!plan) {
    selection.readout.setRemainingTime(5000);
  } else if (owned && !plan->deleted && !plan->suspended && !r.frozen &&
             !track->pending) {
    const auto received = beaconCode(r.beacon),
               assigned = beaconCode(plan->assignedBeacon);
    if (received.isEmpty() || assigned.isEmpty() || received == assigned)
      response_ = beaconReadout(*track);
  } else if (!owned && !plan->suspended && !symbolTCP(track->owner).isEmpty() &&
             !(track->pending && plan->cps == tcp_)) {
    selection.quickLook = !selection.quickLook;
  }
}
}  // namespace stars
