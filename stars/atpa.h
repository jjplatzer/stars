#pragma once
#include <QJsonArray>
#include <memory>

#include "net/track.h"
#include "prefs.h"

namespace stars {
QString normalizedRunway(QString text);
enum class AtpaStatus { None, Monitor, Warning, Alert };
struct AtpaPair {
  int volume{};
  bool ineligible{};
  QString lead;
  double distance{}, minimum{};
  AtpaStatus status{};
};
struct AtpaDisplay {
  QString field;
  AtpaStatus fieldStatus{}, cone{};
  bool mileage{};
  double ring{}, manualCone{};
};
class Atpa {
 public:
  void configure(const QJsonObject& adaptation, const QString& siteKey,
                 const QString& tcp);
  bool enabled() const;
  bool adapted() const { return !volumes_.isEmpty(); }
  void update(const QMap<QString, net::RadarTrack>& tracks, bool changed);
  void resetTracks();
  AtpaDisplay display(const net::RadarTrack& track,
                      const Preferences& prefs) const;
  const QMap<QString, AtpaPair>& pairs() const { return pairs_; }
  QString statusLine(bool reduced) const;
  QString disabledVolumes() const;
  bool hasGraphics() const {
    return !pairs_.isEmpty() || !overrides_.isEmpty();
  }
  void clearManual(QChar control);
  QString supervisor(QString command);
  void setDisplay(QChar control, bool enabled, Preferences& prefs);
  QString setTrackDisplay(const QString& command, const net::RadarTrack& track,
                          bool full, const Preferences& prefs);

 private:
  struct Scratch {
    QString entry;
    bool secondary, ineligible;
  };
  struct Volume {
    QString id, key, airport, runway;
    QPointF threshold, axis;
    double scale{}, heading{}, deviation{}, length{}, left{}, right{},
        reduced{};
    int floor{}, ceiling{};
    QList<Scratch> scratch;
    QSet<QString> excluded;
    bool alerts{}, monitor{};
    QPointF offset(QPointF p) const;
    QPointF coordinates(QPointF p) const;
  };
  struct Site {
    bool enabled = true;
    QSet<QString> disabled, reducedOff;
    quint64 revision = 1, allReset{}, alertReset{};
  };
  struct Override {
    std::optional<bool> mileage, distance, alert, monitor;
    qint64 firstMrt{};
    double ring{}, cone{};
  };
  QString tcp_;
  QList<Volume> volumes_;
  std::shared_ptr<Site> site_;
  quint64 revision_{}, allReset_{}, alertReset_{};
  QMap<QString, AtpaPair> pairs_;
  QMap<QString, Override> overrides_;
};
}  // namespace stars
