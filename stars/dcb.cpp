#include "dcb.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "datablock.h"
#include "stars.h"

namespace stars {
namespace {
constexpr float size = 72;
using Page = Dcb::Page;
using Shape = Dcb::Shape;
using Button = Dcb::Button;
// Literal REDS layout order: two half-height buttons fill a column.
struct Layout {
  std::vector<Button> buttons;
  float x = 0, y = 0;
  bool disabled = false;
  void at(int column) {
    x = column * size;
    y = 0;
    disabled = false;
  }
  void add(const QString& text, Shape shape = Shape::Full,
           bool selected = false, bool unavailable = false,
           bool unsupported = false) {
    const float w = shape == Shape::HalfHorizontal ? size / 2 : size;
    const float h = shape == Shape::HalfVertical ? size / 2 : size;
    buttons.push_back({text,
                       {x, y, w, h},
                       shape,
                       selected,
                       disabled || unavailable,
                       unsupported});
    if (shape == Shape::HalfVertical && y == 0)
      y = h;
    else {
      x += w;
      y = 0;
    }
  }
  void nav(const QString& text, Page page, Shape shape = Shape::Full) {
    add(text, shape);
    buttons.back().destination = page;
    buttons.back().navigates = true;
  }
};
}  // namespace

Preferences& Dcb::preferences() {
  return radar_ ? radar_->preferences() : defaults_;
}
std::vector<Dcb::Control> Dcb::controls() {
  auto& b = preferences().brightness;
  auto& c = preferences().charSize;
  if (page_ == Page::Main)
    return {{"LDR LEN", "LDR LEN", &preferences().leaderLength, 0, 7, 1},
            {"LDR DIR", "LDR DIR", &preferences().leaderDirection, 0, 7, 1}};
  if (page_ == Page::Aux)
    return {{"VOL", "VOL", &preferences().audioVolume, 1, 10, 1},
            {"DWELL", "DWELL", &preferences().dwell, 0, 2, 1},
            {"HISTORY", "HISTORY", &preferences().historyCount, 1, 10, 1}};
  if (page_ == Page::Brite)
    return {{"DCB", "DCB", &b.dcb, 25, 100, 5},
            {"BKC", "BKC", &b.backgroundContrast, 0, 100, 5},
            {"MPA", "MPA", &b.videoGroupA, 5, 100, 5},
            {"MPB", "MPB", &b.videoGroupB, 5, 100, 5},
            {"FDB", "FDB", &b.fullDatablocks, 5, 100, 5, true},
            {"LST", "LST", &b.lists, 25, 100, 5},
            {"POS", "POS", &b.positions, 5, 100, 5, true},
            {"LDB", "LDB", &b.limitedDatablocks, 5, 100, 5, true},
            {"OTH", "OTH", &b.otherTracks, 5, 100, 5, true},
            {"TLS", "TLS", &b.lines, 5, 100, 5, true},
            {"RR", "RR", &b.rangeRings, 5, 100, 5, true},
            {"CMP", "CMP", &b.compass, 5, 100, 5, true},
            {"BCN", "BCN", &b.beaconSymbols, 5, 100, 5, true},
            {"PRI", "PRI", &b.primarySymbols, 5, 100, 5, true},
            {"HST", "HST", &b.history, 5, 100, 5, true},
            {"WX", "WX", &b.weather, 5, 100, 5},
            {"WXC", "WXC", &b.wxContrast, 5, 100, 5}};
  if (page_ == Page::CharSize)
    return {{"DATA BLOCKS", "DATA\nBLOCKS", &c.datablocks, 0, 5, 1},
            {"LISTS", "LISTS", &c.lists, 0, 5, 1},
            {"DCB", "DCB", &c.dcb, 0, 2, 1},
            {"TOOLS", "TOOLS", &c.tools, 0, 5, 1},
            {"POS", "POS", &c.positionSymbols, 0, 5, 1}};
  return {};
}
void Dcb::finishAdjustment() {
  saveAs_ = false;
  ringMotion_ = 0;
  active_.clear();
  input_.clear();
  error_.clear();
}
namespace {
constexpr std::array ringSpacings{2, 5, 10, 20};
}
bool Dcb::setActiveValue(int value) {
  if (active_ == "LDR DIR") {
    const int direction =
        QStringLiteral("89632147").indexOf(QString::number(value));
    if (direction < 0 || value < 1 || value > 9) return false;
    preferences().leaderDirection = direction;
    return true;
  }
  if (active_ == "Ring spacing") {
    if (std::find(ringSpacings.begin(), ringSpacings.end(), value) ==
        ringSpacings.end())
      return false;
    preferences().ringSpacing = value;
    return true;
  }
  for (const auto& control : controls()) {
    if (control.id != active_) continue;
    if (value > control.maximum ||
        (value < control.minimum && !(value == 0 && control.allowOff)))
      return false;
    *control.value = value;
    if (active_ == "VOL" && radar_) radar_->previewVolume();
    return true;
  }
  return false;
}
void Dcb::step(int direction) {
  if (active_ == "LDR DIR") {
    preferences().leaderDirection =
        (preferences().leaderDirection + direction + 8) % 8;
    return;
  }
  input_.clear();
  error_.clear();
  if (active_ == "PTL LNTH") {
    auto& length = preferences().ptlHalfMinutes;
    length = std::clamp(length + direction, 0, 10);
    return;
  }
  if (active_ == "Ring spacing") {
    auto& spacing = preferences().ringSpacing;
    auto it = std::find(ringSpacings.begin(), ringSpacings.end(), spacing);
    if (it != ringSpacings.end()) {
      auto index = std::clamp(int(it - ringSpacings.begin()) - direction, 0,
                              int(ringSpacings.size()) - 1);
      spacing = ringSpacings[index];
    }
    ringMotion_ = 0;
    return;
  }
  for (const auto& c : controls())
    if (c.id == active_) {
      int next = *c.value + direction * c.step;
      if (active_ == "VOL" && radar_ &&
          std::clamp(next, c.minimum, c.maximum) != *c.value)
        radar_->previewVolume();
      *c.value = next < c.minimum && c.allowOff
                     ? 0
                     : std::clamp(next, c.minimum, c.maximum);
      return;
    }
}
int Dcb::columns() const {
  if (page_ == Page::Maps) return 22;
  if (page_ == Page::CharSize || page_ == Page::Pref) return 20;
  return 19;
}

int Dcb::maximumWidth() {
  // Measure the unscrolled button rectangles, including submenu overlays.
  Dcb dcb;
  int width = 0;
  for (int i = 0; i < static_cast<int>(Page::Count); ++i) {
    dcb.page_ = static_cast<Page>(i);
    for (const auto& button : dcb.layout(std::numeric_limits<float>::max()))
      width = std::max(width, static_cast<int>(std::ceil(button.rect.right())));
  }
  return width;
}

std::vector<Button> Dcb::layout(float width) {
  scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, columns() * size - width));
  Layout l;
  const auto half = Shape::HalfVertical;
  if (page_ == Page::Aux || page_ == Page::TPA) {
    l.disabled = page_ == Page::TPA;
    l.add("VOL\n" + QString::number(preferences().audioVolume), Shape::Full,
          active_ == "VOL");
    l.buttons.back().control = "VOL";
    l.add("HISTORY\n" + QString::number(preferences().historyCount), half,
          active_ == "HISTORY");
    l.buttons.back().control = "HISTORY";
    l.add("H_RATE\n4.5", half);
    l.add("CURSOR\nHOME");
    for (const auto* text :
         {"CSR SPD\n5", "MAP\nUNCOR", "UNCOR", "BEACON\nMODE-2", "RTQC", "MCP"})
      l.add(text, Shape::Full, false, false, true);
    l.add("DCB\nTOP", half, true);
    l.add("DCB\nLEFT", half);
    l.add("DCB\nRIGHT", half);
    l.add("DCB\nBOTTOM", half);
    l.add("PTL\nLNTH\n" + preferences().ptlLength(), Shape::Full,
          active_ == "PTL LNTH");
    l.buttons.back().control = "PTL LNTH";
    l.add("PTL OWN", half, preferences().ptlOwn, !preferences().ptlHalfMinutes);
    l.buttons.back().control = "PTL OWN";
    l.add("PTL ALL", half, preferences().ptlAll, !preferences().ptlHalfMinutes);
    l.buttons.back().control = "PTL ALL";
    l.add("DWELL\n" + QStringList{"OFF", "ON", "LOCK"}.at(preferences().dwell),
          Shape::Full, active_ == "DWELL");
    l.buttons.back().control = "DWELL";
    l.nav("TPA/\nATPA", Page::TPA);
    l.add("TSAS", half, false, false, true);
    l.add("TIME\nLINE", half, false, false, true);
    l.nav("SHIFT", Page::Main);
  } else {
    l.disabled = page_ != Page::Main;
    l.add("RANGE\n" + QString::number(radar_ ? int(radar_->range() + .5)
                                             : STARSPane::initialRangeNM));
    l.add("PLACE\nCNTR", half, radar_ && radar_->placingCenter());
    l.add("OFF\nCNTR", half, radar_ && radar_->offCenter());
    l.add("RR\n" + QString::number(preferences().ringSpacing), Shape::Full,
          active_ == "Ring spacing");
    l.buttons.back().control = "Ring spacing";
    l.add("PLACE\nRR", half, placingRings());
    l.buttons.back().control = "Place rings";
    l.add("RR\nCNTR", half, !preferences().useUserRingCenter);
    l.buttons.back().control = "Ring center";
    l.nav("MAPS", Page::Maps);
    for (int i = 0; i < 6; ++i)
      l.add(maps_[i], half,
            radar_ && radar_->visible(maps_[i].section('\n', 0, 0).toInt()),
            maps_[i].isEmpty());
    const auto& levels = preferences().displayWeatherLevel;
    for (int level = 1; level <= int(levels.size()); ++level) {
      const bool available = radar_ && radar_->weatherAvailable(level);
      l.add("WX" + QString::number(level) + (available ? "\nAVL" : ""),
            Shape::HalfHorizontal, levels[level - 1]);
      l.buttons.back().weatherLevel = level;
    }
    l.nav("BRITE", Page::Brite);
    l.add("LDR DIR\n" + leaderDirections.at(preferences().leaderDirection),
          half, active_ == "LDR DIR", false,
          radar_ && radar_->useTaisLeaderDirection());
    l.buttons.back().control = "LDR DIR";
    l.add("LDR LEN\n" + QString::number(preferences().leaderLength), half,
          active_ == "LDR LEN");
    l.buttons.back().control = "LDR LEN";
    l.nav("CHAR\nSIZE", Page::CharSize);
    l.add("MODE\nFSL", Shape::Full, false, false, true);
    l.add("SITE\nFUSED", Shape::Full, false, false, true);
    l.nav(selectedSlot_ ? "PREF\n" + setName(selectedSlot_) : "PREF",
          Page::Pref);
    l.nav("SSA\nFILTER", Page::SSA, half);
    l.nav("GI TEXT\nFILTER", Page::GI, half);
    l.nav("SHIFT", Page::Aux);
  }
  switch (page_) {
    case Page::Maps:
      l.at(5);
      l.nav("DONE", Page::Main, half);
      l.add("CLR ALL", half);
      for (int i = 6; i < 34; ++i)
        l.add(maps_[i], half,
              radar_ && radar_->visible(maps_[i].section('\n', 0, 0).toInt()));
      l.add("GEO\nMAPS", half,
            radar_ && radar_->list() == STARSPane::List::Geographic);
      l.add("AIRPORT", half);
      l.add("SYS\nPROC", half);
      l.add("CURRENT", half,
            radar_ && radar_->list() == STARSPane::List::Current);
      break;
    case Page::Brite:
      l.at(10);
      for (const auto& control : controls()) {
        auto value = *control.value == 0 ? QString("OFF")
                                         : QString::number(*control.value);
        l.add(control.label + " " + value, half, active_ == control.id);
        l.buttons.back().control = control.id;
      }
      l.nav("DONE", Page::Main, half);
      break;
    case Page::CharSize:
      l.at(14);
      for (const auto& control : controls()) {
        auto value = QString::number(*control.value);
        l.add(control.label + "\n" + value, Shape::Full, active_ == control.id);
        l.buttons.back().control = control.id;
      }
      l.nav("DONE", Page::Main);
      break;
    case Page::Pref:
      l.at(0);
      for (int i = 1; i <= 32; ++i) {
        const auto name = setName(i);
        l.add(QString::number(i) + "\n" + name, half, i == selectedSlot_,
              name.isEmpty());
      }
      l.add("DEFAULT", half);
      l.add("FSSTARS", half, false, false, true);
      l.add("RESTORE", half);
      l.add("SAVE", half, false, !selectedSlot_);
      l.add("CHG PIN", half, false, false, true);
      l.add("SAVE AS", half, saveAs_, sets_.size() >= 32);
      l.add("DELETE", half, false, !selectedSlot_);
      l.nav("DONE", Page::Main, half);
      break;
    case Page::SSA:
      l.at(2);
      l.add("ALL", half, preferences().allSsaFields);
      l.buttons.back().control = "SSA field";
      for (const auto* text :
           {"WX",      "TIME",    "ALTSTG",  "STATUS",  "PLAN",    "RADAR",
            "CODES",   "SPC",     "SYS OFF", "RANGE",   "PTL",     "ALT FIL",
            "NAS I/F", "INTRAIL", "2.5",     "AIRPORT", "OP MODE", "TT",
            "WX HIST", "QL",      "TW OFF",  "CON/CPL", "OFF IND", "CRDA",
            "FLOW",    "AMZ",     "TBFM"}) {
        l.add(text, half, preferences().showsSsaField(text));
        l.buttons.back().control = "SSA field";
      }
      l.nav("DONE", Page::Main);
      break;
    case Page::GI:
      l.at(11);
      for (int i = 0; i < 10; ++i) {
        QString text = i == 0 ? "MAIN" : "GI " + QString::number(i);
        if (i && !airports_[i].isEmpty()) text += "\n" + airports_[i];
        l.add(text, half, preferences().giFilter[i]);
        l.buttons.back().control = "GI " + QString::number(i);
      }
      l.nav("DONE", Page::Main);
      break;
    case Page::TPA: {
      l.at(14);
      const auto& p = preferences();
      const bool enabled = radar_ ? radar_->atpaEnabled() : atpa_;
      const auto button = [&](QString label, QChar control, bool on) {
        l.add(label + (on ? "ENABLED" : "INHIBTD"), Shape::Full, false,
              control != '+' && !enabled);
        l.buttons.back().control = QString("TPA ") + control;
      };
      button("A/TPA\nMILEAGE\n", '+', p.tpaMileage);
      button("INTRAIL\nDIST\n", 'D', p.atpaDistance);
      button("ALERT\nCONES\n", 'A', p.atpaAlertCones);
      button("MONITOR\nCONES\n", 'B', p.atpaMonitorCones);
      l.nav("DONE", Page::Aux);
      break;
    }
    default:
      break;
  }
  for (auto& button : l.buttons) button.rect.translate(-scroll_, 0);
  return l.buttons;
}

