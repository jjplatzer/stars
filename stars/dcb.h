#pragma once

#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <array>
#include <vector>

#include "../renderer/font.h"
#include "prefs.h"

namespace stars {
class STARSPane;
// DCB layout, input and adjustment controls for one STARS pane.
class Dcb {
 public:
  static constexpr int height = 72;
  enum class Page {
    Main,
    Aux,
    Maps,
    Brite,
    CharSize,
    Pref,
    SSA,
    GI,
    TPA,
    Count
  };
  enum class Shape { Full, HalfVertical, HalfHorizontal };
  struct Button {
    QString text;
    QRectF rect;
    Shape shape;
    bool selected = false, disabled = false, unsupported = false;
    Page destination = Page::Main;
    bool navigates = false;
    QString control;
    int weatherLevel = 0;  // 1 through 6 for the main WX buttons.
  };
  static int maximumWidth();
  Dcb() = default;
  Dcb(const QJsonObject& configuration, const QString& position,
      const QString& tcp);
  static Dcb load(const QString& position, const QString& tcp);
  void radar(STARSPane* radar) { radar_ = radar; }
  void configurePrefSets(QJsonObject sets, SavePrefSets save) {
    sets_ = std::move(sets);
    saveSets_ = std::move(save);
  }
  bool hasSelectedSet() const { return selectedSlot_ != 0; }
  void openPref();
  void defaultPref();
  const std::array<QString, 10>& giAirports() const { return airports_; }
  Page page() const { return page_; }
  float scroll() const { return scroll_; }
  std::vector<Button> layout(float width);
  void motion(QPointF position) { mouse_ = position; }
  void press(float width);
  void release();
  void wheel(float delta, float width);
  bool adjusting() const { return !active_.isEmpty(); }
  bool placingRings() const { return active_ == "Place rings"; }
  void adjustMotion(double deltaY);
  QString preview() const;
  void finishAdjustment();
  void cancelCommand();
  void cancelLeaderAdjustment() {
    if (active_ == "LDR DIR") cancelCommand();
  }
  bool key(const QString& key);
  void draw(renderer::CmdBuffer& cb, const renderer::FontSize& font,
            renderer::TextureID texture, int width, int height, int pixelWidth,
            int pixelHeight, int top);

 private:
  struct Control {
    QString id, label;
    int* value;
    int minimum, maximum, step;
    bool allowOff = false;
  };
  QJsonObject sets_, entrySnapshot_;
  SavePrefSets saveSets_;
  int selectedSlot_ = 0, entrySlot_ = 0;
  bool saveAs_ = false;
  QString setName(int slot) const;
  void prefAction(const QString& action);
  bool prefKey(const QString& key);
  void commitSet(int slot, const QJsonObject& set);
  Preferences defaults_;
  Preferences& preferences();
  std::vector<Control> controls();
  QString pressedPref_;
  QRectF pressedPrefRect_;
  QString active_, input_, error_;
  bool setActiveValue(int value);
  void step(int direction);
  Page page_ = Page::Main;
  float scroll_ = 0;
  QPointF mouse_{-1, -1};
  bool down_ = false, suppressPress_ = false, atpa_ = false;
  double ringMotion_ = 0;
  STARSPane* radar_ = nullptr;
  std::array<QString, 38>
      maps_{};  // CRC order: 6 main, then 32 top/bottom slots.
  std::array<QString, 10> airports_{};
  int columns() const;
};
}  // namespace stars
