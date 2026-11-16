#pragma once

#include "matrix.h"
#include "rgb.h"
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <vector>

namespace stars::renderer {

using TextureID = std::uint32_t;
using Z = int;
struct PointVertex { float x{}, y{}; };
struct ColoredVertex { float x{}, y{}; RGB color; };
struct TexturedVertex { float x{}, y{}, u{}, v{}; };
struct FontVertex { float x{}, y{}, u{}, v{}; RGBA color, background; };

// Only the fill modes used by REDS STARS. No ASDE-X/ERAM hatch/checker modes.
enum class DrawMode { Solid, StippleLight, StippleDense, StippleFAAHFSTD010A,
                      StippleRestrictionArea, StippleRestrictionAreaHighDPI };
enum class Op { Reset, Projection, Clear, Viewport, Scissor, DisableScissor,
                Blend, DisableBlend, Color, LineWidth, Lines, ColoredLines,
                Triangles, ColoredTriangles, TexturedTriangles, FontTriangles, Call };
struct Rect { int x{}, y{}, w{}, h{}; };
class CmdBuffer;
struct Command {
    Op op;
    Mat4 matrix{};
    RGBA color{};
    Rect rect{};
    float width{};
    TextureID texture{};
    std::uint32_t vertexOffset{}, vertexCount{}, indexOffset{}, indexCount{};
    DrawMode mode = DrawMode::Solid;
    std::shared_ptr<const CmdBuffer> called;
};

// Owns copied geometry. Indices remain local to each draw, as in REDS.
// Static child buffers are shared so a recorded call cannot dangle.
class CmdBuffer {
public:
    void reset();
    bool empty() const { return commands_.empty(); }
    void resetState();
    void loadProjectionMatrix(Mat4 matrix);
    void clear(RGBA color);
    void clearRGB(RGB color) { clear(color.rgba()); }
    void viewport(Rect rect);
    void scissor(Rect rect);
    void disableScissor();
    void blend();
    void disableBlend();
    void setRGBA(RGBA color);
    void setRGB(RGB color) { setRGBA(color.rgba()); }
    void lineWidth(float width);
    void drawLines(std::span<const PointVertex> vertices, std::span<const std::uint32_t> indices);
    void drawColoredLines(std::span<const ColoredVertex> vertices, std::span<const std::uint32_t> indices);
    void drawTriangles(std::span<const PointVertex> vertices, std::span<const std::uint32_t> indices, DrawMode mode = DrawMode::Solid);
    void drawColoredTriangles(std::span<const ColoredVertex> vertices, std::span<const std::uint32_t> indices);
    void drawTexturedTriangles(TextureID texture, std::span<const TexturedVertex> vertices, std::span<const std::uint32_t> indices);
    void drawFontTriangles(TextureID texture, std::span<const FontVertex> vertices, std::span<const std::uint32_t> indices);
    void call(std::shared_ptr<const CmdBuffer> other);

    std::span<const Command> commands() const { return commands_; }
    std::span<const PointVertex> points() const { return points_; }
    std::span<const ColoredVertex> coloredPoints() const { return colored_; }
    std::span<const TexturedVertex> texturedPoints() const { return textured_; }
    std::span<const FontVertex> fontPoints() const { return fonts_; }
    std::span<const std::uint32_t> indices() const { return indices_; }
private:
    template<class V> void draw(Command command, std::vector<V>& destination,
                               std::span<const V> vertices, std::span<const std::uint32_t> indices,
                               unsigned primitiveSize);
    std::vector<Command> commands_;
    std::vector<PointVertex> points_;
    std::vector<ColoredVertex> colored_;
    std::vector<TexturedVertex> textured_;
    std::vector<FontVertex> fonts_;
    std::vector<std::uint32_t> indices_;
};

// std::map keeps both deterministic low-to-high ordering and stable references.
class ZCmdBuffer {
public:
    CmdBuffer& at(Z z) { return buffers_[z]; }
    void reset() { buffers_.clear(); }
    bool empty() const { return buffers_.empty(); }
    const std::map<Z, CmdBuffer>& buffers() const { return buffers_; }
private:
    std::map<Z, CmdBuffer> buffers_;
};

} // namespace stars::renderer
