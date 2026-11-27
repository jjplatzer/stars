#pragma once
#include <SDL3/SDL_video.h>

#include <QString>
#include <memory>

namespace stars::ui {
// Qt owns NSApplication on macOS. Observe its mouse dispatch before SDL's
// periodic pump; negative coordinates mean the mouse left this window.
class NativeMouse {
 public:
  using Hook = void (*)(void*, float, float);
  using KeyHook = bool (*)(void*, const QString&);
#ifdef __APPLE__
  NativeMouse(SDL_Window* window, void* context, Hook hook, KeyHook key);
  ~NativeMouse();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
#else
  NativeMouse(SDL_Window*, void*, Hook, KeyHook) {}
#endif
};
}  // namespace stars::ui
