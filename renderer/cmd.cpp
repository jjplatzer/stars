#include "cmd.h"

#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace stars::renderer {
void CmdBuffer::reset() {
    commands_.clear(); points_.clear(); colored_.clear(); textured_.clear(); fonts_.clear(); indices_.clear();
}
void CmdBuffer::resetState() { commands_.push_back({Op::Reset}); }
void CmdBuffer::loadProjectionMatrix(Mat4 matrix) { commands_.push_back({.op = Op::Projection, .matrix = matrix}); }
void CmdBuffer::clear(RGBA color) { commands_.push_back({.op = Op::Clear, .color = color}); }
void CmdBuffer::viewport(Rect rect) { commands_.push_back({.op = Op::Viewport, .rect = rect}); }
void CmdBuffer::scissor(Rect rect) { commands_.push_back({.op = Op::Scissor, .rect = rect}); }
void CmdBuffer::disableScissor() { commands_.push_back({Op::DisableScissor}); }
void CmdBuffer::blend() { commands_.push_back({Op::Blend}); }
void CmdBuffer::disableBlend() { commands_.push_back({Op::DisableBlend}); }
void CmdBuffer::setRGBA(RGBA color) { commands_.push_back({.op = Op::Color, .color = color}); }
void CmdBuffer::lineWidth(float width) {
    if (!std::isfinite(width) || width <= 0) throw std::invalid_argument("Invalid line width");
    commands_.push_back({.op = Op::LineWidth, .width = width});
}

template<class V> void CmdBuffer::draw(Command command, std::vector<V>& destination,
                                     std::span<const V> vertices, std::span<const std::uint32_t> indices,
                                     unsigned primitiveSize) {
    if (vertices.empty() || indices.empty()) return;
    if (indices.size() % primitiveSize) throw std::invalid_argument("Incomplete draw primitive");
    for (auto index : indices) if (index >= vertices.size()) throw std::out_of_range("Draw index exceeds vertex count");
    constexpr auto limit = std::numeric_limits<std::uint32_t>::max();
    if (vertices.size() > limit || destination.size() > limit - vertices.size() ||
        indices.size() > limit || indices_.size() > limit - indices.size()) throw std::length_error("Draw buffer exceeds GPU address range");
    command.vertexOffset = static_cast<std::uint32_t>(destination.size());
    command.vertexCount = static_cast<std::uint32_t>(vertices.size());
    command.indexOffset = static_cast<std::uint32_t>(indices_.size());
    command.indexCount = static_cast<std::uint32_t>(indices.size());
    // Callers may reuse geometry exposed by this buffer's read-only views.
    // Preserve such input before reserve/insert can invalidate those views.
    const auto aliases = [](const auto& view, const auto& storage) {
        auto first=reinterpret_cast<std::uintptr_t>(view.data());
        auto start=reinterpret_cast<std::uintptr_t>(storage.data());
        return !storage.empty() && first>=start && first-start<storage.size()*sizeof(storage[0]);
    };
    std::vector<V> vertexCopy;
    std::vector<std::uint32_t> indexCopy;
    if (aliases(vertices,destination)) { vertexCopy.assign(vertices.begin(),vertices.end()); vertices=vertexCopy; }
    if (aliases(indices,indices_)) { indexCopy.assign(indices.begin(),indices.end()); indices=indexCopy; }
    // Reserve before modifying sizes so allocation failure leaves the recording intact.
    const auto grow = [](auto& data, std::size_t extra) {
        const auto needed = data.size() + extra;
        if (needed > data.capacity()) data.reserve(std::max(needed, data.capacity() + data.capacity() / 2));
    };
    grow(destination, vertices.size());
    grow(indices_, indices.size());
    grow(commands_, 1);
    destination.insert(destination.end(), vertices.begin(), vertices.end());
    indices_.insert(indices_.end(), indices.begin(), indices.end());
    commands_.push_back(std::move(command));
}
void CmdBuffer::drawLines(std::span<const PointVertex> v, std::span<const std::uint32_t> i) { draw({Op::Lines}, points_, v, i, 2); }
void CmdBuffer::drawColoredLines(std::span<const ColoredVertex> v, std::span<const std::uint32_t> i) { draw({Op::ColoredLines}, colored_, v, i, 2); }
void CmdBuffer::drawTriangles(std::span<const PointVertex> v, std::span<const std::uint32_t> i, DrawMode mode) { draw({.op = Op::Triangles, .mode = mode}, points_, v, i, 3); }
void CmdBuffer::drawColoredTriangles(std::span<const ColoredVertex> v, std::span<const std::uint32_t> i) { draw({Op::ColoredTriangles}, colored_, v, i, 3); }
void CmdBuffer::drawTexturedTriangles(TextureID texture, std::span<const TexturedVertex> v, std::span<const std::uint32_t> i) { draw({.op = Op::TexturedTriangles, .texture = texture}, textured_, v, i, 3); }
void CmdBuffer::drawFontTriangles(TextureID texture, std::span<const FontVertex> v, std::span<const std::uint32_t> i) { draw({.op = Op::FontTriangles, .texture = texture}, fonts_, v, i, 3); }
void CmdBuffer::call(std::shared_ptr<const CmdBuffer> other) {
    if (!other) return;
    std::vector<const CmdBuffer*> pending{other.get()};
    std::unordered_set<const CmdBuffer*> visited;
    while (!pending.empty()) {
        const auto* current = pending.back();
        pending.pop_back();
        if (current == this) throw std::invalid_argument("Recursive command buffer call");
        if (!visited.insert(current).second) continue;
        for (const auto& command : current->commands()) if (command.called) pending.push_back(command.called.get());
    }
    if (!other->empty()) commands_.push_back({.op = Op::Call, .called = std::move(other)});
}
} // namespace stars::renderer
