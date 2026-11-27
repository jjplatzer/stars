#include <QDateTime>

#include "stars.h"

namespace stars {
void STARSPane::drawPreviewArea(renderer::CmdBuffer& cb, const Fonts& fonts,
                                int width, int height) const {
  const auto text = preview();
  if (text.isEmpty()) return;
  const int size = prefs_.charSize.datablocks;
  renderer::TextBuilder builder;
  builder.addText(fonts.sizes.at(size), text.toStdString(),
                  {width * .05f, height * .25f},
                  {colors::brightness(palette_.PreviewList,
                                      prefs_.brightness.fullDatablocks)
                       .rgba(),
                   {}});
  builder.generateCommands(cb, fonts.textures.at(size));
}

namespace {
// Routine METARs are hourly. Allow 15 minutes for a late observation before
// marking it stale, then the manual's ten-minute missing interval (§4.9.19).
constexpr int staleAge = 75 * 60, missingAge = staleAge + 10 * 60;
QString pressure(int hundredths) {
  return QStringLiteral("%1.%2")
      .arg(hundredths / 100)
      .arg(hundredths % 100, 2, 10, QChar('0'));
}
}  // namespace
QStringList STARSPane::altimeterStations() const {
  QStringList ids;
  for (const auto& airport : ssaAirports_) ids.append(airport.id);
  if (!primaryAirport_.isEmpty() && !ids.contains(primaryAirport_))
    ids.append(primaryAirport_);
  return ids;
}
QStringList STARSPane::facilityAltimeterStations() const {
  QStringList ids;
  for (const auto& station : facilityStations_) ids.append(station.id);
  if (!primaryAirport_.isEmpty() && !ids.contains(primaryAirport_))
    ids.append(primaryAirport_);
  return ids;
}
std::optional<STARSPane::StationSetting> STARSPane::stationSetting(
    const QString& id) const {
  const auto entered = enteredAltimeters_.constFind(id);
  int value;
  qint64 observed;
  if (entered != enteredAltimeters_.cend()) {
    value = entered->hundredths;
    if (entered->mode != 'A')
      return StationSetting{value, entered->mode, false};
    observed = entered->enteredAt;
  } else {
    const auto report = altimeters_.constFind(id);
    if (report == altimeters_.cend()) return std::nullopt;
    value = report->hundredths;
    observed = report->observed;
  }
  // An archive with no prior reading does not establish a missing automatic
  // station.
  if (!value) return StationSetting{0, ' ', false};
  const auto age = altimeterSecond_ - observed;
  return StationSetting{value, age >= staleAge ? 'M' : 'A', age >= missingAge};
}
QString STARSPane::systemSetting() const {
  const auto station = stationSetting(primaryAirport_);
  return station && station->hundredths && !station->missing
             ? pressure(station->hundredths)
             : QString{};
}
QList<STARSPane::AirportSetting> STARSPane::airportSettings() const {
  QList<AirportSetting> entries;
  const bool visible = prefs_.showsSsaField("AIRPORT");
  for (const auto& airport : ssaAirports_) {
    const auto station = stationSetting(airport.id);
    if (!station || (!visible && !station->missing)) continue;
    const auto setting = station->hundredths
                             ? pressure(station->hundredths) + station->status
                             : QString(6, ' ');
    entries.append({airport.label + ' ' + setting, station->missing});
  }
  return entries;
}

QStringList STARSPane::giLines() const {
  QStringList result;
  for (size_t i = 0; i < gi_.size(); ++i) {
    const auto& entry = gi_[i];
    if (!prefs_.giFilter[i] || (entry.code.isEmpty() && entry.text.isEmpty()))
      continue;
    QString line = entry.code;
    if (!line.isEmpty() && !entry.text.isEmpty()) line += ' ';
    line += entry.text;
    if (i && !dcb_.giAirports()[i].isEmpty())
      line = dcb_.giAirports()[i] + ':' + line;
    result.append(line.size() > 32 ? line.left(31) + '+' : line);
  }
  return result;
}
QStringList STARSPane::quickLookLines() const {
  QStringList entries, lines;
  const auto& owners = prefs_.quickLook;
  if (owners.contains("ALL")) entries.append(owners["ALL"] ? "ALL+" : "ALL");
  for (bool plus : {true, false})
    for (auto it = owners.cbegin(); it != owners.cend(); ++it)
      if (it.key() != "ALL" && it.value() == plus)
        entries.append(it.key() + (plus ? "+" : ""));
  for (const auto& entry : entries) {
    if (lines.isEmpty() || lines.back().size() + 1 + entry.size() > 32) {
      if (lines.size() == 2) {
        while (lines.back().size() > 30)
          lines.back().truncate(lines.back().lastIndexOf(' '));
        lines.back() += " +";
        break;
      }
      lines.append(entry);
    } else
      lines.back() += ' ' + entry;
  }
  return lines;
}
void STARSPane::drawSSA(renderer::CmdBuffer& cb, const Fonts& fonts, int width,
                        int height) const {
  using namespace renderer;
  // REDS' default SSA anchor and fixed 10px check box (independent of LISTS
  // character size). Table 2-15 field C remains visible with healthy sensors.
  const auto anchor = prefs_.lists.value("S").position;
  const float x = width * anchor.x() + 5, y = height * anchor.y();
  const auto textColor =
      colors::brightness(palette_.List, prefs_.brightness.lists);
  const auto alertColor =
      colors::brightness(palette_.TextAlert, prefs_.brightness.lists);
  cb.loadProjectionMatrix(screenOrtho(float(width), float(height)));
  cb.setRGB(textColor);
  cb.lineWidth(1);
  LinesBuilder box;
  const std::array<PointVertex, 4> corners = {
      {{x - 5, y - 5}, {x + 5, y - 5}, {x + 5, y + 5}, {x - 5, y + 5}}};
  box.addLineLoop(corners);
  box.generateCommands(cb);
  ColoredTrianglesBuilder triangle;
  const auto tier = [&](float width, float top, float bottom) {
    triangle.addQuad({x - width / 2, y - 3.5f + top},
                     {x + width / 2, y - 3.5f + top},
                     {x + width / 2, y - 3.5f + bottom},
                     {x - width / 2, y - 3.5f + bottom}, alertColor);
  };
  tier(7, 0, 2);
  tier(5, 2, 4);
  tier(3, 4, 6);
  tier(1, 6, 7);
  triangle.generateCommands(cb);
  const auto size = prefs_.charSize.lists;
  const auto& font = fonts.sizes.at(size);
  TextBuilder text;
  float rowY = y + 10;
  const bool blinkOn = (QDateTime::currentMSecsSinceEpoch() / 500) % 2;
  for (const auto& row : ssaRows()) {
    float column = x - 5;
    for (const auto& field : row) {
      if (!field.blink || blinkOn)
        text.addText(font, field.text.toStdString(), {column, rowY},
                     {field.color.rgba(), {}});
      column += field.text.size() * font.charSize().x;
    }
    rowY += font.lineHeight;
  }
  text.generateCommands(cb, fonts.textures.at(size));
}
QList<STARSPane::SsaRow> STARSPane::ssaRows() const {
  QList<SsaRow> rows;
  const auto textColor =
      colors::brightness(palette_.List, prefs_.brightness.lists);
  const auto alertColor =
      colors::brightness(palette_.TextAlert, prefs_.brightness.lists);
  const auto row = [&](const QString& text, renderer::RGB color) {
    rows.append(SsaRow{{text, color}});
  };
  if (prefs_.showsSsaField("WX")) {
    QString levels;
    for (int level = 1; level <= 6; ++level) {
      if (!weatherAvailable(level)) continue;
      const int column = (level - 1) * 3;
      levels = levels.leftJustified(column, ' ');
      levels += (prefs_.displayWeatherLevel[level - 1] ? QStringLiteral("(%1)")
                                                       : QStringLiteral(" %1 "))
                    .arg(level);
    }
    // Each level owns three columns. The WX row also owns its line when all six
    // are blank.
    row(levels,
        colors::brightness(palette_.SystemStatusWX, prefs_.brightness.lists));
  }
  const bool showTime = prefs_.showsSsaField("TIME");
  QString fieldE = showTime ? recordingTime_ : QString{};
  if (prefs_.showsSsaField("ALTSTG")) {
    const auto setting = systemSetting();
    if (!fieldE.isEmpty() && !setting.isEmpty()) fieldE += ' ';
    fieldE += setting;
  }
  if (showTime || !fieldE.isEmpty()) row(fieldE, textColor);
  for (const auto& line : giLines()) row(line, textColor);
  SsaRow system;
  if (prefs_.showsSsaField("STATUS")) {
    // REDS uses a simulator-source proxy for FSL/EFSL. In playback only
    // the current window and reports already delivered at the playhead count.
    const auto status = !archiveAvailable_               ? "NA/NA/NA"
                        : traffic_.receiving(trafficMs_) ? "OK/OK/NA"
                                                         : "NR/NR/NA";
    system.append({status, archiveAvailable_ ? textColor : alertColor});
  }
  if (prefs_.showsSsaField("RADAR"))
    system.append({system.isEmpty() ? "FUSED" : " FUSED", textColor});
  if (!system.isEmpty()) rows.append(system);
  if (prefs_.showsSsaField("CODES"))
    for (int i = 0; i < prefs_.selectedBeacons.size(); i += 5)
      row(prefs_.selectedBeacons.mid(i, 5).join(' '), textColor);
  const auto warningColor =
      colors::brightness(palette_.TextWarning, prefs_.brightness.lists);
  if (prefs_.showsSsaField("SPC")) {
    SsaRow codes;
    for (const QString code : {"HJ", "RF", "EM", "LL", "MI"})
      if (activeConditions_.contains(code))
        codes.append({codes.isEmpty() ? code : ' ' + code,
                      code == "MI" ? warningColor : alertColor});
    if (!codes.isEmpty()) rows.append(codes);
  }
  if (prefs_.showsSsaField("SYS OFF")) {
    QStringList off;
    for (const QString name : {"APP", "CA", "MCI", "MSAW"})
      if (alertSite_->inhibited.contains(name)) off.append(name);
    if (crda_.inhibited) off.append("CRDA");
    if (atpa_.adapted() && !atpa_.enabled()) off.append("INTRAIL");
    if (!off.isEmpty()) row(off.join(' '), textColor);
  }
  QStringList rangePtl;
  if (prefs_.showsSsaField("RANGE"))
    rangePtl.append(QString::number(qRound(range_)) + "NM");
  if (prefs_.showsSsaField("PTL") && prefs_.ptlHalfMinutes)
    rangePtl.append("PTL: " + prefs_.ptlLength());
  if (!rangePtl.isEmpty()) row(rangePtl.join(' '), textColor);
  if (prefs_.showsSsaField("ALT FIL")) row(altitudeFilters(true), textColor);
  for (bool reduced : {false, true}) {
    if (!prefs_.showsSsaField(reduced ? "2.5" : "INTRAIL")) continue;
    const auto line = atpa_.statusLine(reduced);
    if (!line.isEmpty()) row(line, textColor);
  }
  const auto entries = airportSettings();
  for (qsizetype i = 0; i < entries.size(); i += 3) {
    SsaRow airports;
    for (auto j = i; j < std::min(i + 3, entries.size()); ++j) {
      const auto& entry = entries[j];
      airports.append({j == i ? entry.text : ' ' + entry.text,
                       entry.missing ? warningColor : textColor,
                       entry.missing});
    }
    rows.append(airports);
  }
  if (prefs_.showsSsaField("OP MODE")) row("MODE: NORMAL", textColor);
  if (historyIndex_ && prefs_.showsSsaField("WX HIST"))
    row(QStringLiteral("WX HIST:%1").arg(historyIndex_), textColor);
  if (prefs_.showsSsaField("QL")) {
    auto lines = quickLookLines();
    if (!lines.isEmpty()) lines.front().prepend("QL: ");
    for (const auto& line : lines) row(line, textColor);
  }
  if (prefs_.showsSsaField("CRDA") && crda_.enabled())
    row("CRDA ENABLED", textColor);
  if (prefs_.showsSsaField("CON/CPL") && !tcp_.isEmpty())
    row(tcp_ + " CON: " + tcp_, textColor);
  return rows;
}
QString STARSPane::altitudeFilters(bool ssa) const {
  const auto label = [](int feet) {
    return feet < 0 ? QString("N99")
                    : QString::number(feet / 100).rightJustified(3, '0');
  };
  const auto& a = prefs_.associatedAltitude;
  const auto& u = prefs_.unassociatedAltitude;
  return label(u[0]) + " " + label(u[1]) + (ssa ? " U " : "\n") + label(a[0]) +
         " " + label(a[1]) + (ssa ? " A" : "");
}
QString STARSPane::listText() const {
  if (list() == List::None) return {};
  QString text = list_ == List::Current ? "MAPS" : "GEOGRAPHIC MAPS";
  int count = 0;
  for (const auto& [id, map] : listing_) {
    if (map.shortName.trimmed().isEmpty() ||
        (list_ == List::Current && !visible(id)))
      continue;
    auto label = map.shortName.trimmed().left(8).toUpper().replace(' ', '_');
    text += QString("\n%1%2 %3 %4")
                .arg(visible(id) ? ">" : " ")
                .arg(id, 3)
                .arg(label, -8)
                .arg(map.name.trimmed().toUpper());
    if (++count == 50) break;
  }
  return text;
}
void STARSPane::drawVideoMapsList(renderer::CmdBuffer& cb,
                                  const renderer::FontSize& font,
                                  renderer::TextureID texture, int width,
                                  int height) const {
  using namespace renderer;
  auto text = listText();
  if (!text.isEmpty()) {
    cb.loadProjectionMatrix(screenOrtho(width, height));
    const auto anchor = prefs_.lists.value("TX").position;
    TextBuilder builder;
    builder.addText(
        font, text.toStdString(),
        {float(width * anchor.x()), float(height * anchor.y())},
        {colors::brightness(palette_.List, prefs_.brightness.lists).rgba(),
         {}});
    builder.generateCommands(cb, texture);
  }
}
}  // namespace stars
