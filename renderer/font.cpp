#include "font.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace stars::renderer {
namespace {
// Invalid sequences consume one byte, like Go range over a UTF-8 string.
char32_t next(std::string_view& s) {
    auto first=static_cast<unsigned char>(s.front()); s.remove_prefix(1);
    if(first<0x80) return first;
    unsigned n=first>=0xc2 && first<=0xdf ? 1 : first>=0xe0 && first<=0xef ? 2 : first>=0xf0 && first<=0xf4 ? 3 : 0;
    if(!n || s.size()<n) return 0xfffd;
    char32_t code=first & (0x7f>>n);
    for(unsigned i=0;i<n;++i) {
        auto b=static_cast<unsigned char>(s[i]); if((b&0xc0)!=0x80) return 0xfffd;
        code=(code<<6)|(b&0x3f);
    }
    if((n==2 && code<0x800) || (n==3 && code<0x10000) || (code>=0xd800 && code<=0xdfff) || code>0x10ffff) return 0xfffd;
    s.remove_prefix(n); return code;
}
}
Font Font::parse(std::span<const std::uint8_t> bytes) {
    using detail::valid;
    detail::Reader r(bytes); valid(r.header("STBMFONT",6)==6);
    Font result; std::array<bool,6> seen{};
    for(unsigned i=0;i<6;++i) {
        auto index=r.u32(); valid(index<6 && !seen[index]); seen[index]=true;
        auto& f=result.sizes[index];
        f.height=r.u32(); f.pointSize=r.u32(); f.width=r.u32(); f.lineHeight=r.u32();
        f.atlasWidth=r.u32(); f.atlasHeight=r.u32(); f.glyphCount=r.u32(); auto size=r.u32();
        valid(f.height>0 && f.height<=4096 && f.pointSize<=4096 && f.width<=4096 && f.lineHeight>0 && f.lineHeight<=4096);
        valid(f.atlasWidth>0 && f.atlasWidth<=4096 && f.atlasHeight>0 && f.atlasHeight<=4096);
        valid(f.glyphCount>0 && f.glyphCount<=256 && size==std::uint64_t(f.atlasWidth)*f.atlasHeight);
        std::array<bool,256> codes{};
        for(unsigned j=0;j<f.glyphCount;++j) {
            auto code=r.u32(); valid(code<f.glyphCount && !codes[code]); codes[code]=true;
            auto& g=f.glyphs[code]; g.x=r.u32(); g.y=r.u32(); g.width=r.u32(); g.height=r.u32();
            g.offsetX=r.i32(); g.offsetY=r.i32(); g.advance=r.i32();
            valid(std::uint64_t(g.x)+g.width<=f.atlasWidth && std::uint64_t(g.y)+g.height<=f.atlasHeight);
            valid(g.offsetX>=-4096 && g.offsetX<=4096 && g.offsetY>=-4096 && g.offsetY<=4096 && g.advance>=-4096 && g.advance<=4096);
        }
        auto pixels=r.bytes(size); f.pixels.assign(pixels.begin(),pixels.end());
    }
    r.end(); return result;
}
PointVertex FontSize::measure(std::string_view text) const {
    float maximum=0,pen=0,lines=1; const Glyph* pending=nullptr;
    auto flush=[&] { maximum=std::max(maximum,pen+(pending?pending->width:0)); pen=0; pending=nullptr; };
    while(!text.empty()) {
        auto c=next(text); if(c=='\r') continue;
        if(c=='\n') { flush(); ++lines; continue; }
        if(auto g=glyph(c)) { if(pending) pen+=pending->advance; pending=g; }
    }
    flush(); return {maximum,lines*lineHeight};
}
PointVertex FontSize::charSize() const {
    auto g=glyph(' '); if(!g) g=glyph('0');
    return {g?float(g->advance):0,float(lineHeight)};
}
int FontSize::spacing() const {
    auto g=glyph('0'); return g?std::max(0,g->advance-int(g->width)):0;
}
void TextBuilder::addText(const FontSize& font,std::string_view text,PointVertex origin,TextStyle style) {
    float x=origin.x,y=origin.y;
    while(!text.empty()) {
        auto c=next(text); if(c=='\r') continue;
        if(c=='\n') { x=origin.x; y+=font.lineHeight; continue; }
        const auto* g=font.glyph(c); if(!g) continue;
        if(g->width && g->height) {
            if(vertices.size()>std::numeric_limits<std::uint32_t>::max()-4) throw std::length_error("too much text geometry");
            const float x0=x+g->offsetX,y0=y+g->offsetY,x1=x0+g->width,y1=y0+g->height;
            const float u0=float(g->x)/font.atlasWidth,v0=float(g->y)/font.atlasHeight;
            const float u1=float(g->x+g->width)/font.atlasWidth,v1=float(g->y+g->height)/font.atlasHeight;
            const auto base=static_cast<std::uint32_t>(vertices.size());
            vertices.insert(vertices.end(),{{x0,y0,u0,v0,style.color,style.background},{x1,y0,u1,v0,style.color,style.background},
                {x1,y1,u1,v1,style.color,style.background},{x0,y1,u0,v1,style.color,style.background}});
            indices.insert(indices.end(),{base,base+1,base+2,base,base+2,base+3});
        }
        x+=g->advance; // Last glyph advance is unused; measurement uses bitmap width.
    }
}
} // namespace stars::renderer
