#include "frame.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace stars::renderer {
namespace {
class Replay {
public:
    Replay(int w, int h) : width(w), height(h) {
        if (w <= 0 || h <= 0) throw std::invalid_argument("invalid render target dimensions");
        viewport = {0, 0, w, h};
        reset();
    }
    Frame result;
    void run(const CmdBuffer& buffer) {
        if (buffer.empty()) return;
        ++result.stats.buffers;
        if (active.size() >= 256 || !active.insert(&buffer).second)
            throw std::invalid_argument("recursive command buffer");
        for (const auto& c : buffer.commands()) {
            switch (c.op) {
            case Op::Reset: reset(); break;
            case Op::Projection: projection = c.matrix; break;
            case Op::Clear: clear(c.color); break;
            case Op::Viewport: viewport = topLeft(c.rect); break;
            case Op::Scissor: scissor = clip(topLeft(c.rect)); break;
            case Op::DisableScissor: scissor = {0, 0, width, height}; break;
            case Op::Blend: blend = true; break;
            case Op::DisableBlend: blend = false; break;
            case Op::Color: color = c.color; break;
            case Op::LineWidth: lineWidth = c.width; break;
            case Op::Call: if (c.called) run(*c.called); break;
            default: draw(buffer, c); break;
            }
        }
        active.erase(&buffer);
        result.stats.uploadBytes=result.vertices.size()*sizeof(Vertex);
        result.stats.gpuDrawCalls=result.batches.size();
    }
private:
    int width, height;
    Rect viewport{}, scissor{};
    Mat4 projection{};
    RGBA color{};
    bool blend{};
    float lineWidth{};
    std::unordered_set<const CmdBuffer*> active;

