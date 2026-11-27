#pragma once
#include "../renderer/font.h"
#include "../renderer/gpu.h"

namespace stars {
// STARS font selection and REDS-compatible atlas layout. Set B is the default.
renderer::FontSize loadFont(bool setB = true, unsigned size = 1);
struct Fonts {
  std::array<renderer::FontSize, 6> sizes;
  std::array<renderer::TextureID, 6> textures{};
  std::array<renderer::FontSize, 6> outlines;
  std::array<renderer::TextureID, 6> outlineTextures{};
  explicit Fonts(bool setB = true);
  void upload(renderer::Gpu& gpu);
};
}  // namespace stars
