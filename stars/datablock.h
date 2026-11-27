#pragma once
#include "atpa.h"
#include "colors.h"
#include "net/track.h"
#include "prefs.h"

namespace stars {
inline const QStringList leaderDirections{"N", "NE", "E", "SE",
                                          "S", "SW", "W", "NW"};
QString symbolTCP(const QString& tcp);
QString specialCondition(const QString& beacon);
struct SlewDisplay {
  std::optional<bool> beacon;
  bool quickLook = false, acknowledged = false, dwelled = false;
  bool lowAltitude = false, lowAltitudeAcknowledged = false,
       msawInhibit = false;
  bool taisLeaderDirection = false;
  std::optional<int> direction;
};
struct Datablock {
  QString symbol, alertSuffix;
  colors::RGB alertSuffixColor;
  colors::RGB color, symbolColor, alertColor, leaderColor;
  // Relative to the leader-aligned row. The limited block can extend two rows
  // above it.
  std::array<QString, 5> rows;
  int alertRow = -1, direction{};
  bool visible{}, showSymbol{}, showLeader{}, full{};
  int fieldLength{};
  colors::RGB fieldColor;
};
Datablock datablock(const net::RadarTrack& t, const QString& tcp,
                    const QSet<QString>& localTCPs, const Preferences& prefs,
                    const colors::Palette& palette, bool vfrSymbol,
                    bool beaconReadout, bool requestedAltitude, qint64 ms,
                    AtpaDisplay atpa = {}, SlewDisplay slew = {},
                    bool hideAssignedAltitudes = false,
                    const QStringList& subsetSymbols = {});
}  // namespace stars
