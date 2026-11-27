#include "cursors.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstring>
#include <stdexcept>

#include "../renderer/cursor.h"
#include "../util/resources.h"
#include "dcb.h"

namespace stars {
namespace {
void require(bool ok) {
  if (!ok) throw std::runtime_error(SDL_GetError());
}
SDL_Cursor* makeCursor(const renderer::Cursor& bitmap) {
  const auto surface = [&](int scale) {
    auto* image = SDL_CreateSurface(bitmap.width * scale, bitmap.height * scale,
                                    SDL_PIXELFORMAT_RGBA32);
    require(image);
    for (unsigned y = 0; y < bitmap.height * scale; ++y)
      for (unsigned x = 0; x < bitmap.width * scale; ++x)
        std::memcpy(
            static_cast<Uint8*>(image->pixels) + y * image->pitch + x * 4,
            bitmap.pixels.data() + ((y / scale) * bitmap.width + x / scale) * 4,
            4);
    return image;
  };
  auto* base = surface(1);
  try {
    // Exact nearest-neighbor images keep the bitmap crisp on Retina.
    for (int scale : {2, 3, 4}) {
      auto* image = surface(scale);
      const bool added = SDL_AddSurfaceAlternateImage(base, image);
      SDL_DestroySurface(image);
      require(added);
    }
    auto* result =
        SDL_CreateColorCursor(base, bitmap.hotspotX, bitmap.hotspotY);
    require(result);
    SDL_DestroySurface(base);
    return result;
  } catch (...) {
    SDL_DestroySurface(base);
    throw;
  }
}

SDL_Cursor* loadCursor(const QString& path, unsigned size,
                       renderer::RGB color = {1, 1, 1}) {
  const auto bytes = util::readCompressedResource(path);
  auto bitmap = renderer::Cursors::parse(
                    {reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                     std::size_t(bytes.size())})
                    .sizes[size];
  for (size_t i = 0; i < bitmap.pixels.size(); i += 4) {
    bitmap.pixels[i] = std::lround(bitmap.pixels[i] * color.r);
    bitmap.pixels[i + 1] = std::lround(bitmap.pixels[i + 1] * color.g);
    bitmap.pixels[i + 2] = std::lround(bitmap.pixels[i + 2] * color.b);
  }
  return makeCursor(bitmap);
}
}  // namespace
Cursors::Cursors() {
  dcb_.reset(loadCursor(":/resources/cur/dcb.bin.zst", 0));
  setScopeColor({1, 1, 1});
}
void Cursors::setScopeColor(renderer::RGB color) {
  if (scope_ && color.r == scopeColor_.r && color.g == scopeColor_.g &&
      color.b == scopeColor_.b)
    return;
  decltype(scope_) next(
      loadCursor(":/resources/cur/keyboard.bin.zst", 1, color),
      SDL_DestroyCursor);
  if (scope_ && SDL_GetCursor() == scope_.get())
    require(SDL_SetCursor(next.get()));
  scope_.swap(next);
  scopeColor_ = color;
}
SDL_Cursor* Cursors::at(QPointF mouse, QSizeF size, bool showDcb) const {
  if (mouse.x() < 0 || mouse.x() >= size.width() || mouse.y() < 0 ||
      mouse.y() >= size.height())
    return SDL_GetDefaultCursor();
  return showDcb && mouse.y() < Dcb::height ? dcb_.get() : scope_.get();
}
}  // namespace stars
