#include "font.h"
#include "cursor.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace stars::renderer;
void check(bool value) { if(!value) throw std::runtime_error("bitmap asset contract failed"); }
void close(float a,float b) { check(std::isfinite(a) && std::abs(a-b)<0.0001f); }
void rejects(auto operation) {
    bool failed=false; try { operation(); } catch(const std::runtime_error&) { failed=true; } check(failed);
}
void put(std::vector<std::uint8_t>& b,std::size_t at,std::uint32_t v) {
    for(unsigned i=0;i<4;++i) b.at(at+i)=std::uint8_t(v>>(8*i));
}
int main() {
    try {
        const std::filesystem::path root=STARS_RESOURCES;
        constexpr unsigned heights[2][6]={{9,14,17,20,21,23},{11,12,15,16,18,19}};
        for(auto name: {"a","b","oa","ob"}) {
            auto font=Font::load(root/"bmp"/(std::string(name)+".bin.zst"));
            for(unsigned i=0;i<6;++i) {
                auto& f=font.sizes[i];
                check(f.height==heights[std::string(name).back()=='b'][i] && f.glyphCount==256);
                check(f.lineHeight==f.height && f.pixels.size()==f.atlasWidth*f.atlasHeight);
                auto zero=f.glyph('0'), space=f.glyph(' '); check(zero && space);
                close(f.measure("00").x,zero->advance+zero->width);
                close(f.measure("0\r\n00").x,zero->advance+zero->width);
                close(f.measure("0\r\n00").y,2*f.lineHeight);
                close(f.measure("").x,0); close(f.measure("").y,f.lineHeight);
                close(f.measure("0\n").y,2*f.lineHeight);
                close(f.charSize().x,space->advance);
                close(f.measure("0\xf0\x9f\x98\x80").x,zero->width); // unsupported codepoint is skipped
                close(f.measure("0\xff").x,zero->width); // malformed UTF-8 is skipped
                const auto* symbol=f.glyph(0xa0);
                close(f.measure("\xc2\xa0").x,symbol->width); // UTF-8 decoded to STARS slot
                TextBuilder b; b.addText(f,"0\n0",{10,20});
                check(b.vertices.size()==8 && b.indices.size()==12);
                close(b.vertices[0].x,10+zero->offsetX); close(b.vertices[0].y,20+zero->offsetY);
                close(b.vertices[4].y,20+f.lineHeight+zero->offsetY);
                close(b.vertices[0].u,float(zero->x)/f.atlasWidth);
                close(b.vertices[0].v,float(zero->y)/f.atlasHeight);
            }
        }
        auto raw=unpack(root/"bmp/a.bin.zst");
        for(auto cut: {std::size_t(0),std::size_t(15),std::size_t(51),raw.size()-1})
            rejects([&] { Font::parse(std::span(raw).first(cut)); });
        for(auto [offset,value]: {std::pair<std::size_t,std::uint32_t>{8,2},{12,7},{16,6},{36,0xffffffff},{44,257},{48,1},{52,256},{56,0xffffffff}}) {
            auto bad=raw; put(bad,offset,value); rejects([&] { Font::parse(bad); });
        }
        raw.push_back(0); rejects([&] { Font::parse(raw); });
        rejects([&] { unpack(std::span<const std::uint8_t>{}); });
        std::uint8_t invalid[]={1,2,3,4}; rejects([&] { unpack(invalid); });
        auto dcb=Cursors::load(root/"cur/dcb.bin.zst");
        check(dcb.sizes.size()==1 && dcb.at(5).width==18 && dcb.at(0).hotspotX==1 && dcb.at(0).hotspotY==0);
        auto keyboard=Cursors::load(root/"cur/keyboard.bin.zst");
        check(keyboard.sizes.size()==5 && keyboard.at(5).width==23 && keyboard.at(-1).width==11);
        for(auto& c: keyboard.sizes) check(c.hotspotX==c.width/2 && c.hotspotY==c.height/2 && c.pixels.size()==c.width*c.height*4);
        CmdBuffer cb; keyboard.at(0).draw(cb,7,{10,20});
        close(cb.texturedPoints()[0].x,5); close(cb.texturedPoints()[0].y,15);
        raw=unpack(root/"cur/dcb.bin.zst");
        for(auto [offset,value]: {std::pair<std::size_t,std::uint32_t>{12,2},{16,1},{20,0},{28,18},{36,1},{40,0}}) {
            auto bad=raw; put(bad,offset,value); rejects([&] { Cursors::parse(bad); });
        }
        raw.pop_back(); rejects([&] { Cursors::parse(raw); });
        std::cout<<"24 font atlases and 6 cursors: layout, UTF-8, bounds and malformed input verified\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
