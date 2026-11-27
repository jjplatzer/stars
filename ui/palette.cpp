#include "palette.h"

namespace stars::ui {
namespace {
// Stable profile keys and display labels for every independently editable role.
template <class Palette, class Visitor>
void visitColors(Palette& p, Visitor visit) {
  const auto series = [&](const QString& key, const QString& label,
                          auto& entries) {
    for (size_t i = 0; i < entries.size(); ++i) {
      const auto number = QString::number(i + 1);
      visit(key + number, label + ' ' + number, entries[i]);
    }
  };
  visit("BackgroundCeiling", "BKC Ceiling", p.BackgroundCeiling);
  visit("Background", "Scope Background", p.Background);
  visit("Cursor", "Scope Cursor", p.Cursor);
  visit("List", "List Text", p.List);
  visit("PreviewList", "Preview Area", p.PreviewList);
  visit("TextAlert", "Text Alert", p.TextAlert);
  visit("TextWarning", "Text Warning", p.TextWarning);
  visit("SystemStatusWX", "SSA Weather Levels", p.SystemStatusWX);
  visit("SSAASR9Status", "SSA ASR-9 Status", p.SSAASR9Status);
  visit("CoordMessage", "Coordination Message", p.CoordMessage);
  visit("RangeRing", "Range Ring", p.RangeRing);
  visit("Compass", "Compass", p.Compass);
  visit("RangeBearingLine", "Range Bearing Line", p.RangeBearingLine);
  visit("PTL", "Predicted Track Line", p.PTL);
  series("WX", "Weather Level", p.WX);
  visit("WXPattern", "Weather Stipple", p.WXPattern);
  visit("MapADefault", "Map A Default", p.MapADefault);
  series("MapA", "Map A Color", p.MapA);
  visit("MapBDefault", "Map B Default", p.MapBDefault);
  series("MapB", "Map B Color", p.MapB);
  visit("RestrictionAreaText", "Restriction Area Text", p.RestrictionAreaText);
  series("RestrictionAreaGeom", "Restriction Area Color",
         p.RestrictionAreaGeom);
  visit("OwnedDatablock", "Owned Data Block", p.OwnedDatablock);
  visit("UnownedDatablock", "Unowned Data Block", p.UnownedDatablock);
  visit("AlertDatablock", "Alert Data Block", p.AlertDatablock);
  visit("CautionDatablock", "Caution Data Block", p.CautionDatablock);
  visit("GhostDatablock", "Ghost Data Block", p.GhostDatablock);
  visit("SelectedDatablock", "Selected Data Block", p.SelectedDatablock);
  visit("TrackGeometry", "Track Geometry", p.TrackGeometry);
  visit("BeaconTargetExtent", "Beacon Target Extent", p.BeaconTargetExtent);
  visit("PositionSymbolOwned", "Position Symbol Owned", p.PositionSymbolOwned);
  visit("PositionSymbolOutline", "Position Symbol Outline",
        p.PositionSymbolOutline);
  visit("TestTarget", "Test Target", p.TestTarget);
  visit("TerminalProximityAlert", "Terminal Proximity Alert",
        p.TerminalProximityAlert);
  series("TrackHistory", "Track History", p.TrackHistory);
  visit("ATPAWarning", "ATPA Warning", p.ATPAWarning);
  visit("ATPAAlert", "ATPA Alert", p.ATPAAlert);
  visit("FMARunway", "FMA Runway", p.FMARunway);
  visit("FMANTZNormal", "FMA NTZ Normal", p.FMANTZNormal);
  visit("FMANTZCaution", "FMA NTZ Caution", p.FMANTZCaution);
  visit("FMANTZWarning", "FMA NTZ Warning", p.FMANTZWarning);
  visit("FMAReferenceLines", "FMA Reference Lines", p.FMAReferenceLines);
  visit("FMAAMZOutline", "FMA AMZ Outline", p.FMAAMZOutline);
  visit("FMACourseLine", "FMA Course Line", p.FMACourseLine);
  visit("FMAFixBarLine", "FMA Fix Bar Line", p.FMAFixBarLine);
  visit("TSASTimelineETAOwned", "TSAS Timeline ETA Owned",
        p.TSASTimelineETAOwned);
  visit("TSASTimelineETAUnowned", "TSAS Timeline ETA Unowned",
        p.TSASTimelineETAUnowned);
  visit("TSASTimelineNonFlightData", "TSAS Timeline Non Flight Data",
        p.TSASTimelineNonFlightData);
  visit("TSASTimelineSTA", "TSAS Timeline STA", p.TSASTimelineSTA);
  visit("TSASTimelineSpacing", "TSAS Timeline Spacing", p.TSASTimelineSpacing);
  visit("TSASTrajectory", "TSAS Trajectory", p.TSASTrajectory);
  visit("TSASSlotOwned", "TSAS Slot Owned", p.TSASSlotOwned);
  visit("TSASSlotUnowned", "TSAS Slot Unowned", p.TSASSlotUnowned);
  visit("TSASSlotHighlight", "TSAS Slot Highlight", p.TSASSlotHighlight);
  visit("TSASSlotHandoffAttention", "TSAS Slot Handoff Attention",
        p.TSASSlotHandoffAttention);
  visit("TSASSlotPointoutAttention", "TSAS Slot Pointout Attention",
        p.TSASSlotPointoutAttention);
  visit("TSASSlotPointoutAccepted", "TSAS Slot Pointout Accepted",
        p.TSASSlotPointoutAccepted);
  visit("TSASSlotRNPOverride", "TSAS Slot RNP Override", p.TSASSlotRNPOverride);
  visit("TSASSlotNonDeconflicted", "TSAS Slot Non Deconflicted",
        p.TSASSlotNonDeconflicted);
  visit("DCBButton", "DCB Button", p.DCBButton);
  visit("DCBActiveButton", "DCB Active Button", p.DCBActiveButton);
  visit("DCBText", "DCB Text", p.DCBText);
  visit("DCBTextSelected", "DCB Text Selected", p.DCBTextSelected);
  visit("DCBUnsupportedButton", "DCB Unsupported Button",
        p.DCBUnsupportedButton);
  visit("DCBUnsupportedText", "DCB Unsupported Text", p.DCBUnsupportedText);
  visit("DCBDisabledButton", "DCB Disabled Button", p.DCBDisabledButton);
  visit("DCBDisabledText", "DCB Disabled Text", p.DCBDisabledText);
  visit("DCBBackground", "DCB Background", p.DCBBackground);
  visit("DCBTopBevel", "DCB Top Bevel", p.DCBTopBevel);
  visit("DCBBottomBevel", "DCB Bottom Bevel", p.DCBBottomBevel);
  visit("DCBWXButton", "DCB Weather Button", p.DCBWXButton);
  visit("DCBActiveWXButton", "DCB Active Weather Button", p.DCBActiveWXButton);
}
}  // namespace

QList<PaletteColor> paletteColors(const colors::Palette& palette) {
  QList<PaletteColor> result;
  visitColors(palette,
              [&](const QString& key, const QString& label, renderer::RGB rgb) {
                result.append({key, label,
                               QColor(qRound(rgb.r * 255), qRound(rgb.g * 255),
                                      qRound(rgb.b * 255))});
              });
  return result;
}
colors::Palette adaptedPalette(colors::Palette defaults,
                               const ColorOverrides& overrides) {
  visitColors(defaults,
              [&](const QString& key, const QString&, renderer::RGB& rgb) {
                const auto it = overrides.constFind(key);
                if (it != overrides.cend())
                  rgb = renderer::rgb8(it->red(), it->green(), it->blue());
              });
  return defaults;
}
void applyColorChanges(ColorOverrides& overrides,
                       const ColorOverrides& changes) {
  for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
    if (it->isValid())
      overrides.insert(it.key(), *it);
    else
      overrides.remove(it.key());
  }
}
}  // namespace stars::ui
