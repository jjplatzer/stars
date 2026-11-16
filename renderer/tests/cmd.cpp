#include "builders.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace stars::renderer;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class E, class F> void rejects(F action) {
    try { action(); } catch (const E&) { return; }
    throw std::runtime_error("Expected rejection of invalid geometry");
}
int main() {
    try {
        CmdBuffer cb;
        LinesBuilder lines;
        lines.addLine({1, 2}, {3, 4});
        lines.generateCommands(cb);
        lines.reset(); // Recording must own its vertices, not reference builder storage.
        lines.addCircle({10, 20}, 3, 4);
        lines.generateCommands(cb);
        check(cb.points()[0].x == 1 && cb.points()[1].y == 4, "Builder reset changed recorded geometry");
        check(cb.commands()[1].vertexOffset == 2 && cb.commands()[1].indexOffset == 2, "Wrong batched draw offsets");
        check(cb.indices()[2] == 0 && cb.indices().back() == 0, "Local indices or circle closure changed");
        for (const auto& point : lines.vertices) check(std::abs(std::hypot(point.x - 10, point.y - 20) - 3) < 0.0001, "Circle radius changed");
        TrianglesBuilder triangles;
        triangles.addQuad({0, 0}, {2, 0}, {2, 2}, {0, 2});
        triangles.addCircle({0, 0}, 2, 8);
        float area = 0;
        for (std::size_t n = 0; n < triangles.indices.size(); n += 3) {
            const auto a = triangles.vertices[triangles.indices[n]];
            const auto b = triangles.vertices[triangles.indices[n + 1]];
            const auto c = triangles.vertices[triangles.indices[n + 2]];
            const auto cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            check(cross > 0, "Triangle winding changed"); area += cross / 2;
        }
        check(std::abs(area - (4 + 8 * std::sqrt(2.f))) < 0.0001, "Quad/circle triangulation has gaps or overlaps");
        triangles.generateCommands(cb, DrawMode::StippleRestrictionArea);
        check(cb.commands().back().mode == DrawMode::StippleRestrictionArea, "Lost STARS fill mode");
        const std::array<PointVertex, 3> points{{{0,0}, {1,0}, {0,1}}};
        const std::array<std::uint32_t, 3> invalid{0, 1, 3};
        const auto count = cb.commands().size();
        rejects<std::out_of_range>([&] { cb.drawTriangles(points, invalid); });
        rejects<std::invalid_argument>([&] { cb.drawLines(points, invalid); });
        check(cb.commands().size() == count, "Invalid batch altered recording");
        TexturedTrianglesBuilder texture;
        texture.addQuad({0,0}, {0,0}, {1,0}, {1,0}, {1,1}, {1,1}, {0,1}, {0,1});
        texture.generateCommands(cb, 42);
        check(cb.commands().back().texture == 42 && cb.texturedPoints()[2].v == 1, "Lost texture coordinates or handle");
        const std::array<FontVertex, 3> font{{{0,0,0,0,{1,0,0,1},{0,0,0,0}}, {1,0,1,0,{1,0,0,1},{0,0,0,0}}, {0,1,0,1,{1,0,0,1},{0,0,0,0}}}};
        const std::array<std::uint32_t, 3> valid{0,1,2};
        cb.drawFontTriangles(7, font, valid);
        check(cb.fontPoints()[0].color.r == 1 && cb.fontPoints()[0].background.a == 0, "Lost glyph compositing colors");
        CmdBuffer reused;
        reused.drawTriangles(points,valid);
        reused.drawTriangles(reused.points(),reused.indices());
        check(reused.points().size()==6 && reused.indices().size()==6 && reused.points()[4].x==1,
              "Reusing owned geometry invalidated recording input");
        auto child = std::make_shared<CmdBuffer>();
        child->setRGB({0,1,0});
        cb.call(child);
        child.reset();
        check(cb.commands().back().called->commands()[0].color.g == 1, "Static buffer lifetime not retained");
        auto a = std::make_shared<CmdBuffer>();
        auto b = std::make_shared<CmdBuffer>();
        a->resetState(); b->call(a);
        rejects<std::invalid_argument>([&] { a->call(b); });
        rejects<std::invalid_argument>([&] { a->call(a); });
        ZCmdBuffer layers;
        auto* stable = &layers.at(100);
        layers.at(-5).clear({0,0,0,1}); layers.at(20).setRGB({1,0,0});
        check(stable == &layers.at(100) && layers.buffers().begin()->first == -5, "Layer references/order changed");
        cb.reset();
        check(cb.empty() && cb.points().empty() && cb.indices().empty() && cb.fontPoints().empty(), "Reset retained stale geometry");
        layers.reset(); check(layers.empty(), "Layer reset retained a frame");
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