void Dcb::release() {
  down_ = false;
  suppressPress_ = false;
  const auto action = std::exchange(pressedPref_, QString{});
  if (action.isEmpty() || page_ != Page::Pref ||
      !pressedPrefRect_.contains(mouse_))
    return;
  if (action == "DONE") {
    finishAdjustment();
    page_ = Page::Main;
  } else if (radar_)
    prefAction(action);
}

void Dcb::press(float width) {
  pressedPref_.clear();
  down_ = true;
  if (adjusting() && mouse_.y() >= size) {
    if (active_ == "VOL" && radar_) radar_->previewVolume();
    finishAdjustment();
    suppressPress_ = true;
    return;
  }
  if (mouse_.x() < 0 || mouse_.x() >= width || mouse_.y() < 0 ||
      mouse_.y() >= size)
    return;
  auto buttons = layout(width);
  // Overlays own the hit, including their disabled or inert buttons.
  for (auto i = buttons.rbegin(); i != buttons.rend(); ++i)
    if (i->rect.contains(mouse_)) {
      if (page_ == Page::Pref && !i->disabled && !i->unsupported) {
        pressedPref_ = i->text;
        pressedPrefRect_ = i->rect;
      } else if (i->navigates && !i->disabled && !i->unsupported) {
        if (i->destination == Page::Pref)
          openPref();
        else {
          finishAdjustment();
          page_ = i->destination;
        }
        suppressPress_ = true;

      } else if (i->control.startsWith("TPA ") && !i->disabled) {
        if (radar_)
          radar_->setAtpaDisplay(i->control.back(),
                                 i->text.endsWith("INHIBTD"));
        suppressPress_ = true;
      } else if (!i->disabled &&
                 (i->control == "PTL OWN" || i->control == "PTL ALL")) {
        const bool own = i->control == "PTL OWN";
        preferences().ptlOwn = own && !i->selected;
        preferences().ptlAll = !own && !i->selected;
        suppressPress_ = true;
      } else if (i->control.startsWith("GI ")) {
        auto& shown = preferences().giFilter[i->control.mid(3).toInt()];
        shown = !shown;
        suppressPress_ = true;
      } else if (i->control == "SSA field" && !i->disabled) {
        auto& prefs = preferences();
        if (i->text == "ALL")
          prefs.allSsaFields = !prefs.allSsaFields;
        else {
          prefs.allSsaFields = false;
          if (i->selected)
            prefs.ssaFields.remove(i->text);
          else
            prefs.ssaFields.insert(i->text);
        }
        suppressPress_ = true;
      } else if (i->weatherLevel && !i->disabled) {
        auto& selected = preferences().displayWeatherLevel[i->weatherLevel - 1];
        selected = !selected;
        suppressPress_ = true;
      } else if (i->control == "Ring center" && !i->disabled) {
        auto& user = preferences().useUserRingCenter;
        user = !user;
        suppressPress_ = true;
      } else if (!i->control.isEmpty() && !i->disabled && !i->unsupported) {
        const bool selected = active_ == i->control;
        if (selected && active_ == "VOL" && radar_) radar_->previewVolume();
        finishAdjustment();
        if (!selected) {
          active_ = i->control;
          if (radar_ && page_ == Page::Main) radar_->finishCenter();
        }
        suppressPress_ = true;
      } else if (radar_ && !i->disabled && !i->unsupported) {
        if (i->text == "CLR ALL")
          radar_->clearMaps();
        else if (i->text == "GEO\nMAPS")
          radar_->toggleList(STARSPane::List::Geographic);
        else if (i->text == "CURRENT")
          radar_->toggleList(STARSPane::List::Current);
        else if (i->text == "PLACE\nCNTR") {
          finishAdjustment();
          radar_->placeCenter();
        } else if (i->text == "OFF\nCNTR")
          radar_->toggleCenter();
        else if (page_ == Page::Main || page_ == Page::Maps) {
          const auto id = i->text.section('\n', 0, 0).toInt();
          if (id > 0) radar_->toggleMap(id);
        }
      }
      return;
    }
}
void Dcb::adjustMotion(double deltaY) {
  if (active_ != "Ring spacing" && active_ != "LDR DIR" &&
      active_ != "LDR LEN" && active_ != "DWELL" && active_ != "VOL" &&
      active_ != "HISTORY" && active_ != "PTL LNTH")
    return;
  ringMotion_ += deltaY;
  if (std::abs(ringMotion_) > 10) {
    step(ringMotion_ < 0 ? 1 : -1);
    ringMotion_ = 0;
  }
}
void Dcb::wheel(float delta, float width) {
  if (adjusting()) {
    if (delta != 0) step(delta > 0 ? 1 : -1);
    return;
  }
  if (mouse_.y() < 0 || mouse_.y() >= size || mouse_.x() < 0 ||
      mouse_.x() >= width || delta == 0)
    return;
  scroll_ += delta > 0 ? size : -size;
  scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, columns() * size - width));
}

