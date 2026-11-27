#include "playback.h"

#include <algorithm>

namespace stars::ui {
qint64 Playback::earliest(qint64 now) {
  return now - qint64(STARS_RETENTION_DAYS) * 86400;
}
qint64 Playback::latest(qint64 now) {
  return now - qint64(STARS_BUFFER_MINUTES) * 60;
}

qint64 Playback::currentMs(qint64 now) const {
  const auto base = positionMs_.value_or(latest(now) * 1000);
  if (!playing()) return base;
  return std::clamp(base + elapsed_.elapsed(), earliest(now) * 1000,
                    latest(now) * 1000 + 999);
}
qint64 Playback::position(qint64 now) const { return currentMs(now) / 1000; }
void Playback::select(qint64 time, qint64 now) {
  elapsed_.invalidate();
  positionMs_ = std::clamp(time, earliest(now), latest(now)) * 1000;
  loadDeadline_.setRemainingTime(60000, Qt::PreciseTimer);
}
void Playback::toggle(qint64 now) {
  if (!selected() || loading()) return;
  positionMs_ = std::clamp(currentMs(now), earliest(now) * 1000,
                           latest(now) * 1000 + 999);
  if (playing())
    elapsed_.invalidate();
  else
    elapsed_.start();
}
bool Playback::canSeek(int seconds, qint64 now) const {
  const auto destination = position(now) + seconds;
  return selected() && !loading() && destination >= earliest(now) &&
         destination <= latest(now);
}
void Playback::seek(int seconds, qint64 now) {
  const auto destination = currentMs(now) + seconds * 1000;
  if (!selected() || loading() || destination / 1000 < earliest(now) ||
      destination / 1000 > latest(now))
    return;
  positionMs_ = destination;
  if (playing()) elapsed_.start();
}
}  // namespace stars::ui
