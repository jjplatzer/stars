#pragma once

#include <cstdint>

namespace stars::renderer {

// Straight (not premultiplied) color, matching REDS renderer/rgb.go.
struct RGBA { float r{}, g{}, b{}, a{}; };
struct RGB {
    float r{}, g{}, b{};
    constexpr RGBA rgba() const { return {r, g, b, 1}; }
};
constexpr RGB rgb8(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    constexpr float inv = 1.0f / 255.0f;
    return {r * inv, g * inv, b * inv};
}
constexpr RGBA rgba8(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    const auto c = rgb8(r, g, b);
    return {c.r, c.g, c.b, a * (1.0f / 255.0f)};
}
constexpr RGB rgbHex(std::uint32_t value) {
    return rgb8(static_cast<std::uint8_t>(value >> 16), static_cast<std::uint8_t>(value >> 8), static_cast<std::uint8_t>(value));
}
constexpr RGBA rgbaHex(std::uint32_t value) { return rgbHex(value).rgba(); }

} // namespace stars::renderer
