#pragma once

#include "cmd.h"

namespace stars::renderer {

// GPU-ready clip coordinates. Input projections retain REDS' OpenGL convention;
// replay converts depth to SDL's [0,w] range. All rectangles here are top-left.
struct Vertex {
    float x{}, y{}, z{}, w{1}, u{}, v{};
    RGBA color{}, background{};
};
struct Batch {
    Rect viewport, scissor;
    bool blend = true;
    TextureID texture{};
    unsigned mode{}; // 0 solid, 1 texture, 2 font, 3..7 STARS stipple masks
    std::uint32_t first{}, count{};
};
// Logical counts match REDS, including commands culled by viewport/scissor.
// GPU counts describe the expanded triangle stream actually submitted.
struct Stats {
    std::size_t buffers{}, bufferBytes{}, drawCalls{}, points{}, lines{}, triangles{};
    std::size_t uploadBytes{}, gpuDrawCalls{};
    Stats& operator+=(const Stats& other) {
        buffers+=other.buffers; bufferBytes+=other.bufferBytes; drawCalls+=other.drawCalls;
        points+=other.points; lines+=other.lines; triangles+=other.triangles;
        uploadBytes+=other.uploadBytes; gpuDrawCalls+=other.gpuDrawCalls; return *this;
    }
};
struct Frame {
    Stats stats;
    std::vector<Vertex> vertices;
    std::vector<Batch> batches;
};

// Geometry is expanded to triangles, including wide lines (SDL GPU has no
// portable wide-line primitive). Nested calls and Z layers share render state.
Frame prepare(const CmdBuffer& commands, int width, int height);
Frame prepare(const ZCmdBuffer& commands, int width, int height);

} // namespace stars::renderer
