#pragma once
#include <SDL3/SDL_mouse.h>

#include <QPointF>
#include <QSizeF>
#include <memory>

#include "../renderer/rgb.h"

namespace stars {
// Cached STARS bitmaps. Construct/destroy while the host's SDL video subsystem
// is alive; the host retains responsibility for native cursor ownership/focus.
class Cursors {
 public:
  Cursors();
  void setScopeColor(renderer::RGB color);
  SDL_Cursor* at(QPointF mouse, QSizeF size, bool showDcb = true) const;

 private:
  renderer::RGB scopeColor_{};
  std::unique_ptr<SDL_Cursor, decltype(&SDL_DestroyCursor)> dcb_{
      nullptr, SDL_DestroyCursor};
  std::unique_ptr<SDL_Cursor, decltype(&SDL_DestroyCursor)> scope_{
      nullptr, SDL_DestroyCursor};
};
}  // namespace stars