void Dcb::draw(renderer::CmdBuffer& cb, const renderer::FontSize& font,
               renderer::TextureID texture, int width, int height,
               int pixelWidth, int pixelHeight, int top) {
  using namespace renderer;
  if (width <= 0 || height <= 0) return;
  const float sx = float(pixelWidth) / width, sy = float(pixelHeight) / height;
  cb.loadProjectionMatrix(screenOrtho(float(width), float(height)));
  const QRectF bar(0, 0, width, std::min(72, height - top));
  auto clip = [&](QRectF r) {
    const int x = int(std::floor(r.left() * sx)),
              y = int(std::floor((r.top() + top) * sy));
    const int right = int(std::ceil(r.right() * sx)),
              bottom = int(std::ceil((r.bottom() + top) * sy));
    cb.scissor({x, pixelHeight - bottom, right - x, bottom - y});
  };
  auto fill = [&](QRectF r, RGB color) {
    ColoredTrianglesBuilder b;
    b.addQuad({float(r.left()), float(r.top() + top)},
              {float(r.right()), float(r.top() + top)},
              {float(r.right()), float(r.bottom() + top)},
              {float(r.left()), float(r.bottom() + top)}, color);
    b.generateCommands(cb);
  };
  const auto& palette = radar_ ? radar_->palette() : colors::tcw;
  clip(bar);
  fill(bar, palette.DCBBackground);
  for (const auto& button : layout(width)) {
    const auto visible = button.rect.intersected(bar);
    if (visible.isEmpty()) continue;
    clip(visible);
    bool selected = button.selected;
    const bool hovered = visible.contains(mouse_);
    if (hovered && down_ &&
        (page_ != Page::Pref ||
         button.rect == pressedPrefRect_ && button.text == pressedPref_) &&
        !suppressPress_ && !button.disabled && !button.unsupported)
      selected = !selected;
    RGB color = selected ? palette.DCBActiveButton : palette.DCBButton,
        text = palette.DCBText;
    if (button.weatherLevel && radar_ &&
        radar_->weatherAvailable(button.weatherLevel))
      color = selected ? palette.DCBActiveWXButton : palette.DCBWXButton;
    if (button.disabled) {
      color = palette.DCBDisabledButton;
      text = palette.DCBDisabledText;
    } else if (button.unsupported) {
      color = palette.DCBUnsupportedButton;
      text = palette.DCBUnsupportedText;
    } else if (hovered)
      text = palette.DCBTextSelected;
    const float illumination = preferences().brightness.dcb / 100.f;
    color.r *= illumination;
    color.g *= illumination;
    color.b *= illumination;
    fill(button.rect, color);
    auto light = palette.DCBTopBevel, dark = palette.DCBBottomBevel;
    if (selected && !button.disabled && !button.unsupported)
      std::swap(light, dark);
    const float x = button.rect.left(), y = button.rect.top() + top,
                r = button.rect.right(), b = button.rect.bottom() + top;
    ColoredLinesBuilder bevel;
    for (int i = 0; i < 3; ++i) {
      const float f = float(i);
      bevel.addLineRGB({x, y + f}, {r - f, y + f}, light);
      bevel.addLineRGB({x + f, y}, {x + f, b - f}, light);
      bevel.addLineRGB({r - f, y + f}, {r - f, b}, dark);
      bevel.addLineRGB({x + f, b - f}, {r, b - f}, dark);
    }
    // macOS' REDS OpenGL backend only supports 1-pixel aliased lines.
#ifdef __APPLE__
    cb.lineWidth(1);
#else
    cb.lineWidth(std::max(1.f, sx));
#endif
    bevel.generateCommands(cb);
    auto label = button.text.trimmed();
    if (label.isEmpty()) continue;
    const auto lines = label.split('\n');
    const int count = button.shape == Shape::HalfHorizontal
                          ? std::max(2, int(lines.size()))
                          : int(lines.size());
    float ty =
        y +
        std::max(0.f,
                 (float(button.rect.height()) - font.lineHeight * count) / 2);
    TextBuilder textBuilder;
    for (const auto& raw : lines) {
      auto line = raw.trimmed();
      int advance = 0;
      for (const auto c : line.toUcs4())
        if (auto g = font.glyph(c)) advance += g->advance;
      const float tx =
          x + std::max(1.f, (float(button.rect.width()) - advance) / 2);
      textBuilder.addText(font, line.toStdString(), {tx, ty}, {text.rgba()});
      ty += font.lineHeight;
    }
    textBuilder.generateCommands(cb, texture);
  }
  cb.disableScissor();
}
}  // namespace stars
