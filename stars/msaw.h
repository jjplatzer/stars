#pragma once

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>
#include <optional>

namespace stars {
namespace net {
struct RadarTrack;
}
bool msawInitialClimb(const net::RadarTrack& track);

class MsawAirspace {
 public:
  static MsawAirspace load(const QString& facility);
  std::optional<int> minimumAt(QPointF position) const;

 private:
  struct Sector {
    int minimum;
    QRectF bounds;
    QVector<QVector<QPointF>> rings;
  };
  QVector<Sector> sectors_;
};

bool withinMsawAirportVolume(QPointF position, int altitude);
}  // namespace stars
