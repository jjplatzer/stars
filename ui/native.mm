#include "native.h"
#import <Cocoa/Cocoa.h>
#include <SDL3/SDL.h>
#include <stdexcept>

namespace stars::ui {
struct NativeMouse::Impl {
  id monitor = nil, resign = nil;
  NSWindow* window = nil;
  void* context;
  Hook hook;
  bool inside = false;
  void leave() {
    if (inside) {
      inside = false;
      hook(context, -1, -1);
    }
  }
  ~Impl() {
    if (monitor) [NSEvent removeMonitor:monitor];
    if (resign) [NSNotificationCenter.defaultCenter removeObserver:resign];
  }
};
NativeMouse::NativeMouse(SDL_Window* window, void* context, Hook hook, KeyHook key)
    : impl_(std::make_unique<Impl>()) {
  auto* state = impl_.get();
  state->context = context;
  state->hook = hook;
  state->window = (__bridge NSWindow*)SDL_GetPointerProperty(
      SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
  if (!state->window) throw std::runtime_error("Missing native STARS window");
  const auto mask = NSEventMaskKeyDown | NSEventMaskMouseMoved | NSEventMaskLeftMouseDragged |
                    NSEventMaskRightMouseDragged | NSEventMaskOtherMouseDragged |
                    NSEventMaskMouseEntered | NSEventMaskMouseExited;
  state->monitor = [NSEvent
      addLocalMonitorForEventsMatchingMask:mask
                                   handler:^NSEvent*(NSEvent* event) {
                                     if (event.type == NSEventTypeKeyDown) {
                                       if (event.window != state->window) return event;
                                       const auto modifiers =
                                           event.modifierFlags &
                                           (NSEventModifierFlagCommand | NSEventModifierFlagShift |
                                            NSEventModifierFlagOption | NSEventModifierFlagControl);
                                       if (modifiers == (NSEventModifierFlagOption |
                                                         NSEventModifierFlagShift) &&
                                           [[event charactersByApplyingModifiers:0].lowercaseString
                                               isEqualToString:@"w"]) {
                                         if (!event.isARepeat && key(state->context, "ToggleItws"))
                                           return nil;
                                         return event;
                                       }
                                       if (modifiers == (NSEventModifierFlagCommand |
                                                         NSEventModifierFlagShift) &&
                                           [event.charactersIgnoringModifiers.lowercaseString
                                               isEqualToString:@"s"]) {
                                         if (!event.isARepeat)
                                           key(state->context, "OpenAdaptation");
                                         return nil;
                                       }
                                       if (modifiers == (NSEventModifierFlagCommand |
                                                         NSEventModifierFlagOption) &&
                                           [event.charactersIgnoringModifiers.lowercaseString
                                               isEqualToString:@"s"]) {
                                         if (!event.isARepeat)
                                           key(state->context, "SetRecordingStart");
                                         return nil;
                                       }
                                       if (modifiers == (NSEventModifierFlagCommand |
                                                         NSEventModifierFlagShift) &&
                                           [event.charactersIgnoringModifiers.lowercaseString
                                               isEqualToString:@"p"]) {
                                         if (!event.isARepeat) key(state->context, "SwitchProfile");
                                         return nil;
                                       }
                                       if (modifiers == (NSEventModifierFlagCommand |
                                                         NSEventModifierFlagShift) &&
                                           [event.charactersIgnoringModifiers.lowercaseString
                                               isEqualToString:@"a"]) {
                                         if (!event.isARepeat)
                                           key(state->context, "ToggleAssignedAltitudes");
                                         return nil;
                                       }
                                       if (modifiers == (NSEventModifierFlagCommand |
                                                         NSEventModifierFlagShift) &&
                                           [event.charactersIgnoringModifiers.lowercaseString
                                               isEqualToString:@"l"]) {
                                         if (!event.isARepeat)
                                           key(state->context, "ToggleLeaderDirection");
                                         return nil;
                                       }
                                       if ((modifiers == NSEventModifierFlagControl &&
                                            event.keyCode == 100) ||
                                           (modifiers == 0 && event.keyCode == 103)) {
                                         if (!event.isARepeat)
                                           key(state->context,
                                               event.keyCode == 100 ? "Ctrl+F8" : "F11");
                                         return nil;
                                       }
                                       if (modifiers == NSEventModifierFlagControl &&
                                           event.keyCode == 111) {
                                         if (!event.isARepeat) key(state->context, "Ctrl+F12");
                                         return nil;
                                       }
                                       if ((modifiers & NSEventModifierFlagControl) &&
                                           event.keyCode == 101) {
                                         if (!event.isARepeat) key(state->context, "Ctrl+F9");
                                         return nil;
                                       }
                                       if (modifiers & (NSEventModifierFlagCommand |
                                                        NSEventModifierFlagControl))
                                         return event;
                                       if (modifiers == 0 && event.keyCode == 98) {
                                         if (!event.isARepeat) key(state->context, "F7");
                                         return nil;
                                       }
                                       QString entry;
                                       switch (event.keyCode) {
                                         case 111:
                                           entry = "F12";
                                           break;
                                         case 119:
                                           entry = "End";
                                           break;
                                         case 36:
                                         case 76:
                                           entry = "Enter";
                                           break;
                                         case 51:
                                           entry = "Backspace";
                                           break;
                                         case 53:
                                           entry = "Escape";
                                           break;
                                         default:
                                           entry = QString::fromNSString(event.characters);
                                           // A dead grave can have no text until a second key is
                                           // pressed. Ask the active layout for the symbol with
                                           // Shift/Option applied.
                                           if (entry.isEmpty() &&
                                               [[event
                                                   charactersByApplyingModifiers:event
                                                                                     .modifierFlags]
                                                   isEqualToString:@"`"])
                                             entry = QStringLiteral("`");
                                           break;
                                       }
                                       if (key(state->context, entry)) return nil;
                                       return event;
                                     }
                                     if (event.window == state->window) {
                                       const NSRect bounds = state->window.contentView.bounds;
                                       const NSPoint point = [state->window.contentView
                                           convertPoint:event.locationInWindow
                                               fromView:nil];
                                       if (NSPointInRect(point, bounds)) {
                                         const float y = state->window.contentView.flipped
                                                             ? point.y
                                                             : NSHeight(bounds) - point.y;
                                         state->inside = true;
                                         state->hook(state->context, point.x, y);
                                       } else
                                         state->leave();
                                     } else
                                       state->leave();
                                     return event;
                                   }];
  state->resign = [NSNotificationCenter.defaultCenter
      addObserverForName:NSApplicationDidResignActiveNotification
                  object:NSApp
                   queue:nil
              usingBlock:^(NSNotification*) {
                state->leave();
              }];
}
NativeMouse::~NativeMouse() = default;
}  // namespace stars::ui
