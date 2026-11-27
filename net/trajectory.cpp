#include "trajectory.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace stars::net {
namespace {
constexpr double kMetresPerDegree = 1852.0 * 60;
constexpr double kKnotToMetresPerSecond = 1852.0 / 3600;
constexpr double kMaxSpeed = 800 * kKnotToMetresPerSecond;
constexpr qint64 kMaxGapMs = 20000;

bool validPosition(QPointF p) {
  return std::abs(p.x()) <= 180 && std::abs(p.y()) <= 90;
}
double length(QPointF p) { return std::hypot(p.x(), p.y()); }
}  // namespace

std::optional<TrajectorySegment> TrajectorySegment::between(
    const TrajectoryAnchor& first, const TrajectoryAnchor& last) {
  if (first.timeMs < 0 || last.timeMs <= first.timeMs ||
      last.timeMs - first.timeMs > kMaxGapMs ||
      !validPosition(first.position) || !validPosition(last.position))
    return {};

  TrajectorySegment segment;
  segment.startMs_ = first.timeMs;
  segment.endMs_ = last.timeMs;
  segment.start_ = first.position;
  segment.end_ = last.position;
  const double latitude = (first.position.y() + last.position.y()) / 2;
  segment.eastScale_ =
      kMetresPerDegree * std::cos(latitude * std::numbers::pi / 180);
  if (segment.eastScale_ < 1e-4) return {};
  segment.chord_ = {
      std::remainder(last.position.x() - first.position.x(), 360.) *
          segment.eastScale_,
      (last.position.y() - first.position.y()) * kMetresPerDegree};
  const double seconds = (last.timeMs - first.timeMs) / 1000.;
  const double distance = length(segment.chord_);
  if (distance > seconds * kMaxSpeed) return {};
  if (!first.velocity || !last.velocity) return segment;
  const auto start = *first.velocity * kKnotToMetresPerSecond;
  const auto end = *last.velocity * kKnotToMetresPerSecond;
  if (!(length(start) <= kMaxSpeed && length(end) <= kMaxSpeed)) return segment;

  // In Bezier form the two inner control points of (Hermite - linear)
  // are (T*v0-chord)/3 and (chord-T*v1)/3. Their norms bound the whole curve.
  const auto a = start * seconds, b = end * seconds;
  if (std::abs(first.position.y() + a.y() / (3 * kMetresPerDegree)) > 90 ||
      std::abs(last.position.y() - b.y() / (3 * kMetresPerDegree)) > 90)
    return segment;
  const double envelope = std::max(50., distance * .5);
  if (length(a - segment.chord_) > 3 * envelope ||
      length(b - segment.chord_) > 3 * envelope)
    return segment;
  // Ordered control points along the chord preclude backtracking and loops.
  const auto forwardA = QPointF::dotProduct(a, segment.chord_);
  const auto forwardB = QPointF::dotProduct(b, segment.chord_);
  if (distance < 1 || forwardA < 0 || forwardB < 0 ||
      forwardA + forwardB > 3 * distance * distance)
    return segment;
  segment.startTangent_ = a;
  segment.endTangent_ = b;
  segment.method_ = TrajectoryMethod::Hermite;
  return segment;
}

std::optional<QPointF> TrajectorySegment::at(qint64 timeMs) const {
  if (timeMs < startMs_ || timeMs > endMs_) return {};
  if (timeMs == startMs_) return start_;
  if (timeMs == endMs_) return end_;
  const double u = double(timeMs - startMs_) / (endMs_ - startMs_), v = 1 - u;
  QPointF offset = chord_ * u;
  if (method_ == TrajectoryMethod::Hermite)
    offset = chord_ * (u * u * (3 - 2 * u)) + startTangent_ * (u * v * v) -
             endTangent_ * (u * u * v);
  return QPointF(std::remainder(start_.x() + offset.x() / eastScale_, 360.),
                 start_.y() + offset.y() / kMetresPerDegree);
}
}  // namespace stars::net
