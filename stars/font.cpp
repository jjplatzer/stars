#include "font.h"

#include <algorithm>

#include "../util/resources.h"

namespace stars {
using util::readCompressedResource;
namespace {
renderer::Font readFont(bool setB, bool outline) {
  const QString filename = outline ? (setB ? "ob" : "oa") : (setB ? "b" : "a");
  const auto bytes =
      readCompressedResource(":/resources/bmp/" + filename + ".bin.zst");
  return renderer::Font::parse(
      {reinterpret_cast<const std::uint8_t*>(bytes.constData()),
       std::size_t(bytes.size())});
}
renderer::FontSize strip(const renderer::FontSize& source) {
  auto font = source;
  // REDS packs the selected size into a horizontal strip. Retain the same
  // UV positions for its fractional, advance-centered DCB text.
  font.atlasWidth = 0;
  font.atlasHeight = 0;
  for (const auto& glyph : font.glyphs) {
    font.atlasWidth += glyph.width;
    font.atlasHeight = std::max(font.atlasHeight, glyph.height);
  }
  font.pixels.assign(font.atlasWidth * font.atlasHeight, 0);
  unsigned x = 0;
  for (auto& glyph : font.glyphs) {
    for (unsigned y = 0; y < glyph.height; ++y)
      std::copy_n(
          source.pixels.begin() + (glyph.y + y) * source.atlasWidth + glyph.x,
          glyph.width, font.pixels.begin() + y * font.atlasWidth + x);
    glyph.x = x;
    glyph.y = 0;
    x += glyph.width;
  }
  return font;
}
}  // namespace
renderer::FontSize loadFont(bool setB, unsigned size) {
  return strip(readFont(setB, false).sizes.at(size));
}
Fonts::Fonts(bool setB) {
  const auto regular = readFont(setB, false), outline = readFont(setB, true);
  for (unsigned i = 0; i < sizes.size(); ++i) {
    sizes[i] = strip(regular.sizes[i]);
    outlines[i] = strip(outline.sizes[i]);
  }
}
void Fonts::upload(renderer::Gpu& gpu) {
  for (unsigned i = 0; i < sizes.size(); ++i) {
    const auto& o = outlines[i];
    outlineTextures[i] = gpu.texture(o.atlasWidth, o.atlasHeight,
                                     SDL_GPU_TEXTUREFORMAT_R8_UNORM, o.pixels);
    const auto& font = sizes[i];
    textures[i] = gpu.texture(font.atlasWidth, font.atlasHeight,
                              SDL_GPU_TEXTUREFORMAT_R8_UNORM, font.pixels);
  }
}
}  // namespace stars
