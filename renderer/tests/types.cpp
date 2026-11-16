#include "matrix.h"
#include "rgb.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace stars::renderer;

void close(float actual, float expected) {
    if (std::abs(actual - expected) > 0.00001f) throw std::runtime_error("Renderer value differs from REDS contract");
}

int main() {
    try {
        const auto c = rgbaHex(0x0080ff);
        close(c.r, 0); close(c.g, 128.f / 255.f); close(c.b, 1); close(c.a, 1);
        close(rgba8(255, 0, 128, 64).a, 64.f / 255.f);
        const auto m = screenOrtho(1200, 800);
        const auto point = [&](float x, float y, float wantX, float wantY) {
            close(m[0] * x + m[4] * y + m[12], wantX);
            close(m[1] * x + m[5] * y + m[13], wantY);
        };
        point(0, 0, -1, 1);
        point(1200, 800, 1, -1);
        point(600, 400, 0, 0);
        if (screenOrtho(0, 800) != identity() || screenOrtho(1200, -1) != identity())
            throw std::runtime_error("Minimized window must have a valid identity projection");
        const auto depth = ortho(-5, 5, -10, 10, 2, 12);
        close(depth[10] * -2 + depth[14], -1);
        close(depth[10] * -12 + depth[14], 1);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
