#include "cursor.h"
#include <algorithm>
#include <stdexcept>

namespace stars::renderer {
Cursors Cursors::parse(std::span<const std::uint8_t> bytes) {
    using detail::valid;
    detail::Reader r(bytes); auto count=r.header("STBMCURS",5); valid(count==1 || count==5);
    Cursors result; result.sizes.resize(count); std::vector<bool> seen(count);
    for(unsigned i=0;i<count;++i) {
        auto index=r.u32(); valid(index<count && !seen[index]); seen[index]=true;
        auto& c=result.sizes[index]; c.width=r.u32(); c.height=r.u32(); c.hotspotX=r.u32(); c.hotspotY=r.u32();
        auto pitch=r.u32(),size=r.u32();
        valid(c.width>0 && c.width<=4096 && c.height>0 && c.height<=4096 && c.hotspotX<c.width && c.hotspotY<c.height);
        valid(pitch==c.width*4 && size==std::uint64_t(pitch)*c.height);
        auto pixels=r.bytes(size); c.pixels.assign(pixels.begin(),pixels.end());
    }
    r.end(); return result;
}
const Cursor& Cursors::at(int characterSize) const {
    if(sizes.empty()) throw std::logic_error("empty cursor set");
    return sizes[std::clamp(characterSize,0,int(sizes.size())-1)];
}
void Cursor::draw(CmdBuffer& cb,TextureID texture,PointVertex mouse) const {
    float x=mouse.x-hotspotX,y=mouse.y-hotspotY;
    TexturedTrianglesBuilder b;
    b.addQuad({x,y},{0,0},{x+width,y},{1,0},{x+width,y+height},{1,1},{x,y+height},{0,1});
    cb.setRGBA({1,1,1,1}); cb.blend(); b.generateCommands(cb,texture);
}
} // namespace stars::renderer
