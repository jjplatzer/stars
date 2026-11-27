#include "fusion.h"

#include <QtConcurrentRun>
#include <algorithm>

#include "net/archive.h"

namespace stars {
Fusion::~Fusion() {
  if (working_) pending_.cancel();
}
void Fusion::reset() {
  if (working_) pending_.cancel();
  ++generation_;
  requested_.clear();
  index_ = {};
  displayed_.clear();
  dirty_ = true;
  epoch_ = -1;
}
void Fusion::setMode(ReconstructionMode mode) {
  if (mode_ == mode) return;
  if (mode == ReconstructionMode::Original) reset();
  mode_ = mode;
  dirty_ = true;
}
const FusionTrack* Fusion::display(const QString& number) const {
  const auto it = displayed_.constFind(number);
  return it == displayed_.cend() ? nullptr : &it.value();
}

void Fusion::index(QPromise<Index>& work,
                   const net::TaisCache::Snapshots& snapshots,
                   QMap<qint64, Parsed> previous) {
  Index result;
  struct State {
    qint64 lastMrt{}, dropped{};
    QString address;
    bool continuous = false;
  };
  QMap<QString, State> states;
  for (auto window = snapshots.cbegin(); window != snapshots.cend(); ++window) {
    if (work.isCanceled()) return;
    if (!window.value() || window.value()->parts.isEmpty()) {
      // Retain the observation-time watermark so a late duplicate on the other
      // side of an archive gap cannot be mistaken for a newer generation.
      for (auto& state : states) state.continuous = false;
      continue;
    }
    auto parsed = previous.value(window.key());
    if (parsed.source != window.value()) {
      auto rows = std::make_shared<Measurements>();
      for (const auto& d : window.value()->events) {
        if (work.isCanceled()) return;
        if (!d.root.isEmpty() && d.root != "TATrackAndFlightPlan") continue;
        for (const auto& r : net::parseTrackReports(d.body, d.facility)) {
          if (r.number.isEmpty()) continue;
          rows->append(
              {r.number,
               r.address,
               {r.mrtMs, r.location.value_or(QPointF{}), r.velocity},
               d.receivedNs,
               r.location && !r.frozen && !r.status.startsWith("coast"),
               r.status == "drop",
               r.newTrack});
        }
      }
      parsed = {window.value(), rows};
    }
    result.windows.insert(window.key(), parsed);
    for (const auto& m : *parsed.measurements) {
      if (work.isCanceled()) return;
      auto& state = states[m.number];
      const auto time = m.anchor.timeMs, receipt = m.receiptNs / 1000000;
      if (time <= 0) {
        state.continuous = false;
        continue;
      }
      if (time <= state.dropped || time < state.lastMrt) continue;
      auto endRun = [&] {
        if (state.continuous) result.runs[m.number].last().discontinuity = time;
        state.continuous = false;
      };
      if (m.drop) {
        endRun();
        state = {};
        state.dropped = time;
        continue;
      }
      if (!m.usable || receipt - time > net::Traffic::coastSeconds * 1000) {
        endRun();
        continue;
      }
      auto& runs = result.runs[m.number];
      bool join = state.continuous && time > state.lastMrt &&
                  !net::startsGeneration(state.lastMrt, state.address, receipt,
                                         time, m.address, m.acquisition);
      if (join)
        join = net::TrajectorySegment::between(
                   runs.last().points.last()->anchor, m.anchor)
                   .has_value();
      if (!join) {
        endRun();
        runs.append(Run{});
      }
      runs.last().points.append(&m);
      state.lastMrt = time;
      state.address = m.address;
      state.continuous = true;
    }
  }
  work.addResult(std::move(result));
}

bool Fusion::update(const net::TaisCache::Snapshots& snapshots,
                    const QMap<QString, net::RadarTrack>& tracks,
                    qint64 playbackMs, int historyCount, bool trafficChanged) {
  if (working_ && pending_.isFinished()) {
    working_ = false;
    if (!pending_.isCanceled() && generation_ == jobGeneration_ &&
        snapshots == requested_) {
      index_ = pending_.takeResult();
      dirty_ = true;
    } else {
      requested_.clear();
    }
    pending_ = {};
  }
  if (mode_ == ReconstructionMode::Original) {
    const bool refresh = dirty_;
    dirty_ = false;
    return refresh;
  }
  if (snapshots != requested_) {
    if (!index_.runs.isEmpty()) {
      index_.runs.clear();
      dirty_ = true;
    }
    if (!working_) {
      requested_ = snapshots;
      jobGeneration_ = generation_;
      pending_ = QtConcurrent::run(&Fusion::index, snapshots, index_.windows);
      working_ = true;
    }
  }
  const int interval = mode_ == ReconstructionMode::Hz1 ? 1000 : 500;
  const auto epoch = playbackMs - playbackMs % interval;
  if (!trafficChanged && !dirty_ && epoch == epoch_ &&
      historyCount == historyCount_)
    return false;
  dirty_ = false;
  epoch_ = epoch;
  historyCount_ = historyCount;
  displayed_.clear();
  for (const auto& track : tracks) {
    const auto& r = track.report;
    if (!r.location || r.frozen || r.status.startsWith("coast")) continue;
    auto runs = index_.runs.constFind(r.number);
    if (runs == index_.runs.cend()) continue;
    // Bind the display segment to the exact observation already delivered to
    // Traffic. A future reuse of this number cannot select a newer run.
    const Run* match = nullptr;
    // Run end times are nondecreasing in accepted receipt order. At equal
    // timestamps more than one run may qualify; reject ambiguous matches.
    auto candidate = std::ranges::lower_bound(
        *runs, r.mrtMs, {},
        [](const Run& run) { return run.points.last()->anchor.timeMs; });
    for (; candidate != runs->end() &&
           candidate->points.first()->anchor.timeMs <= r.mrtMs;
         ++candidate) {
      const Run& run = *candidate;
      const auto& points = run.points;
      const auto it = std::ranges::lower_bound(
          points, r.mrtMs, {},
          [](const Measurement* m) { return m->anchor.timeMs; });
      if ((*it)->anchor.timeMs != r.mrtMs ||
          (*it)->anchor.position != *r.location ||
          (*it)->address != r.address ||
          (*it)->receiptNs > playbackMs * 1000000 ||
          (*it)->anchor.timeMs < track.firstMrtMs)
        continue;
      if (match) {
        match = nullptr;
        break;
      }
      match = &run;
    }
    if (!match || (match->discontinuity && epoch >= *match->discontinuity))
      continue;
    const auto sample = [&](qint64 when) -> std::optional<QPointF> {
      const auto& points = match->points;
      if (when < track.firstMrtMs || when > points.last()->anchor.timeMs)
        return {};
      const auto hi = std::ranges::lower_bound(
          points, when, {},
          [](const Measurement* m) { return m->anchor.timeMs; });
      if ((*hi)->anchor.timeMs == when) return (*hi)->anchor.position;
      if (hi == points.begin()) return {};
      return net::TrajectorySegment::between((*std::prev(hi))->anchor,
                                             (*hi)->anchor)
          ->at(when);
    };
    FusionTrack display{sample(epoch).value_or(*r.location), {}};
    for (int i = 1; i <= historyCount; ++i) {
      const auto history = sample(epoch - i * 4500);
      if (!history) break;
      display.history.append(*history);
    }
    displayed_.insert(r.number, display);
  }
  return true;
}
}  // namespace stars
