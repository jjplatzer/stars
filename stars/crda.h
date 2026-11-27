#pragma once
#include <QJsonArray>
#include <QRectF>
#include <array>

#include "net/track.h"

namespace stars {
class Crda {
 public:
  struct Runway {
    QString id;
    QPointF target, image;
    double heading{}, imageHeading{}, targetLength{}, imageLength{},
        nearDistance{}, length{}, nearWidth{}, farWidth{};
    double tieOffset{}, headingTolerance{}, descentDistance{},
        descentAltitude{}, targetAltitude{}, above{}, below{};
    QStringList primary, secondary;
    int direction = -1;
  };
  struct Control {
    bool enabled = false, boundary = false, lines = false;
    int direction = -1;
  };
  struct Pair {
    QString airport, tie, stagger;
    std::array<Runway, 2> runway;
    std::array<Control, 2> control{};
    QChar mode = 'D';
  };
  struct Ghost {
    QPointF position;
    int pair, side;
    QString symbol;
  };
  struct Selection {
    int pair = -1;
    qint64 generation{};
    QString plan, acid;
    bool force = false, qualify = false, suppressed = false, full = true;
  };
  void configure(const QJsonArray& adaptation);
  QString command(const QString& entry, QString& output);
  std::optional<Ghost> ghost(const net::RadarTrack& target, double variation,
                             const Selection& selection,
                             std::optional<QPointF> displayPosition = {}) const;
  bool enabled() const;
  QList<Pair> pairs;
  bool inhibited = false;
  static std::array<QPointF, 4> boundary(const Runway& r, double variation);
  static QPointF offset(QPointF origin, double nm, double heading, double scale,
                        double variation);
  static double scaleAt(double lat);

 private:
  bool forceAll_ = false;
};
}  // namespace stars
