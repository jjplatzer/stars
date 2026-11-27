#pragma once
#include <QFuture>
#include <QList>
#include <QMap>
#include <QPointF>
#include <QString>
#include <optional>

namespace stars::net {
struct FlightPlan {
  QString sfpn, acid, assignedBeacon, cps, ocr, direction, primary, secondary;
  QString runway, flightRules;
  QString aircraftType, rules, category, airport, exitFix, type;
  QString entryFix, origin, destination, gufi, equipmentSuffix;
  int assignedAltitude{}, requestedAltitude{};
  bool rnav{}, suspended{}, deleted{}, ifr{}, vfr{};
};
struct TrackReport {
  QString number, address, status, beacon;
  qint64 mrtMs{}, sourceMs{};
  std::optional<QPointF> location;
  std::optional<int> altitude;
  double speed{};
  bool newTrack{}, frozen{}, pseudo{};
  std::optional<QPointF> velocity;  // east/north knots
  std::optional<FlightPlan> plan;
};
QList<TrackReport> parseTrackReports(const QByteArray& body,
                                     const QString& facility);
struct PastPosition {
  qint64 time;
  QPointF point;
  std::optional<int> altitude{};
};
struct RadarTrack {
  TrackReport report;
  QList<PastPosition> history;
  QString owner, formerOwner;
  qint64 acceptedMs{}, firstMrtMs{}, beaconSinceMs{}, associationSinceMs{};
  bool pending{};
};
// A flight plan can be received before acquisition and outlive the radar
// return.
struct ListedPlan {
  FlightPlan plan;
  QString track, owner, formerOwner, beacon;
  bool pending = true, frozen = false, coasting = false;
  std::optional<int> altitude;
  qint64 firstSeen{}, vfrSince{}, suspendSince{}, lastMrt{}, sourceMs{};
};
struct Delivery;
class TaisCache;
class Traffic {
 public:
  ~Traffic();
  void reset();
  bool acknowledgeHandoff(const QString& number, const QString& tcp);
  bool advance(const TaisCache& cache, qint64 ms);
  bool receiving(qint64 ms) const {
    return lastReceipt_ && *lastReceipt_ <= ms &&
           ms - *lastReceipt_ < coastSeconds * 1000;
  }
  bool initialized() const { return cursor_.has_value(); }
  const QMap<QString, RadarTrack>& tracks() const { return tracks_; }
  const QMap<QString, ListedPlan>& flightPlans() const { return plans_; }
  static constexpr int coastSeconds = 30;
  static constexpr int ownershipLookbehindSeconds = 300;

 private:
  void apply(const TrackReport& report, qint64 receiptMs, bool history);
  void updatePlan(const TrackReport& report, qint64 receiptMs);
  QMap<QString, ListedPlan> plans_;
  QMap<QString, qint64> deletedPlans_;
  QMap<QString, QString> trackPlans_;
  QMap<QString, RadarTrack> tracks_;
  QMap<QString, qint64> dropped_;
  std::optional<qint64> cursor_, lastReceipt_;
  quint64 revision_{};
  struct Batch {
    qint64 receivedMs;
    QList<TrackReport> reports;
  };
  struct Pending {
    qint64 through;
    bool seed;
    QFuture<QList<Batch>> work;
  };
  std::optional<Pending> pending_;
};
}  // namespace stars::net
