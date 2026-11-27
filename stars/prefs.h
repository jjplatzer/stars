#pragma once
#include <QJsonObject>
#include <QMap>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <array>
#include <functional>
#include <optional>

namespace stars {
// Operator-visible values from TI 6191.409, sections 4.9.1 and 4.10.
// Kept per pane; profile persistence is separate from display preferences.
struct BrightnessPreferences {
  int dcb = 60, backgroundContrast = 0, videoGroupA = 50, videoGroupB = 40;
  int fullDatablocks = 80, lists = 80, positions = 80, limitedDatablocks = 80;
  int otherTracks = 80, lines = 40, rangeRings = 20, compass = 40;
  int beaconSymbols = 55, primarySymbols = 80, history = 60, weather = 30,
      wxContrast = 30;
};
struct CharacterSizePreferences {
  int datablocks = 1, lists = 1, dcb = 1, tools = 1, positionSymbols = 0;
};
using SavePrefSets = std::function<void(const QJsonObject& previous,
                                        const QJsonObject& updated)>;
struct ListPreferences {
  QPointF position;
  int lines = 5;
  bool visible = false;
};
struct RestrictionDisplay {
  bool hidden = false, hideText = false, forceBlink = false, stopBlink = false;
};
struct Preferences {
  QMap<QString, RestrictionDisplay> restrictionDisplay;
  QMap<QString, ListPreferences> lists{
      {"S", {{.05, .10}, 5, true}},  {"TX", {{.85, .5}}},
      {"TM", {{.80, .75}, 5, true}}, {"TQ", {{.05, .55}}},
      {"T", {{.05, .35}, 5, true}},  {"TV", {{.05, .80}, 5, true}},
      {"TC", {{.80, .35}}},          {"TN", {{.05, .30}}},
      {"TRA", {{.80, .425}}},        {"TL", {{.80, .15}}},
      {"P1", {{.05, .50}}},          {"P2", {{.05, .20}}},
      {"P3", {{.05, .10}}}};
  bool allSsaFields = false;
  inline static const QSet<QString> defaultSsaFields{"TIME", "WX", "ALTSTG"};
  QSet<QString> ssaFields = defaultSsaFields;
  void resetSsaFilters() {
    allSsaFields = false;
    ssaFields = defaultSsaFields;
  }
  bool showsSsaField(const QString& field) const {
    return allSsaFields || ssaFields.contains(field);
  }
  int leaderDirection = 0, leaderLength = 1, historyCount = 5, audioVolume = 10;
  bool ldbBeaconCodes = true;
  bool tpaMileage = true, atpaDistance = true, atpaAlertCones = true,
       atpaMonitorCones = false;
  QStringList selectedBeacons;
  std::array<bool, 10> giFilter{true};
  // Individual owners override ALL when both are selected. True denotes QL+.
  QMap<QString, bool> quickLook;
  int dwell = 1;  // 0 inhibit, 1 enable, 2 lock (TI 4.15.5).
  bool ptlOwn = false, ptlAll = false;
  int ptlHalfMinutes = 2;
  QString ptlLength() const {
    return QString::number(ptlHalfMinutes / 2., 'f', 1);
  }
  static bool validBeacon(const QString& code) {
    return (code.size() == 2 || code.size() == 4) &&
           std::all_of(code.cbegin(), code.cend(),
                       [](QChar c) { return c >= '0' && c <= '7'; });
  }
  std::array<int, 2> associatedAltitude{100, 60000},
      unassociatedAltitude{100, 60000};
  int ringSpacing = 5;
  bool useUserRingCenter = false;
  std::optional<QPointF> userRingCenter;
  BrightnessPreferences brightness;
  CharacterSizePreferences charSize;
  std::array<bool, 6> displayWeatherLevel{true, true, true, true, true, true};
};
}  // namespace stars
