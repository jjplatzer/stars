#include "pane.h"

#include <SDL3/SDL.h>

#include <QApplication>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QKeySequence>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QTimeZone>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

#include "../renderer/builders.h"
#include "../renderer/window.h"
#include "../stars/cursors.h"
#include "../stars/font.h"
#include "../stars/stars.h"
#include "adaptation.h"
#include "alertaudio.h"
#include "itws.h"
#include "metar.h"
#include "native.h"
#include "net/tais.h"
#include "playback.h"
#include "theme.h"
#include "weather.h"

namespace stars::ui {
namespace {
constexpr int barHeight = 24, menuWidth = 28, buttonWidth = 36,
              popupWidth = 395;
enum TitleButton {
  Menu,
  Minimize,
  Maximize,
  Close,
  Replay10,
  Play,
  Forward10,
  settingsButton,
  recordingButton,
  switchProfileButton,
  altitudeButton,
  leaderButton,
  itwsButton,
  originalButton,
  hz1Button,
  hz2Button,
  popupBackground
};
struct MenuAction {
  TitleButton button;
  const char* label;
  QKeyCombination shortcut;
};
constexpr MenuAction menuActions[]{
    {settingsButton,
     QT_TRANSLATE_NOOP("stars::ui::Pane", "Open Adaptation Settings…"),
     Qt::CTRL | Qt::SHIFT | Qt::Key_S},
    {recordingButton, QT_TRANSLATE_NOOP("stars::ui::Pane", "Play Recording"),
     Qt::CTRL | Qt::ALT | Qt::Key_S},
    {switchProfileButton,
     QT_TRANSLATE_NOOP("stars::ui::Pane", "Switch Profile"),
     Qt::CTRL | Qt::SHIFT | Qt::Key_P},
    {altitudeButton,
     QT_TRANSLATE_NOOP("stars::ui::Pane", "Hide Assigned Altitudes"),
     Qt::CTRL | Qt::SHIFT | Qt::Key_A},
    {leaderButton,
     QT_TRANSLATE_NOOP("stars::ui::Pane", "Use TAIS-derived LDR DIR"),
     Qt::CTRL | Qt::SHIFT | Qt::Key_L},
    {itwsButton, QT_TRANSLATE_NOOP("stars::ui::Pane", "Use ITWS WX"),
     Qt::ALT | Qt::SHIFT | Qt::Key_W},
    {originalButton,
     QT_TRANSLATE_NOOP("stars::ui::Pane", "Historical Positions: Original"),
     Qt::Key_unknown},
    {hz1Button,
     QT_TRANSLATE_NOOP("stars::ui::Pane", "Reconstructed Positions: 1 Hz"),
     Qt::Key_unknown},
    {hz2Button,
     QT_TRANSLATE_NOOP("stars::ui::Pane", "Reconstructed Positions: 2 Hz"),
     Qt::Key_unknown}};
constexpr int titleStart = 4 * menuWidth;
constexpr int sectionHeading = 20, separatorHeight = 9, actionHeight = 30,
              sectionGap = 14;
constexpr int sectionHeight = sectionHeading + separatorHeight + actionHeight;
constexpr int basePopupHeight =
    2 * sectionHeight + sectionGap + 6 * actionHeight;
int sectionTop(int action) {
  return barHeight +
         (action == settingsButton ? 0 : sectionHeight + sectionGap);
}
QRect actionRect(int action, int width, bool hasItws) {
  const int offset =
      action >= originalButton
          ? (4 + int(hasItws) + action - originalButton) * actionHeight
      : action == itwsButton          ? 4 * actionHeight
      : action == leaderButton        ? 3 * actionHeight
      : action == altitudeButton      ? 2 * actionHeight
      : action == switchProfileButton ? actionHeight
                                      : 0;
  return {0, sectionTop(action) + sectionHeading + separatorHeight + offset,
          std::min(width, popupWidth), actionHeight};
}
qint64 firstRecordingMinute(qint64 now) {
  return ((Playback::earliest(now) + 59) / 60) * 60;
}
qint64 lastRecordingMinute(qint64 now) {
  return (Playback::latest(now) / 60) * 60;
}
QRect titleButtonRect(int button, int width) {
  if (button == Menu) return {0, 0, menuWidth, barHeight};
  if (button >= Replay10 && button <= Forward10)
    return {(1 + button - Replay10) * menuWidth, 0, menuWidth, barHeight};
  return {width - (Close + 1 - button) * buttonWidth, 0, buttonWidth,
          barHeight};
}
void require(bool ok) {
  if (!ok) throw std::runtime_error(SDL_GetError());
}
}  // namespace
struct Pane::Impl {
  static std::map<SDL_WindowID, Impl*> windows;
  static Impl* cursorOwner;
  Pane* owner;
  QString name, suffix;
  SDL_Window* window{};
  SDL_GPUDevice* device{};
  std::unique_ptr<renderer::Window> output;
  QTimer timer;
  renderer::TextureID texture{};
  std::unique_ptr<STARSPane> scope;
  Adaptation adaptation;
  Playback playback;
  net::TaisCache tais;
  std::unique_ptr<Weather> weather;
  QString itwsSiteId;
  std::unique_ptr<ItwsArchive> itws;
  int popupHeight() const {
    return basePopupHeight + (itwsSiteId.isEmpty() ? 0 : actionHeight);
  }
  void toggleItws() {
    if (itwsSiteId.isEmpty()) return;
    if (itws)
      itws.reset();
    else
      itws = std::make_unique<ItwsArchive>(itwsSiteId);
    scope->resetWeather();
    dirty = true;
  }
  std::unique_ptr<MetarArchive> metars;
  QPointF weatherCenter;
  QSize weatherSize;
  double weatherRange{};
  QRectF weatherBounds;
  std::unique_ptr<QDialog> recordingDialog;
  qint64 displayedSecond = -1;
  bool backEnabled = false, playEnabled = true, forwardEnabled = false;
  void openRecording() {
    setMenuOpen(false);
    if (recordingDialog && recordingDialog->isVisible()) {
      recordingDialog->raise();
      recordingDialog->activateWindow();
      return;
    }
    recordingDialog = std::make_unique<QDialog>();
    auto* dialog = recordingDialog.get();
    dialog->setWindowTitle(tr("Play Recording"));
    auto* layout = new QFormLayout(dialog);
    auto* edit = new QDateTimeEdit(dialog);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    edit->setTimeZone(QTimeZone::utc());
#else
    edit->setTimeSpec(Qt::UTC);
#endif
    edit->setDisplayFormat("yyyy-MM-dd HH:mm");
    edit->setKeyboardTracking(false);
    edit->setCalendarPopup(true);
    auto utc = [](qint64 time) {
      return QDateTime::fromSecsSinceEpoch(time, QTimeZone::utc());
    };
    const auto opened = QDateTime::currentSecsSinceEpoch();
    edit->setDateTimeRange(utc(firstRecordingMinute(opened)),
                           utc(lastRecordingMinute(opened)));
    edit->setDateTime(utc(playback.position(opened) / 60 * 60));
    layout->addRow(tr("Start Time (UTC)"), edit);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    layout->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog,
                     &QDialog::reject);
    QObject::connect(
        buttons, &QDialogButtonBox::accepted, dialog, [this, dialog, edit] {
          // Recheck retention only on confirmation so the field stays editable
          // at its lower bound.
          const auto now = QDateTime::currentSecsSinceEpoch();
          const auto minute = edit->dateTime().toSecsSinceEpoch() / 60 * 60;
          playback.select(std::clamp(minute, firstRecordingMinute(now),
                                     lastRecordingMinute(now)),
                          now);
          scope->beginPlayback(playback.position(now));
          tais.prepare(playback.position(now),
                       net::Traffic::ownershipLookbehindSeconds);
          dirty = true;
          dialog->accept();
        });
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
  }
  Fonts fontB;
  Fonts fontA{false};
  bool useFontB = true;
  std::unique_ptr<Cursors> cursors;
  std::unique_ptr<NativeMouse> nativeMouse;
  bool watching = false, nativeTracking = false;
  bool menuOpen = false;
  int hover = -1, pressed = -1;
  QSize lastSize;
  bool dirty = true, initialized = false, closing = false;
  Impl(Pane* p, const QString& name, const QString& suffix,
       const QString& facility)
      : owner(p), name(name), suffix(suffix), tais(facility) {}
  AlertAudio alarm;
  ~Impl() {
    timer.stop();
    nativeMouse.reset();
    if (watching) SDL_RemoveEventWatch(cursorEvent, this);
    if (cursorOwner == this) {
      SDL_SetCursor(SDL_GetDefaultCursor());
      cursorOwner = nullptr;
    }
    cursors.reset();
    if (window) windows.erase(SDL_GetWindowID(window));
    output.reset();
    if (device) SDL_DestroyGPUDevice(device);
    if (window) SDL_DestroyWindow(window);
    if (initialized) SDL_QuitSubSystem(SDL_INIT_VIDEO);
  }
  void setMenuOpen(bool open) {
    menuOpen = open;
    dirty = true;
    if (open) scope->cancelPointer();
  }
  void toggleLeaderDirection() {
    scope->toggleTaisLeaderDirection();
    dirty = true;
  }
  void toggleAssignedAltitudes() {
    scope->toggleAssignedAltitudes();
    dirty = true;
  }
  void openSettings() {
    setMenuOpen(false);
    adaptation.open();
  }
  void switchProfile() {
    if (closing) return;
    closing = true;
    emit owner->switchProfileRequested();
    owner->deleteLater();
  }
  bool menuKey(const QString& key) {
    if (!menuOpen) return false;
    if (key == "Escape") setMenuOpen(false);
    if (key == "Enter" || key == " ") openSettings();
    return true;
  }
  void selectCursor(float x, float y, bool force = false) {
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    const bool resize = !(SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) &&
                        y >= barHeight && (x < 5 || x >= w - 5 || y >= h - 5);
    auto* desired = SDL_GetDefaultCursor();
    if (!resize && button(x, y) < settingsButton)
      desired =
          cursors->at({x, y - barHeight}, {double(w), double(h - barHeight)},
                      scope->dcbHeight() > 0);
    cursorOwner = this;
    if (force || SDL_GetCursor() != desired) SDL_SetCursor(desired);
  }
  void toggleDcb() {
    scope->key("Ctrl+F9");
    if (cursorOwner == this) {
      float x, y;
      int wx, wy;
      SDL_GetGlobalMouseState(&x, &y);
      SDL_GetWindowPosition(window, &wx, &wy);
      selectCursor(x - wx, y - wy, true);
    }
  }
  static bool cursorEvent(void* data, SDL_Event* event) {
    // OS mouse events arrive on the video thread. Never make video calls
    // here for events injected by a worker thread.
    if (!SDL_IsMainThread()) return true;
    auto& self = *static_cast<Impl*>(data);
    if (SDL_GetWindowFromEvent(event) != self.window) return true;
    // Qt's native stream is authoritative while over this window. SDL's
    // Cocoa focus bookkeeping can lag behind that stream by one pump.
    if (cursorOwner && cursorOwner->nativeTracking &&
        (event->type == SDL_EVENT_MOUSE_MOTION ||
         event->type == SDL_EVENT_WINDOW_MOUSE_ENTER ||
         event->type == SDL_EVENT_WINDOW_MOUSE_LEAVE))
      return true;
    if (event->type == SDL_EVENT_MOUSE_MOTION)
      self.selectCursor(event->motion.x, event->motion.y);
    else if (event->type == SDL_EVENT_WINDOW_MOUSE_ENTER) {
      float x, y;
      int wx, wy;
      SDL_GetGlobalMouseState(&x, &y);
      SDL_GetWindowPosition(self.window, &wx, &wy);
      self.selectCursor(x - wx, y - wy, true);
    } else if (event->type == SDL_EVENT_WINDOW_MOUSE_LEAVE ||
               event->type == SDL_EVENT_WINDOW_HIDDEN ||
               event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
      if (cursorOwner == &self) {
        SDL_SetCursor(SDL_GetDefaultCursor());
        cursorOwner = nullptr;
      }
    }
    return true;
  }
  static void nativeMotion(void* data, float x, float y) {
    auto& self = *static_cast<Impl*>(data);
    if (x < 0 || y < 0) {
      self.nativeTracking = false;
      if (cursorOwner == &self) {
        SDL_SetCursor(SDL_GetDefaultCursor());
        cursorOwner = nullptr;
      }
      self.hover = -1;
      self.scope->hover({-1, -1});
      self.dirty = true;
    } else {
      self.nativeTracking = true;
      self.selectCursor(x, y);
      if (!self.menuOpen) self.scope->hover({x, y - barHeight});
      const auto b = self.button(x, y);
      if (b != self.hover) {
        self.hover = b;
        self.dirty = true;
      }
    }
  }
  int button(float x, float y) const {
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    if (menuOpen && y >= barHeight && y < barHeight + popupHeight() && x >= 0 &&
        x < std::min(w, popupWidth)) {
      for (const auto& action : menuActions)
        if ((action.button != itwsButton || !itwsSiteId.isEmpty()) &&
            actionRect(action.button, w, !itwsSiteId.isEmpty())
                .contains(int(x), int(y)))
          return action.button;
      return popupBackground;
    }
    if (y < 0 || y >= barHeight || x < 0 || x >= w) return -1;
    for (int id = Menu; id <= Forward10; ++id)
      if (titleButtonRect(id, w).contains(int(x), int(y))) return id;
    return -1;
  }
  static SDL_HitTestResult hit(SDL_Window* w, const SDL_Point* p, void* data) {
    auto& self = *static_cast<Impl*>(data);
    int width, height;
    SDL_GetWindowSize(w, &width, &height);
    if (self.menuOpen && self.button(p->x, p->y) >= settingsButton)
      return SDL_HITTEST_NORMAL;
    // Keep the entire toolbar clickable; resize from the sides/bottom.
    if (!(SDL_GetWindowFlags(w) & SDL_WINDOW_MAXIMIZED) && p->y >= barHeight) {
      if (p->y >= height - 5)
        return p->x < 5            ? SDL_HITTEST_RESIZE_BOTTOMLEFT
               : p->x >= width - 5 ? SDL_HITTEST_RESIZE_BOTTOMRIGHT
                                   : SDL_HITTEST_RESIZE_BOTTOM;
      if (p->x < 5) return SDL_HITTEST_RESIZE_LEFT;
      if (p->x >= width - 5) return SDL_HITTEST_RESIZE_RIGHT;
    }
    if (p->y < barHeight && p->x >= titleStart &&
        p->x < width - 3 * buttonWidth && self.button(p->x, p->y) < 0)
      return SDL_HITTEST_DRAGGABLE;
    return SDL_HITTEST_NORMAL;
  }
  void fitDisplay(SDL_Rect area, bool center) {
    const int minimumWidth = std::min(Dcb::maximumWidth(), area.w);
    require(SDL_SetWindowMaximumSize(window, 0, 0));
    require(
        SDL_SetWindowMinimumSize(window, minimumWidth, std::min(240, area.h)));
    require(SDL_SetWindowMaximumSize(window, area.w, area.h));
    int w, h, x, y;
    require(SDL_GetWindowSize(window, &w, &h));
    require(SDL_GetWindowPosition(window, &x, &y));
    w = std::clamp(w, minimumWidth, area.w);
    h = std::min(h, area.h);
    const int left = center ? area.x + (area.w - w) / 2
                            : std::clamp(x, area.x, area.x + area.w - w);
    const int top = center ? area.y + (area.h - h) / 2
                           : std::clamp(y, area.y, area.y + area.h - h);
    if (center || left != x || top != y)
      require(SDL_SetWindowPosition(window, left, top));
  }
  void fitCurrentDisplay() {
    SDL_Rect area{};
    if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window), &area))
      fitDisplay(area, false);
  }
  void maximize() {
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED)
      SDL_RestoreWindow(window);
    else
      SDL_MaximizeWindow(window);
    dirty = true;
  }
  void event(const SDL_Event& e) {
    if (closing) return;
    if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
      closing = true;
      QCoreApplication::quit();
    } else if (e.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED) {
      fitCurrentDisplay();
      dirty = true;
    } else if (e.type == SDL_EVENT_KEY_UP &&
               (e.key.key == SDLK_F1 || e.key.key == SDLK_LCTRL ||
                e.key.key == SDLK_RCTRL))
      scope->setBeaconReadout(false);
    else if (e.type == SDL_EVENT_KEY_DOWN) {
      if (e.key.key == SDLK_F1 && (e.key.mod & SDL_KMOD_CTRL)) {
        scope->setBeaconReadout(true);
        return;
      }
#ifdef __APPLE__
      constexpr auto shortcutMod = SDL_KMOD_GUI;
#else
      constexpr auto shortcutMod = SDL_KMOD_CTRL;
#endif
      if (e.key.key == SDLK_S && (e.key.mod & shortcutMod) &&
          (e.key.mod & SDL_KMOD_SHIFT) && !(e.key.mod & SDL_KMOD_ALT)) {
        if (!e.key.repeat) openSettings();
        return;
      }
      if (e.key.key == SDLK_S && (e.key.mod & shortcutMod) &&
          (e.key.mod & SDL_KMOD_ALT)) {
        if (!e.key.repeat) openRecording();
        return;
      }
      if (!itwsSiteId.isEmpty() && e.key.key == SDLK_W &&
          (e.key.mod & SDL_KMOD_ALT) && (e.key.mod & SDL_KMOD_SHIFT) &&
          !(e.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL))) {
        if (!e.key.repeat) toggleItws();
        return;
      }
      if (e.key.key == SDLK_L && (e.key.mod & shortcutMod) &&
          (e.key.mod & SDL_KMOD_SHIFT) && !(e.key.mod & SDL_KMOD_ALT)) {
        if (!e.key.repeat) toggleLeaderDirection();
        return;
      }
      if (e.key.key == SDLK_A && (e.key.mod & shortcutMod) &&
          (e.key.mod & SDL_KMOD_SHIFT) && !(e.key.mod & SDL_KMOD_ALT)) {
        if (!e.key.repeat) toggleAssignedAltitudes();
        return;
      }
      if (e.key.key == SDLK_P && (e.key.mod & shortcutMod) &&
          (e.key.mod & SDL_KMOD_SHIFT) && !(e.key.mod & SDL_KMOD_ALT)) {
        if (!e.key.repeat) switchProfile();
        return;
      }
      if (menuOpen) {
        if (e.key.key == SDLK_ESCAPE) setMenuOpen(false);
        if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER ||
            e.key.key == SDLK_SPACE)
          openSettings();
        return;
      }
      const auto modifiers = e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT |
                                          SDL_KMOD_GUI | SDL_KMOD_SHIFT);
      if ((e.key.key == SDLK_F8 && (modifiers & SDL_KMOD_CTRL) &&
           !(modifiers & ~SDL_KMOD_CTRL)) ||
          (e.key.key == SDLK_F11 && !modifiers)) {
        if (!e.key.repeat) scope->key(e.key.key == SDLK_F8 ? "Ctrl+F8" : "F11");
        return;
      }
      if (e.key.key == SDLK_F12 && (e.key.mod & SDL_KMOD_CTRL)) {
        if (!e.key.repeat) scope->key("Ctrl+F12");
        return;
      }
      if (e.key.key == SDLK_F9 && (e.key.mod & SDL_KMOD_CTRL)) {
        if (!e.key.repeat) toggleDcb();
        return;
      }
      const auto character =
          SDL_GetKeyFromScancode(e.key.scancode, e.key.mod, false);
      if (e.key.key == SDLK_F7 &&
          !(e.key.mod &
            (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI | SDL_KMOD_SHIFT))) {
        if (!e.key.repeat) scope->key("F7");
      } else if (character == SDLK_GRAVE || character == 0x0300)
        scope->key("`");
      else if (e.key.key == SDLK_KP_MULTIPLY || character == '*')
        scope->key("*");
      else if (e.key.key == SDLK_KP_PLUS || character == '+')
        scope->key("+");
      else if (e.key.key == SDLK_KP_PERIOD)
        scope->key(".");
      else if (e.key.key >= SDLK_0 && e.key.key <= SDLK_9)
        scope->key(QString(QChar('0' + e.key.key - SDLK_0)));
      else if (e.key.key >= SDLK_KP_1 && e.key.key <= SDLK_KP_9)
        scope->key(QString(QChar('1' + e.key.key - SDLK_KP_1)));
      else if (e.key.key == SDLK_KP_0)
        scope->key("0");
      else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER)
        scope->key("Enter");
      else if (e.key.key == SDLK_F12 && !modifiers)
        scope->key("F12");
      else if (e.key.key == SDLK_END)
        scope->key("End");
      else if (e.key.key == SDLK_ESCAPE)
        scope->key("Escape");
      else if (e.key.key == SDLK_BACKSPACE)
        scope->key("Backspace");
      else if (e.key.key >= 32 && e.key.key <= 126)
        scope->key(QString(QChar(e.key.key)));
    } else if (e.type == SDL_EVENT_MOUSE_MOTION) {
      int w, h;
      SDL_GetWindowSize(window, &w, &h);
      if (!menuOpen)
        scope->motion(
            {e.motion.x, e.motion.y - barHeight},
            {e.motion.xrel, e.motion.yrel}, {double(w), double(h - barHeight)},
            e.motion.state & SDL_BUTTON_RMASK,
            e.motion.x >= 5 && e.motion.x < w - 5 && e.motion.y < h - 5);
      auto b = button(e.motion.x, e.motion.y);
      if (b != hover) {
        hover = b;
        dirty = true;
      }
    } else if (e.type == SDL_EVENT_WINDOW_MOUSE_LEAVE ||
               e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
      if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        setMenuOpen(false);
        scope->setBeaconReadout(false);
      }
      hover = -1;
      scope->cancelPointer();
      pressed = -1;
      dirty = true;
    } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
      int w, h;
      SDL_GetWindowSize(window, &w, &h);
      if (menuOpen) return;
      const float wheel =
          e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
      scope->wheel({e.wheel.mouse_x, e.wheel.mouse_y - barHeight}, wheel,
                   SDL_GetModState() & SDL_KMOD_CTRL,
                   {double(w), double(h - barHeight)});
    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
               e.button.button == SDL_BUTTON_LEFT) {
      int w, h;
      SDL_GetWindowSize(window, &w, &h);
      pressed = button(e.button.x, e.button.y);
      if (pressed >= Replay10 && pressed <= Forward10 && playback.loading()) {
        pressed = -1;
        return;
      }
      if (menuOpen && pressed != Menu && e.button.y < barHeight)
        setMenuOpen(false);
      if (menuOpen && e.button.y >= barHeight) {
        if (pressed < settingsButton) setMenuOpen(false);
        return;
      }
      try {
        scope->press({e.button.x, e.button.y - barHeight},
                     {double(w), double(h - barHeight)});
      } catch (const std::exception& error) {
        QMessageBox::warning(nullptr, tr("Could Not Display Map"),
                             QString::fromUtf8(error.what()));
      }
      if (e.button.clicks == 2 && e.button.y < barHeight && pressed < 0)
        maximize();
    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP &&
               e.button.button == SDL_BUTTON_LEFT) {
      int w, h;
      SDL_GetWindowSize(window, &w, &h);
      if (!menuOpen)
        scope->release({e.button.x, e.button.y - barHeight},
                       {double(w), double(h - barHeight)});
      int b = button(e.button.x, e.button.y);
      if (b == pressed) {
        if (b == Menu) setMenuOpen(!menuOpen);
        if (b == settingsButton) openSettings();
        if (b >= originalButton && b <= hz2Button) {
          scope->setReconstructionMode(
              std::array{ReconstructionMode::Original, ReconstructionMode::Hz1,
                         ReconstructionMode::Hz2}[b - originalButton]);
          dirty = true;
        }
        if (b == itwsButton) toggleItws();
        if (b == leaderButton) toggleLeaderDirection();
        if (b == altitudeButton) toggleAssignedAltitudes();
        if (b == recordingButton) openRecording();
        if (b == switchProfileButton) switchProfile();
        if (b == Minimize) SDL_MinimizeWindow(window);
        if (b == Maximize) maximize();
        const auto now = QDateTime::currentSecsSinceEpoch();
        if (b == Replay10 || b == Forward10) {
          const int seconds = b == Replay10 ? -10 : 10;
          if (playback.canSeek(seconds, now)) {
            playback.seek(seconds, now);
            scope->resetTraffic();
            scope->resetWeather();
            emit owner->seekRequested(seconds);
          }
        }
        if (b == Play && !playback.loading()) {
          if (playback.selected()) {
            playback.toggle(now);
            dirty = true;
            emit owner->playRequested();
          } else
            openRecording();
        }
        if (b == Close) {
          closing = true;
          QCoreApplication::quit();
        }
      }
      pressed = -1;
    } else if (e.type >= SDL_EVENT_WINDOW_FIRST &&
               e.type <= SDL_EVENT_WINDOW_LAST)
      dirty = true;
  }
  QImage toolbar(int width, int pixelWidth) {
    const double scale = double(pixelWidth) / width;
    QImage image(
        pixelWidth,
        std::max(1, int(std::lround(
                        (barHeight + (menuOpen ? popupHeight() : 0)) * scale))),
        QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.scale(scale, scale);
    painter.fillRect(QRect(0, 0, width, barHeight), QColor(20, 20, 20));
    const bool disabled = (hover == Replay10 && !backEnabled) ||
                          (hover == Play && !playEnabled) ||
                          (hover == Forward10 && !forwardEnabled);
    if (hover >= 0 && hover < settingsButton && !disabled)
      painter.fillRect(titleButtonRect(hover, width),
                       hover == 3 ? QColor(232, 17, 35) : QColor(37, 37, 37));
    painter.setPen(Qt::white);
    auto font = QApplication::font();
    font.setPixelSize(13);
    painter.setFont(font);
    const int textWidth =
        std::max(0, width - titleStart - 6 - 3 * buttonWidth - 6);
    auto nameFont = interBlackFont();
    nameFont.setPixelSize(font.pixelSize());
    painter.setFont(nameFont);
    const QFontMetrics nameMetrics(nameFont);
    const int nameWidth =
        std::min(textWidth, nameMetrics.horizontalAdvance(name));
    painter.drawText(QRect(titleStart + 6, 0, nameWidth, barHeight),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     nameMetrics.elidedText(name, Qt::ElideRight, textWidth));
    painter.setFont(font);
    const int remaining = textWidth - nameWidth;
    painter.drawText(
        QRect(titleStart + 6 + nameWidth, 0, remaining, barHeight),
        Qt::AlignVCenter | Qt::AlignLeft,
        QFontMetrics(font).elidedText(suffix, Qt::ElideRight, remaining));
    auto symbols = materialSymbolsFont();
    symbols.setPixelSize(12);
    painter.setFont(symbols);
    painter.drawText(titleButtonRect(Menu, width), Qt::AlignCenter,
                     QString(QChar(0xE5D2)));
    painter.setPen(backEnabled ? Qt::white : Qt::gray);
    painter.drawText(titleButtonRect(Replay10, width), Qt::AlignCenter,
                     QString(QChar(0xE059)));
    painter.setPen(forwardEnabled ? Qt::white : Qt::gray);
    painter.drawText(titleButtonRect(Forward10, width), Qt::AlignCenter,
                     QString(QChar(0xE056)));
    auto playFont = font;
    playFont.setPixelSize(symbols.pixelSize());
    painter.setPen(playEnabled ? Qt::white : Qt::gray);
    painter.setFont(playback.playing() ? symbols : playFont);
    painter.drawText(titleButtonRect(Play, width), Qt::AlignCenter,
                     QString(QChar(playback.playing() ? 0xE034 : 0x25B6)));
    painter.setPen(Qt::white);
    // Match REDS' 8px window-control geometry (rather than font-dependent
    // bearings).
    painter.setRenderHint(QPainter::Antialiasing, false);
    for (int b = 1; b <= 3; ++b) {
      const int x = width - (4 - b) * buttonWidth + (buttonWidth - 8) / 2,
                y = (barHeight - 8) / 2;
      if (b == 1)
        painter.drawLine(x, y + 7, x + 7, y + 7);
      else if (b == 2) {
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) {
          painter.drawRect(x + 2, y, 5, 5);
          painter.fillRect(
              x, y + 2, 6, 6,
              hover == 2 ? QColor(37, 37, 37) : QColor(20, 20, 20));
          painter.drawRect(x, y + 2, 5, 5);
        } else
          painter.drawRect(x, y, 7, 7);
      } else {
        painter.drawLine(x, y, x + 7, y + 7);
        painter.drawLine(x + 7, y, x, y + 7);
      }
    }
    if (menuOpen) {
      const QRect popup(0, barHeight, std::min(width, popupWidth),
                        popupHeight());
      QPainterPath outline;
      outline.addRoundedRect(QRectF(popup).adjusted(.5, .5, -.5, -.5), 6, 6);
      painter.setRenderHint(QPainter::Antialiasing);
      painter.fillPath(outline, QColor(61, 61, 76));
      auto headingFont = interBlackFont();
      headingFont.setPixelSize(font.pixelSize() - 1);
      for (int item : {settingsButton, recordingButton}) {
        const int top = sectionTop(item);
        painter.setFont(headingFont);
        painter.setPen(QColor(240, 240, 240));
        painter.drawText(
            QRect(0, top, popup.width(), sectionHeading),
            Qt::AlignHCenter | Qt::AlignBottom,
            item == settingsButton ? tr("Adaptation") : tr("Settings"));
        // REDS inset separator: a one-pixel rule with four pixels above and
        // below.
        painter.fillRect(12, top + sectionHeading + 4, popup.width() - 24, 1,
                         QColor(255, 255, 255, 102));
      }
      for (const auto& action : menuActions) {
        if (action.button == itwsButton && itwsSiteId.isEmpty()) continue;
        const QRect row =
            actionRect(action.button, width, !itwsSiteId.isEmpty());
        painter.setFont(font);
        if (hover == action.button) {
          painter.save();
          painter.setClipRect(row);
          painter.fillPath(outline, QColor(76, 79, 98));
          painter.restore();
        }
        painter.setPen(QColor(240, 240, 240));
        if ((action.button == altitudeButton &&
             scope->assignedAltitudesHidden()) ||
            (action.button == leaderButton &&
             scope->useTaisLeaderDirection()) ||
            (action.button == itwsButton && itws) ||
            (action.button == originalButton &&
             scope->reconstructionMode() == ReconstructionMode::Original) ||
            (action.button == hz1Button &&
             scope->reconstructionMode() == ReconstructionMode::Hz1) ||
            (action.button == hz2Button &&
             scope->reconstructionMode() == ReconstructionMode::Hz2))
          painter.drawText(row.adjusted(12, 0, 0, 0),
                           Qt::AlignLeft | Qt::AlignVCenter,
                           QStringLiteral("✓"));
        painter.drawText(row.adjusted(40, 0, 0, 0),
                         Qt::AlignLeft | Qt::AlignVCenter, tr(action.label));
        if (action.shortcut.key() == Qt::Key_unknown) continue;
        painter.setPen(QColor(190, 190, 200));
        const auto shortcut =
            QKeySequence(action.shortcut).toString(QKeySequence::NativeText);
        painter.drawText(row.adjusted(0, 0, -14, 0),
                         Qt::AlignRight | Qt::AlignVCenter, shortcut);
      }
      painter.strokePath(outline, QPen(QColor(74, 74, 94)));
    }
    return image;
  }
  renderer::CmdBuffer commands() {
    int w, h, pw, ph;
    require(SDL_GetWindowSize(window, &w, &h));
    require(SDL_GetWindowSizeInPixels(window, &pw, &ph));
    renderer::CmdBuffer cb;
    cb.clearRGB({0, 0, 0});
    if (w <= 0 || h <= 0 || pw <= 0 || ph <= 0) return cb;
    const auto now = QDateTime::currentSecsSinceEpoch();
    if (playback.selected() && h > barHeight) {
      const QSize size(w, h - barHeight);
      if (size != weatherSize || scope->center() != weatherCenter ||
          scope->range() != weatherRange) {
        weatherSize = size;
        weatherCenter = scope->center();
        weatherRange = scope->range();
        weatherBounds = scope->weatherBounds(size);
      }
      const auto playbackMs = playback.currentMs(now);
      const auto position = playbackMs / 1000;
      tais.prepare(position, net::Traffic::ownershipLookbehindSeconds);
      if (tais.configured()) scope->updateTraffic(tais, playbackMs);
      scope->setAltimeters(metars->sample(position), position);
      const auto sample =
          itws ? itws->sample(playbackMs)
               : weather->sample(position, weatherBounds, playback.playing());
      scope->setWeather(sample.pictures);
      if (playback.loading() && sample.ready &&
          (!tais.configured() || scope->trafficInitialized()))
        playback.dataReady();
    }
    if (scope)
      alarm.update(scope->previewingVolume() ||
                       (playback.playing() && scope->msawAudioWanted()),
                   scope->preferences().audioVolume);
    const auto second = playback.selected() ? playback.position(now) : -1;
    if (second != displayedSecond) {
      scope->setRecordingTime(
          second < 0 ? QString{}
                     : QDateTime::fromSecsSinceEpoch(second, QTimeZone::utc())
                           .toString("hhmm/ss"));
      displayedSecond = second;
    }
    const bool back = playback.canSeek(-10, now),
               forward = playback.canSeek(10, now);
    const bool play = !playback.loading();
    if (back != backEnabled || play != playEnabled ||
        forward != forwardEnabled) {
      backEnabled = back;
      playEnabled = play;
      forwardEnabled = forward;
      dirty = true;
    }
    scope->draw(cb, useFontB ? fontB : fontA, w, h, pw, ph, barHeight);
    if (dirty || lastSize != QSize(pw, ph)) {
      auto image = toolbar(w, pw);
      auto pixels =
          std::span<const std::uint8_t>(image.constBits(), image.sizeInBytes());
      if (texture)
        output->gpu().update(texture, image.width(), image.height(),
                             SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, pixels);
      else
        texture =
            output->gpu().texture(image.width(), image.height(),
                                  SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, pixels);
      lastSize = {pw, ph};
      dirty = false;
    }
    cb.loadProjectionMatrix(renderer::screenOrtho(float(pw), float(ph)));
    cb.setRGB({1, 1, 1});
    const float bottom = float(std::lround(
        double(barHeight + (menuOpen ? popupHeight() : 0)) * pw / w));
    renderer::TexturedTrianglesBuilder b;
    b.addQuad({0, 0}, {0, 0}, {float(pw), 0}, {1, 0}, {float(pw), bottom},
              {1, 1}, {0, bottom}, {0, 1});
    b.generateCommands(cb, texture);
    return cb;
  }
  void tick() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) {
        QCoreApplication::quit();
        return;
      } else if (auto* window = SDL_GetWindowFromEvent(&e)) {
        if (auto it = windows.find(SDL_GetWindowID(window));
            it != windows.end())
          it->second->event(e);
      }
    }
    if (!closing) output->tryDraw(commands());
  }
};
std::map<SDL_WindowID, Pane::Impl*> Pane::Impl::windows;
Pane::Impl* Pane::Impl::cursorOwner = nullptr;
Pane::Pane(const QString& title, QObject* parent, const QString& position,
           const QString& tcp, const QString& suffix)
    : QObject(parent),
      impl_(std::make_unique<Impl>(this, title, suffix,
                                   position.section('/', 0, 0))) {
  auto& p = *impl_;
  p.scope = std::make_unique<STARSPane>(position, tcp);
  p.itwsSiteId =
      itwsSite(position.section('/', 0, 0), p.scope->positionFacility());
  p.weather = std::make_unique<Weather>(p.scope->artcc());
  p.metars =
      std::make_unique<MetarArchive>(p.scope->facilityAltimeterStations());
  setAdaptation({}, {});
  require(SDL_InitSubSystem(SDL_INIT_VIDEO));
  p.initialized = true;
  const int side = Dcb::maximumWidth();
  float mouseX, mouseY;
  SDL_GetGlobalMouseState(&mouseX, &mouseY);
  const SDL_Point pointer{int(mouseX), int(mouseY)};
  const auto display = SDL_GetDisplayForPoint(&pointer);
  SDL_Rect area{};
  require(SDL_GetDisplayUsableBounds(
      display ? display : SDL_GetPrimaryDisplay(), &area));
  const int width = std::min(side, area.w);
  const int height = std::min(width + barHeight, area.h);
  p.window = SDL_CreateWindow(
      (title + suffix).toUtf8().constData(), width, height,
      SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_BORDERLESS |
          SDL_WINDOW_HIGH_PIXEL_DENSITY);
  require(p.window);
  p.fitDisplay(area, true);
  require(SDL_ShowWindow(p.window));
  require(SDL_SetWindowHitTest(p.window, Impl::hit, &p));
  p.cursors = std::make_unique<Cursors>();
  require(SDL_ShowCursor());
  require(SDL_AddEventWatch(Impl::cursorEvent, &p));
  p.watching = true;
  p.nativeMouse =
      std::make_unique<NativeMouse>(p.window, &p, Impl::nativeMotion,
                                    [](void* context, const QString& entry) {
                                      auto& host = *static_cast<Impl*>(context);
                                      if (entry == "SwitchProfile") {
                                        host.switchProfile();
                                        return true;
                                      }
                                      if (entry == "SetRecordingStart") {
                                        host.openRecording();
                                        return true;
                                      }
                                      if (entry == "OpenAdaptation") {
                                        host.openSettings();
                                        return true;
                                      }
                                      if (entry == "ToggleItws") {
                                        if (host.itwsSiteId.isEmpty())
                                          return false;
                                        host.toggleItws();
                                        return true;
                                      }
                                      if (entry == "ToggleLeaderDirection") {
                                        host.toggleLeaderDirection();
                                        return true;
                                      }
                                      if (entry == "ToggleAssignedAltitudes") {
                                        host.toggleAssignedAltitudes();
                                        return true;
                                      }
                                      if (host.menuKey(entry)) return true;
                                      if (entry == "Ctrl+F9") {
                                        host.toggleDcb();
                                        return true;
                                      }
                                      auto& pane = *host.scope;
                                      return pane.key(entry);
                                    });
  p.device = SDL_CreateGPUDevice(renderer::Gpu::shaderFormats, false, nullptr);
  require(p.device);
  p.output = std::make_unique<renderer::Window>(p.device, p.window);
  p.output->useTimerPacing();
  p.fontB.upload(p.output->gpu());
  p.fontA.upload(p.output->gpu());
  configureFont(true, {});
  configureStipple(false, {});
  Impl::windows.emplace(SDL_GetWindowID(p.window), &p);
  connect(&p.timer, &QTimer::timeout, this, [this] {
    try {
      impl_->tick();
    } catch (const std::exception& e) {
      impl_->timer.stop();
      impl_->closing = true;
      QMessageBox::critical(nullptr, tr("Could Not Render STARS"),
                            QString::fromUtf8(e.what()));
      deleteLater();
    }
  });
  p.timer.setTimerType(Qt::PreciseTimer);
  p.timer.start(16);
}
void Pane::setAdaptation(const ColorOverrides& overrides,
                         std::function<void(const ColorOverrides&)> save) {
  const auto& defaults = impl_->scope->defaultPalette();
  const auto palette = adaptedPalette(defaults, overrides);
  if (impl_->cursors) impl_->cursors->setScopeColor(palette.Cursor);
  impl_->scope->setPalette(palette);
  impl_->adaptation.configure(
      paletteColors(defaults), overrides,
      [this, save = std::move(save)](const ColorOverrides& changes,
                                     const ColorOverrides& updated) {
        const auto palette =
            adaptedPalette(impl_->scope->defaultPalette(), updated);
        impl_->cursors->setScopeColor(palette.Cursor);
        try {
          if (save) save(changes);
        } catch (...) {
          impl_->cursors->setScopeColor(impl_->scope->palette().Cursor);
          throw;
        }
        impl_->scope->setPalette(palette);
      });
}
void Pane::configurePrefSets(const QJsonObject& sets, SavePrefSets save) {
  impl_->scope->configurePrefSets(sets, std::move(save));
}
void Pane::configureSymbols(QStringList selected,
                            std::function<void(const QStringList&)> save) {
  impl_->scope->setSubsetSymbols(selected);
  impl_->adaptation.configureSymbols(
      impl_->scope->positionCodes(), selected,
      [this, save = std::move(save)](const QStringList& codes) {
        if (save) save(codes);
        impl_->scope->setSubsetSymbols(codes);
      });
}
void Pane::configureStipple(bool enabled, std::function<void(bool)> save) {
  impl_->scope->setFaaStipple(enabled);
  impl_->adaptation.configureStipple(
      enabled, [this, save = std::move(save)](bool selected) {
        if (save) save(selected);
        impl_->scope->setFaaStipple(selected);
      });
}
void Pane::configureFont(bool setB, std::function<void(bool)> save) {
  impl_->useFontB = setB;
  impl_->adaptation.configureFont(
      setB, [this, save = std::move(save)](bool selectedB) {
        if (save) save(selectedB);
        impl_->useFontB = selectedB;
      });
}
Pane::~Pane() = default;
QString Pane::title() const { return impl_->name + impl_->suffix; }
const net::TaisCache& Pane::traffic() const { return impl_->tais; }
void Pane::raise() {
  if (SDL_GetWindowFlags(impl_->window) & SDL_WINDOW_MINIMIZED)
    SDL_RestoreWindow(impl_->window);
  impl_->fitCurrentDisplay();
  SDL_RaiseWindow(impl_->window);
}
QImage Pane::snapshot() {
  auto& p = *impl_;
  auto cb = p.commands();
  auto size = p.output->pixels();
  SDL_GPUTextureCreateInfo info{};
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
  info.width = Uint32(size.x);
  info.height = Uint32(size.y);
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  auto* texture = SDL_CreateGPUTexture(p.device, &info);
  require(texture);
  SDL_GPUCommandBuffer* command = nullptr;
  try {
    command = SDL_AcquireGPUCommandBuffer(p.device);
    require(command);
    p.output->gpu().encode(
        command, texture, info.format, info.width, info.height,
        p.output->gpu().prepare(cb, info.width, info.height));
    auto* submitted = command;
    command = nullptr;
    require(SDL_SubmitGPUCommandBuffer(submitted));
    auto pixels =
        p.output->gpu().read(texture, info.format, info.width, info.height,
                             {0, 0, int(info.width), int(info.height)});
    QImage result(pixels.data(), info.width, info.height,
                  QImage::Format_RGBA8888);
    auto copy = result.mirrored(false, true);
    SDL_ReleaseGPUTexture(p.device, texture);
    return copy;
  } catch (...) {
    if (command) SDL_CancelGPUCommandBuffer(command);
    SDL_ReleaseGPUTexture(p.device, texture);
    throw;
  }
}
}  // namespace stars::ui
