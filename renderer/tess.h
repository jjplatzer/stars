#pragma once
#include "builders.h"

namespace stars::renderer {
// First valid ring is the outer boundary, subsequent rings are holes. Rings
// with fewer than three vertices are skipped, matching REDS. Winding is free.
// As with REDS' Earcut implementation, self-intersecting polygons are best effort.
TrianglesBuilder tessellate(std::span<const std::vector<PointVertex>> rings);
} // namespace stars::renderer
