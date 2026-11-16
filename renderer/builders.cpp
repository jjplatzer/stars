#include "builders.h"
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace stars::renderer {
namespace {
template<class V> std::uint32_t base(const std::vector<V>& vertices, std::size_t count) {
    constexpr auto max = std::numeric_limits<std::uint32_t>::max();
    if (count > max || vertices.size() > max - count) throw std::length_error("Geometry exceeds index range");
    return static_cast<std::uint32_t>(vertices.size());
}
PointVertex circlePoint(PointVertex center, float radius, int n, int segments) {
    const float angle = static_cast<float>(n) / static_cast<float>(segments) * 2 * std::numbers::pi_v<float>;
    return {center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)};
}
}
void LinesBuilder::addLine(PointVertex a, PointVertex b) {
    const auto i = base(vertices, 2);
    vertices.insert(vertices.end(), {a, b}); indices.insert(indices.end(), {i, i + 1});
}
void LinesBuilder::addLineStrip(std::span<const PointVertex> points) {
    if (points.size() < 2) return;
    const auto i = base(vertices, points.size());
    vertices.insert(vertices.end(), points.begin(), points.end());
    for (std::uint32_t n = 0; n < points.size() - 1; ++n) indices.insert(indices.end(), {i + n, i + n + 1});
}
void LinesBuilder::addLineLoop(std::span<const PointVertex> points) {
    if (points.size() < 2) return;
    const auto i = base(vertices, points.size());
    addLineStrip(points);
    indices.insert(indices.end(), {i + static_cast<std::uint32_t>(points.size()) - 1, i});
}
void LinesBuilder::addCircle(PointVertex center, float radius, int segments) {
    if (radius <= 0 || segments < 3) return;
    const auto i = base(vertices, segments);
    for (int n = 0; n < segments; ++n) vertices.push_back(circlePoint(center, radius, n, segments));
    for (int n = 0; n < segments; ++n) indices.insert(indices.end(), {i + n, i + (n + 1) % segments});
}
void ColoredLinesBuilder::addLine(PointVertex a, RGB ca, PointVertex b, RGB cb) {
    const auto i = base(vertices, 2);
    vertices.insert(vertices.end(), {{a.x, a.y, ca}, {b.x, b.y, cb}});
    indices.insert(indices.end(), {i, i + 1});
}
void TrianglesBuilder::addTriangle(PointVertex a, PointVertex b, PointVertex c) {
    const auto i = base(vertices, 3);
    vertices.insert(vertices.end(), {a, b, c}); indices.insert(indices.end(), {i, i + 1, i + 2});
}
void TrianglesBuilder::addQuad(PointVertex a, PointVertex b, PointVertex c, PointVertex d) {
    const auto i = base(vertices, 4);
    vertices.insert(vertices.end(), {a, b, c, d}); indices.insert(indices.end(), {i, i + 1, i + 2, i, i + 2, i + 3});
}
void TrianglesBuilder::addCircle(PointVertex center, float radius, int segments) {
    if (radius <= 0 || segments < 3) return;
    const auto i = base(vertices, static_cast<std::size_t>(segments) + 1);
    vertices.push_back(center);
    for (int n = 0; n < segments; ++n) vertices.push_back(circlePoint(center, radius, n, segments));
    for (int n = 0; n < segments; ++n) indices.insert(indices.end(), {i, i + 1 + n, i + 1 + (n + 1) % segments});
}
void TrianglesBuilder::addIndexed(std::span<const PointVertex> points, std::span<const std::uint32_t> elements) {
    if (points.empty() || elements.empty()) return;
    if (elements.size() % 3) throw std::invalid_argument("Incomplete triangles");
    for (auto index : elements) if (index >= points.size()) throw std::out_of_range("Triangle index exceeds vertex count");
    const auto i = base(vertices, points.size());
    vertices.insert(vertices.end(), points.begin(), points.end());
    for (auto index : elements) indices.push_back(i + index);
}
void ColoredTrianglesBuilder::addTriangle(PointVertex a, RGB ca, PointVertex b, RGB cb, PointVertex c, RGB cc) {
    const auto i = base(vertices, 3);
    vertices.insert(vertices.end(), {{a.x, a.y, ca}, {b.x, b.y, cb}, {c.x, c.y, cc}});
    indices.insert(indices.end(), {i, i + 1, i + 2});
}
void ColoredTrianglesBuilder::addQuad(PointVertex a, PointVertex b, PointVertex c, PointVertex d, RGB color) {
    const auto i = base(vertices, 4);
    vertices.insert(vertices.end(), {{a.x, a.y, color}, {b.x, b.y, color}, {c.x, c.y, color}, {d.x, d.y, color}});
    indices.insert(indices.end(), {i, i + 1, i + 2, i, i + 2, i + 3});
}
void TexturedTrianglesBuilder::addTriangle(PointVertex a, PointVertex ua, PointVertex b, PointVertex ub, PointVertex c, PointVertex uc) {
    const auto i = base(vertices, 3);
    vertices.insert(vertices.end(), {{a.x, a.y, ua.x, ua.y}, {b.x, b.y, ub.x, ub.y}, {c.x, c.y, uc.x, uc.y}});
    indices.insert(indices.end(), {i, i + 1, i + 2});
}
void TexturedTrianglesBuilder::addQuad(PointVertex a, PointVertex ua, PointVertex b, PointVertex ub, PointVertex c, PointVertex uc, PointVertex d, PointVertex ud) {
    const auto i = base(vertices, 4);
    vertices.insert(vertices.end(), {{a.x, a.y, ua.x, ua.y}, {b.x, b.y, ub.x, ub.y}, {c.x, c.y, uc.x, uc.y}, {d.x, d.y, ud.x, ud.y}});
    indices.insert(indices.end(), {i, i + 1, i + 2, i, i + 2, i + 3});
}
} // namespace stars::renderer
