#include "tess.h"
#include "vendor/earcut.hpp"
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace stars::renderer {
TrianglesBuilder tessellate(std::span<const std::vector<PointVertex>> rings) {
    TrianglesBuilder result;
    std::vector<std::vector<std::array<double,2>>> polygon;
    for(const auto& ring:rings) {
        if(ring.size()<3) continue;
        // Earcut reserves one bit of its index type for internal metadata.
        if(ring.size()>std::numeric_limits<std::int32_t>::max()-result.vertices.size())
            throw std::length_error("polygon has too many vertices");
        auto& points=polygon.emplace_back(); points.reserve(ring.size());
        for(auto p:ring) {
            if(!std::isfinite(p.x) || !std::isfinite(p.y)) throw std::invalid_argument("non-finite polygon vertex");
            points.push_back({p.x,p.y}); result.vertices.push_back(p);
        }
    }
    if(polygon.empty()) return result;
    result.indices=mapbox::earcut<std::uint32_t>(polygon);
    if(result.indices.empty()) result.vertices.clear();
    return result;
}
} // namespace stars::renderer
