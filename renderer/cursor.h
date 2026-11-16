#pragma once
#include "asset.h"
#include "builders.h"

namespace stars::renderer {
struct Cursor {
    unsigned width{},height{},hotspotX{},hotspotY{};
    std::vector<std::uint8_t> pixels;
    void draw(CmdBuffer& cb,TextureID texture,PointVertex mouse) const;
};
struct Cursors {
    std::vector<Cursor> sizes;
    static Cursors parse(std::span<const std::uint8_t> bytes);
    static Cursors load(const std::filesystem::path& path) { return parse(unpack(path)); }
    const Cursor& at(int characterSize) const;
};
} // namespace stars::renderer
