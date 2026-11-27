#pragma once
#include <array>

#include "../renderer/rgb.h"

namespace stars::colors {
using renderer::RGB;
using renderer::rgb8;

// TI 6191.409 Rev. 30, Appendix B, Table B-1. REDS supplies the DCB colors.
inline constexpr RGB black = rgb8(0, 0, 0);
inline constexpr RGB white = rgb8(255, 255, 255);
inline constexpr RGB green = rgb8(0, 255, 0);
inline constexpr RGB red = rgb8(255, 0, 0);
inline constexpr RGB yellow = rgb8(255, 255, 0);
inline constexpr RGB cyan = rgb8(0, 255, 255);
inline constexpr RGB blue = rgb8(30, 120, 255);
inline constexpr RGB orange = rgb8(255, 55, 0);
inline constexpr RGB dimGray = rgb8(140, 140, 140);
inline constexpr RGB gray = rgb8(128, 128, 128);
inline constexpr RGB lightBlue = rgb8(173, 216, 230);
inline constexpr RGB magenta = rgb8(255, 0, 255);
inline constexpr RGB gold1 = rgb8(238, 201, 0);
inline constexpr RGB coral2 = rgb8(238, 106, 80);
inline constexpr RGB darkOliveGreen = rgb8(162, 205, 90);
inline constexpr RGB goldenRod = rgb8(218, 165, 32);
inline constexpr RGB royalBlue = rgb8(72, 118, 255);
inline constexpr RGB lightSlateBlue = rgb8(132, 112, 255);
inline constexpr RGB aquamarine2 = rgb8(118, 238, 198);
inline constexpr RGB carrot = rgb8(237, 145, 33);
inline constexpr RGB orchid = rgb8(218, 112, 214);
inline constexpr RGB rosyBrown2 = rgb8(238, 180, 180);
inline constexpr RGB limeGreen = rgb8(50, 205, 50);
inline constexpr RGB indianRed = rgb8(255, 106, 106);
inline constexpr RGB darkGrayBlue = rgb8(38, 77, 77);
inline constexpr RGB darkMustard = rgb8(100, 100, 51);

struct Palette {
  RGB OwnedDatablock = white;
  RGB UnownedDatablock = green;
  RGB AlertDatablock = red;
  RGB CautionDatablock = yellow;
  RGB GhostDatablock = yellow;
  RGB SelectedDatablock = cyan;
  RGB TrackGeometry = blue;
  RGB BeaconTargetExtent = green;
  RGB PositionSymbolOwned = white;
  RGB PositionSymbolOutline = black;
  RGB TestTarget = white;
  RGB TerminalProximityAlert = rgb8(90, 180, 255);
  std::array<RGB, 5> TrackHistory = {rgb8(30, 80, 200), rgb8(70, 70, 170),
                                     rgb8(50, 50, 130), rgb8(40, 40, 110),
                                     rgb8(30, 30, 90)};
  RGB List = green;
  RGB TextAlert = red;
  RGB TextWarning = yellow;
  RGB SystemStatusWX = cyan;
  RGB CoordMessage = green;
  RGB PreviewList = green;
  RGB SSAASR9Status = yellow;
  RGB Cursor = white;
  RGB Background = black;
  RGB BackgroundCeiling = rgb8(50, 50, 50);
  RGB RangeBearingLine = white;
  RGB PTL = white;
  RGB Compass = dimGray;
  RGB RangeRing = dimGray;
  RGB ATPAWarning = yellow;
  RGB ATPAAlert = orange;
  RGB MapADefault = dimGray;
  RGB MapBDefault = dimGray;
  std::array<RGB, 8> MapA = {dimGray, cyan,           magenta,   gold1,
                             coral2,  darkOliveGreen, goldenRod, royalBlue};
  std::array<RGB, 8> MapB = {dimGray, lightSlateBlue, aquamarine2, carrot,
                             orchid,  rosyBrown2,     limeGreen,   indianRed};
  RGB RestrictionAreaText = yellow;
  std::array<RGB, 8> RestrictionAreaGeom = {
      dimGray, cyan,           magenta,     gold1,
      coral2,  lightSlateBlue, aquamarine2, limeGreen};
  std::array<RGB, 6> WX = {darkGrayBlue, darkGrayBlue, darkGrayBlue,
                           darkMustard,  darkMustard,  darkMustard};
  RGB WXPattern = white;
  RGB FMARunway = gray;
  RGB FMANTZNormal = white;
  RGB FMANTZCaution = yellow;
  RGB FMANTZWarning = red;
  RGB FMAReferenceLines = white;
  RGB FMAAMZOutline = lightBlue;
  RGB FMACourseLine = white;
  RGB FMAFixBarLine = white;
  RGB TSASTimelineETAOwned = white;
  RGB TSASTimelineETAUnowned = green;
  RGB TSASTimelineNonFlightData = white;
  RGB TSASTimelineSTA = cyan;
  RGB TSASTimelineSpacing = yellow;
  RGB TSASTrajectory = white;
  RGB TSASSlotOwned = white;
  RGB TSASSlotUnowned = green;
  RGB TSASSlotHighlight = cyan;
  RGB TSASSlotHandoffAttention = white;
  RGB TSASSlotPointoutAttention = yellow;
  RGB TSASSlotPointoutAccepted = yellow;
  RGB TSASSlotRNPOverride = orange;
  RGB TSASSlotNonDeconflicted = yellow;
  RGB DCBButton = rgb8(0, 44, 0);
  RGB DCBActiveButton = rgb8(0, 78, 0);
  RGB DCBText = rgb8(255, 255, 255);
  RGB DCBTextSelected = rgb8(255, 255, 0);
  RGB DCBUnsupportedButton = rgb8(100, 100, 100);
  RGB DCBUnsupportedText = rgb8(200, 200, 200);
  RGB DCBDisabledButton = rgb8(0, 22, 0);
  RGB DCBDisabledText = rgb8(128, 128, 128);
  RGB DCBBackground = rgb8(0, 12, 0);
  RGB DCBTopBevel = rgb8(50, 50, 50);
  RGB DCBBottomBevel = rgb8(0, 0, 0);
  RGB DCBWXButton = rgb8(83, 83, 162);
  RGB DCBActiveWXButton = rgb8(116, 116, 162);
};

inline constexpr Palette tcw{};
inline constexpr Palette tdw = [] {
  auto p = tcw;
  p.UnownedDatablock = p.TerminalProximityAlert = p.TSASSlotUnowned = white;
  p.MapADefault = p.MapBDefault = p.MapA[0] = yellow;
  p.WX = {rgb8(29, 56, 42), rgb8(29, 56, 42), rgb8(94, 72, 30),
          rgb8(94, 72, 30), rgb8(97, 48, 87), rgb8(97, 48, 87)};
  // Table B-1 keeps indexed category B color 1 dim gray at both workstations.
  // FMA is available at TCWs only.
  p.FMARunway = p.FMANTZNormal = p.FMANTZCaution = p.FMANTZWarning = black;
  p.FMAReferenceLines = p.FMAAMZOutline = p.FMACourseLine = p.FMAFixBarLine =
      black;
  return p;
}();

inline constexpr Palette dod = [] {
  auto p = tcw;
  p.UnownedDatablock = p.TerminalProximityAlert = p.TSASSlotUnowned = white;
  p.MapADefault = p.MapBDefault = p.MapA[0] = cyan;
  p.WX = {rgb8(13, 27, 14), rgb8(13, 27, 14), rgb8(54, 37, 7),
          rgb8(54, 37, 7),  rgb8(54, 24, 33), rgb8(54, 24, 33)};
  return p;
}();

constexpr RGB brightness(RGB color, int percent) {
  const float scale = percent / 100.f;
  return {color.r * scale, color.g * scale, color.b * scale};
}
}  // namespace stars::colors