    void reset() {
        projection = identity(); color = {0, 0, 0, 1};
        blend = true; lineWidth = 1; scissor = {0, 0, width, height};
        // OpenGL resetState deliberately preserves the viewport.
    }
    Rect topLeft(Rect r) const {
        const auto y = std::int64_t(height) - r.y - r.h;
        if (y < std::numeric_limits<int>::min() || y > std::numeric_limits<int>::max())
            throw std::invalid_argument("rectangle overflow");
        return {r.x, int(y), r.w, r.h};
    }
    Rect clip(Rect r) const {
        auto x = std::clamp<std::int64_t>(r.x, 0, width);
        auto y = std::clamp<std::int64_t>(r.y, 0, height);
        auto right = std::clamp<std::int64_t>(std::int64_t(r.x) + r.w, x, width);
        auto bottom = std::clamp<std::int64_t>(std::int64_t(r.y) + r.h, y, height);
        return {int(x), int(y), int(right-x), int(bottom-y)};
    }
    Vertex vertex(float x, float y, RGBA fg, float u = 0, float v = 0, RGBA bg = {}) const {
        const auto& m = projection;
        float w = m[3]*x + m[7]*y + m[15];
        return {m[0]*x + m[4]*y + m[12], m[1]*x + m[5]*y + m[13],
                (m[2]*x + m[6]*y + m[14] + w)*0.5f, w, u, v, fg, bg};
    }
    std::uint32_t offset() const {
        if (result.vertices.size() > std::numeric_limits<std::uint32_t>::max())
            throw std::length_error("frame too large");
        return static_cast<std::uint32_t>(result.vertices.size());
    }
    void clear(RGBA fg) {
        if (!scissor.w || !scissor.h) return;
        const auto first = offset();
        // A clear respects scissor, but ignores projection, viewport and blend.
        for (auto p : {PointVertex{-1,-1}, {1,-1}, {1,1}, {-1,-1}, {1,1}, {-1,1}})
            result.vertices.push_back({p.x, p.y, 0, 1, 0, 0, fg, {}});
        result.batches.push_back({{0,0,width,height}, scissor, false, 0, 0, first, 6});
    }
    void line(Vertex a, Vertex b) {
        // Clip homogeneous endpoints before dividing by w. This also avoids
        // unbounded extrusion for lines crossing the projection eye plane.
        for (int plane = 0; plane < 6; ++plane) {
            auto distance = [plane](const Vertex& v) {
                switch (plane) {
                case 0: return v.w + v.x; case 1: return v.w - v.x;
                case 2: return v.w + v.y; case 3: return v.w - v.y;
                case 4: return v.z; default: return v.w - v.z;
                }
            };
            const float da = distance(a), db = distance(b);
            if (da < 0 && db < 0) return;
            if ((da < 0) != (db < 0)) {
                const float t = da / (da-db);
                Vertex v = a;
                auto mix = [t](float x, float y) { return x + t*(y-x); };
                v.x=mix(a.x,b.x); v.y=mix(a.y,b.y); v.z=mix(a.z,b.z); v.w=mix(a.w,b.w);
                v.color={mix(a.color.r,b.color.r),mix(a.color.g,b.color.g),mix(a.color.b,b.color.b),mix(a.color.a,b.color.a)};
                if (da < 0) a=v; else b=v;
            }
        }
        if (a.w <= 0 || b.w <= 0) return;
        float dx = (b.x/b.w-a.x/a.w)*viewport.w;
        float dy = (b.y/b.w-a.y/a.w)*viewport.h;
        const float length = std::hypot(dx, dy);
        if (!(length > 0) || !std::isfinite(length)) return;
        const float ox = -dy/length*lineWidth/viewport.w;
        const float oy = dx/length*lineWidth/viewport.h;
        Vertex ap=a, am=a, bp=b, bm=b;
        ap.x+=ox*a.w; ap.y+=oy*a.w; am.x-=ox*a.w; am.y-=oy*a.w;
        bp.x+=ox*b.w; bp.y+=oy*b.w; bm.x-=ox*b.w; bm.y-=oy*b.w;
        result.vertices.insert(result.vertices.end(), {ap,am,bm,ap,bm,bp});
    }
    void draw(const CmdBuffer& buffer, const Command& c) {
        if (!c.vertexCount || !c.indexCount) return;
        auto& stats=result.stats;
        ++stats.drawCalls;
        const bool lines=c.op==Op::Lines || c.op==Op::ColoredLines;
        if (lines) stats.lines+=c.indexCount/2; else stats.triangles+=c.indexCount/3;
        const auto size=c.op==Op::ColoredLines || c.op==Op::ColoredTriangles ? sizeof(ColoredVertex) :
            c.op==Op::TexturedTriangles ? sizeof(TexturedVertex) : c.op==Op::FontTriangles ? sizeof(FontVertex) : sizeof(PointVertex);
        stats.bufferBytes+=c.vertexCount*size+std::size_t(c.indexCount)*sizeof(std::uint32_t);
        if (viewport.w <= 0 || viewport.h <= 0 || !scissor.w || !scissor.h) return;
        const auto first = offset();
        auto get = [&](std::uint32_t i) {
            const auto index = c.vertexOffset + i;
            switch (c.op) {
            case Op::ColoredLines: case Op::ColoredTriangles: {
                const auto& v=buffer.coloredPoints()[index]; return vertex(v.x,v.y,v.color.rgba()); }
            case Op::TexturedTriangles: {
                const auto& v=buffer.texturedPoints()[index]; return vertex(v.x,v.y,color,v.u,v.v); }
            case Op::FontTriangles: {
                const auto& v=buffer.fontPoints()[index]; return vertex(v.x,v.y,v.color,v.u,v.v,v.background); }
            default: { const auto& v=buffer.points()[index]; return vertex(v.x,v.y,color); }
            }
        };
        auto indices = buffer.indices().subspan(c.indexOffset, c.indexCount);
        if (c.op == Op::Lines || c.op == Op::ColoredLines) {
            for (std::size_t i=0; i<indices.size(); i+=2) line(get(indices[i]),get(indices[i+1]));
        } else {
            for (auto i : indices) result.vertices.push_back(get(i));
        }
        const auto count = offset()-first;
        if (!count) return;
        unsigned mode = c.op == Op::TexturedTriangles ? 1 : c.op == Op::FontTriangles ? 2 :
            c.mode != DrawMode::Solid ? static_cast<unsigned>(c.mode)+2 : 0;
        result.batches.push_back({viewport, scissor, blend, c.texture, mode, first, count});
    }
};
}
Frame prepare(const CmdBuffer& commands, int width, int height) {
    Replay replay(width,height); replay.run(commands); return std::move(replay.result);
}
Frame prepare(const ZCmdBuffer& commands, int width, int height) {
    Replay replay(width,height);
    for (const auto& [z,buffer] : commands.buffers()) replay.run(buffer);
    return std::move(replay.result);
}
} // namespace stars::renderer
