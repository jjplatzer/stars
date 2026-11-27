#pragma once

#include <QPointF>
#include <QtGlobal>
#include <optional>

namespace stars::net {

// Anchors contain measured kinematics only. Time is TAIS MRT, never receipt
// time.
struct TrajectoryAnchor {
  qint64 timeMs;
  QPointF position;
  std::optional<QPointF> velocity;  // East/north knots.
};

enum class TrajectoryMethod { Linear, Hermite };

class TrajectorySegment {
 public:
  // The caller must establish lifecycle continuity before joining two anchors.
  static std::optional<TrajectorySegment> between(const TrajectoryAnchor& first,
                                                  const TrajectoryAnchor& last);
  std::optional<QPointF> at(qint64 timeMs) const;
  TrajectoryMethod method() const { return method_; }

 private:
  TrajectorySegment() = default;
  qint64 startMs_{}, endMs_{};
  QPointF start_, end_, chord_, startTangent_, endTangent_;
  double eastScale_{};
  TrajectoryMethod method_ = TrajectoryMethod::Linear;
};
}  // namespace stars::net
