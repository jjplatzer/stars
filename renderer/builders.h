#pragma once
#include "cmd.h"

namespace stars::renderer {

template<class V> struct Geometry {
    std::vector<V> vertices;
    std::vector<std::uint32_t> indices;
    void reset() { vertices.clear(); indices.clear(); }
};
struct LinesBuilder : Geometry<PointVertex> {
    void addLine(PointVertex a, PointVertex b);
    void addLineStrip(std::span<const PointVertex> points);
    void addLineLoop(std::span<const PointVertex> points);
    void addCircle(PointVertex center, float radius, int segments);
    void generateCommands(CmdBuffer& cb) const { cb.drawLines(vertices, indices); }
};
struct ColoredLinesBuilder : Geometry<ColoredVertex> {
    void addLine(PointVertex a, RGB ca, PointVertex b, RGB cb);
    void addLineRGB(PointVertex a, PointVertex b, RGB color) { addLine(a, color, b, color); }
    void generateCommands(CmdBuffer& cb) const { cb.drawColoredLines(vertices, indices); }
};
struct TrianglesBuilder : Geometry<PointVertex> {
    void addTriangle(PointVertex a, PointVertex b, PointVertex c);
    void addQuad(PointVertex a, PointVertex b, PointVertex c, PointVertex d);
    void addCircle(PointVertex center, float radius, int segments);
    void addIndexed(std::span<const PointVertex> points, std::span<const std::uint32_t> elements);
    void generateCommands(CmdBuffer& cb, DrawMode mode = DrawMode::Solid) const { cb.drawTriangles(vertices, indices, mode); }
};
struct ColoredTrianglesBuilder : Geometry<ColoredVertex> {
    void addTriangle(PointVertex a, RGB ca, PointVertex b, RGB cb, PointVertex c, RGB cc);
    void addTriangleRGB(PointVertex a, PointVertex b, PointVertex c, RGB color) { addTriangle(a, color, b, color, c, color); }
    void addQuad(PointVertex a, PointVertex b, PointVertex c, PointVertex d, RGB color);
    void generateCommands(CmdBuffer& cb) const { cb.drawColoredTriangles(vertices, indices); }
};
struct TexturedTrianglesBuilder : Geometry<TexturedVertex> {
    void addTriangle(PointVertex a, PointVertex ua, PointVertex b, PointVertex ub, PointVertex c, PointVertex uc);
    void addQuad(PointVertex a, PointVertex ua, PointVertex b, PointVertex ub, PointVertex c, PointVertex uc, PointVertex d, PointVertex ud);
    void generateCommands(CmdBuffer& cb, TextureID texture) const { cb.drawTexturedTriangles(texture, vertices, indices); }
};
} // namespace stars::renderer
