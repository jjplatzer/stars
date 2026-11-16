#pragma once

#include <array>

namespace stars::renderer {

// Retain REDS' column-major projection contract. Backend clip-space conversion
// belongs in backend replay, not in STARS' logical coordinate calculations.
using Mat4 = std::array<float, 16>;
constexpr Mat4 identity() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}
constexpr Mat4 ortho(float left, float right, float bottom, float top, float near, float far) {
    return {2 / (right - left), 0, 0, 0,
            0, 2 / (top - bottom), 0, 0,
            0, 0, -2 / (far - near), 0,
            -(right + left) / (right - left), -(top + bottom) / (top - bottom),
            -(far + near) / (far - near), 1};
}
constexpr Mat4 screenOrtho(float width, float height) {
    return width <= 0 || height <= 0 ? identity() : ortho(0, width, height, 0, -1, 1);
}

} // namespace stars::renderer
