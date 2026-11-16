#pragma once

#include "asset.h"
#include "builders.h"
#include <array>
#include <string_view>

namespace stars::renderer {
struct Glyph {
    std::uint32_t x{},y{},width{},height{};
    std::int32_t offsetX{},offsetY{},advance{};
};
struct FontSize {
    unsigned height{},pointSize{},width{},lineHeight{},atlasWidth{},atlasHeight{};
    std::array<Glyph,256> glyphs{};
    unsigned glyphCount{};
    std::vector<std::uint8_t> pixels;
    const Glyph* glyph(char32_t code) const { return code<glyphCount ? &glyphs[code] : nullptr; }
    // UTF-8, matching Go's rune iteration. CR is ignored; LF starts a line.
    PointVertex measure(std::string_view text) const;
    PointVertex charSize() const;
    int spacing() const;
};
struct Font {
    std::array<FontSize,6> sizes;
    static Font parse(std::span<const std::uint8_t> bytes);
    static Font load(const std::filesystem::path& path) { return parse(unpack(path)); }
};
struct TextStyle { RGBA color{1,1,1,1}, background{}; };
struct TextBuilder : Geometry<FontVertex> {
    // A builder contains one size/atlas. Generate before changing textures.
    void addText(const FontSize& font, std::string_view text, PointVertex origin, TextStyle style = {});
    void generateCommands(CmdBuffer& cb,TextureID texture) const { cb.drawFontTriangles(texture,vertices,indices); }
};
} // namespace stars::renderer
