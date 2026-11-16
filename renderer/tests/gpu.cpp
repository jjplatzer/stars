#include "gpu.h"
#include "builders.h"
#include "font.h"
#include "cursor.h"
#include "window.h"
#include "tess.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <array>
#include <algorithm>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace stars::renderer;
void require(bool ok, const char* operation) {
    if (!ok) throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}
struct Target {
    SDL_GPUDevice* device;
    SDL_GPUTextureFormat format;
    SDL_GPUTexture* texture{};
    SDL_GPUTransferBuffer* transfer{};
    explicit Target(SDL_GPUDevice* d,SDL_GPUTextureFormat f=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM) : device(d),format(f) {
        SDL_GPUTextureCreateInfo info{};
        info.type=SDL_GPU_TEXTURETYPE_2D; info.format=format;
        info.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET; info.width=64; info.height=64;
        info.layer_count_or_depth=1; info.num_levels=1;
        texture=SDL_CreateGPUTexture(device,&info); require(texture,"create target");
        SDL_GPUTransferBufferCreateInfo buffer{};
        buffer.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD; buffer.size=64*64*4;
        transfer=SDL_CreateGPUTransferBuffer(device,&buffer);
        if (!transfer) { SDL_ReleaseGPUTexture(device,texture); require(false,"create readback"); }
    }
    ~Target() {
        SDL_ReleaseGPUTransferBuffer(device,transfer); SDL_ReleaseGPUTexture(device,texture);
    }
    std::array<std::uint8_t,64*64*4> draw(Gpu& gpu, const auto& cb) {
        auto* command=SDL_AcquireGPUCommandBuffer(device); require(command,"acquire commands");
        try { gpu.encode(command,texture,format,64,64,prepare(cb,64,64)); }
        catch (...) { SDL_CancelGPUCommandBuffer(command); throw; }
        auto* copy=SDL_BeginGPUCopyPass(command); require(copy,"begin readback");
        SDL_GPUTextureRegion source{}; source.texture=texture; source.w=64; source.h=64; source.d=1;
        SDL_GPUTextureTransferInfo destination{}; destination.transfer_buffer=transfer;
        destination.pixels_per_row=64; destination.rows_per_layer=64;
        SDL_DownloadFromGPUTexture(copy,&source,&destination); SDL_EndGPUCopyPass(copy);
        auto* fence=SDL_SubmitGPUCommandBufferAndAcquireFence(command); require(fence,"submit commands");
        bool waited=SDL_WaitForGPUFences(device,true,&fence,1); SDL_ReleaseGPUFence(device,fence);
        require(waited,"wait for render");
        auto* data=static_cast<const std::uint8_t*>(SDL_MapGPUTransferBuffer(device,transfer,false)); require(data,"map readback");
        std::array<std::uint8_t,64*64*4> result;
        std::copy_n(data,result.size(),result.begin()); SDL_UnmapGPUTransferBuffer(device,transfer);
        return result;
    }
};
void pixel(const auto& image, int x, int y, std::array<int,4> expected, int tolerance=0) {
    for (int c=0;c<4;++c) if (std::abs(int(image[(y*64+x)*4+c])-expected[c])>tolerance)
        throw std::runtime_error("pixel mismatch at " + std::to_string(x) + "," + std::to_string(y) +
            " channel " + std::to_string(c) + ": " + std::to_string(image[(y*64+x)*4+c]) + " expected " + std::to_string(expected[c]));
}
void quad(CmdBuffer& cb, float x0, float y0, float x1, float y1, DrawMode mode=DrawMode::Solid) {
    TrianglesBuilder b; b.addQuad({x0,y0},{x1,y0},{x1,y1},{x0,y1}); b.generateCommands(cb,mode);
}
void test(SDL_GPUDevice* device) {
    Gpu gpu(device); Target target(device);
    CmdBuffer cb;
    cb.clear({1,0,0,1}); cb.loadProjectionMatrix(screenOrtho(64,64)); cb.setRGB({0,1,0});
    quad(cb,0,0,32,32);
    cb.viewport({32,0,32,32}); cb.loadProjectionMatrix(screenOrtho(32,32)); cb.setRGB({1,1,0});
    quad(cb,0,0,32,32);
    cb.scissor({8,8,8,8}); cb.clear({0,0,1,1});
    auto image=target.draw(gpu,cb);
    for (int y=0;y<64;++y) for(int x=0;x<64;++x) {
        std::array<int,4> expected{255,0,0,255};
        if(x<32 && y<32) expected={0,255,0,255};
        if(x>=32 && y>=32) expected={255,255,0,255};
        if(x>=8 && x<16 && y>=48 && y<56) expected={0,0,255,255};
        pixel(image,x,y,expected);
    }
    // The production readback returns OpenGL-style bottom-up rows. A cropped,
    // unaligned-width rectangle also checks transfer row padding and origin.
    auto bottomUp=gpu.read(target.texture,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,64,64,{5,4,37,51});
    for(int y=0;y<51;++y) for(int x=0;x<37;++x) for(int c=0;c<4;++c)
        require(bottomUp[(y*37+x)*4+c]==image[((63-4-y)*64+5+x)*4+c],"readback crop/orientation");
    const std::uint8_t rgba[]={255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
    auto texture=gpu.texture(2,2,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,rgba);
    cb.reset(); cb.clear({0,0,0,1}); cb.loadProjectionMatrix(screenOrtho(64,64)); cb.setRGB({1,1,1});
    TexturedTrianglesBuilder tb;
    tb.addQuad({0,0},{0,0},{64,0},{1,0},{64,64},{1,1},{0,64},{0,1}); tb.generateCommands(cb,texture);
    image=target.draw(gpu,cb);
    for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
        const int i=((y>=32)*2+(x>=32))*4;
        pixel(image,x,y,{rgba[i],rgba[i+1],rgba[i+2],rgba[i+3]});
    }
    const std::uint8_t replacement[]={0,255,255,255};
    gpu.update(texture,1,1,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,replacement);
    image=target.draw(gpu,cb); // Recorded commands still reference the same ID.
    for(int y=0;y<64;++y) for(int x=0;x<64;++x) pixel(image,x,y,{0,255,255,255});
    gpu.remove(texture);
    const std::uint8_t mask[]={0,128,255};
    texture=gpu.texture(3,1,SDL_GPU_TEXTUREFORMAT_R8_UNORM,mask);
    cb.reset(); cb.clear({0,0,0,1}); cb.loadProjectionMatrix(screenOrtho(64,64));
    const RGBA fg{1,1,1,1}, bg{0,0,0,0};
    const FontVertex fv[]={{0,0,0,0,fg,bg},{60,0,1,0,fg,bg},{60,64,1,1,fg,bg},{0,64,0,1,fg,bg}};
    const std::uint32_t indices[]={0,1,2,0,2,3}; cb.drawFontTriangles(texture,fv,indices);
    image=target.draw(gpu,cb);
    // REDS' font shader squares coverage for alpha, then SRC_ALPHA blends.
    pixel(image,10,32,{0,0,0,255}); pixel(image,30,32,{32,32,32,207},1); pixel(image,50,32,{255,255,255,255});
    gpu.remove(texture);
    cb.reset(); cb.clear({0,0,0,1}); cb.loadProjectionMatrix(screenOrtho(64,64));
    cb.setRGB({1,0,1}); cb.lineWidth(4);
    LinesBuilder lines; lines.addLine({8,32},{56,32}); lines.generateCommands(cb);
    image=target.draw(gpu,cb);
    for(int y=0;y<64;++y) for(int x=0;x<64;++x)
        pixel(image,x,y,x>=8 && x<56 && y>=30 && y<34 ? std::array<int,4>{255,0,255,255} : std::array<int,4>{0,0,0,255});
    // Blending uses source alpha for both color and alpha, matching glBlendFunc.
    cb.reset(); cb.clear({0,0,1,1}); cb.loadProjectionMatrix(screenOrtho(64,64));
    cb.setRGBA({1,0,0,0.5}); quad(cb,0,0,64,64);
    image=target.draw(gpu,cb); pixel(image,32,32,{128,0,128,191},1);
    // FNV-1a reference images from REDS' five 32x32 masks, tiled at the
    // OpenGL bottom-left origin. This checks every output pixel and bit order.
    constexpr std::uint64_t hashes[]={0xba3a4c19f7b4c8a5ULL,0xae02411eb15eeaa5ULL,
        0x9d572f420f58d525ULL,0x44d7592e1bd7e325ULL,0x38d496fadc715b25ULL};
    for (unsigned mode=1;mode<=5;++mode) {
        cb.reset(); cb.clear({0,0,0,1}); cb.loadProjectionMatrix(screenOrtho(64,64)); cb.setRGB({1,1,1});
        quad(cb,0,0,64,64,static_cast<DrawMode>(mode)); image=target.draw(gpu,cb);
        std::uint64_t hash=14695981039346656037ULL;
        for(auto byte:image) hash=(hash^byte)*1099511628211ULL;
        if(hash!=hashes[mode-1]) throw std::runtime_error("STARS stipple mask mismatch: " + std::to_string(mode));
    }
    // Draw every exported font size through its actual packed atlas. Compare
    // screen pixels with the resource mask to catch UV/padding/offset mistakes.
    const std::filesystem::path resources=STARS_RESOURCES;
    for(auto name:{"a","b","oa","ob"}) {
        auto font=Font::load(resources/"bmp"/(std::string(name)+".bin.zst"));
        for(auto& f:font.sizes) {
            auto atlas=gpu.texture(f.atlasWidth,f.atlasHeight,SDL_GPU_TEXTUREFORMAT_R8_UNORM,f.pixels);
            cb.reset(); cb.clear({0,0,0,1}); cb.loadProjectionMatrix(screenOrtho(64,64));
            TextBuilder text; text.addText(f,"A.",{10,10}); text.generateCommands(cb,atlas);
            image=target.draw(gpu,cb);
            std::array<std::uint8_t,64*64> expected{};
            int pen=10;
            for(auto code:{'A','.'}) {
                auto g=f.glyph(code);
                for(unsigned y=0;y<g->height;++y) for(unsigned x=0;x<g->width;++x) {
                    int sx=pen+g->offsetX+int(x),sy=10+g->offsetY+int(y);
                    if(sx>=0 && sx<64 && sy>=0 && sy<64)
                        expected[sy*64+sx]|=f.pixels[(g->y+y)*f.atlasWidth+g->x+x];
                }
                pen+=g->advance;
            }
            for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
                auto a=expected[y*64+x]; pixel(image,x,y,{a,a,a,255});
            }
            gpu.remove(atlas);
        }
    }
    for(auto name:{"dcb","keyboard"}) {
        auto cursors=Cursors::load(resources/"cur"/(std::string(name)+".bin.zst"));
        for(auto& c:cursors.sizes) {
            auto atlas=gpu.texture(c.width,c.height,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,c.pixels);
            cb.reset(); cb.clear({1,0,1,1}); cb.loadProjectionMatrix(screenOrtho(64,64));
            c.draw(cb,atlas,{32,32}); image=target.draw(gpu,cb);
            for(unsigned y=0;y<c.height;++y) for(unsigned x=0;x<c.width;++x) {
                const auto i=(y*c.width+x)*4;
                auto expected=std::array<int,4>{255,0,255,255};
                if(c.pixels[i+3]) expected={c.pixels[i],c.pixels[i+1],c.pixels[i+2],c.pixels[i+3]};
                pixel(image,32-int(c.hotspotX)+int(x),32-int(c.hotspotY)+int(y),expected);
            }
            gpu.remove(atlas);
        }
    }
    cb.reset(); cb.clear({0,0,0,1}); cb.loadProjectionMatrix(screenOrtho(64,64)); cb.setRGB({0,1,0});
    const std::vector<std::vector<PointVertex>> rings={{{4,4},{60,4},{60,60},{4,60}},{{20,20},{44,20},{44,44},{20,44}}};
    tessellate(rings).generateCommands(cb); image=target.draw(gpu,cb);
    for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
        bool filled=x>=4 && x<60 && y>=4 && y<60 && !(x>=20 && x<44 && y>=20 && y<44);
        pixel(image,x,y,filled ? std::array<int,4>{0,255,0,255} : std::array<int,4>{0,0,0,255});
    }
    {
        Target bgra(device,SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM);
        cb.reset(); cb.clear({1,0.5f,0,1}); cb.scissor({0,0,64,32}); cb.clear({0,0,1,1});
        bgra.draw(gpu,cb);
        auto read=gpu.read(bgra.texture,bgra.format,64,64,{0,0,64,64});
        for(int y=0;y<64;++y) for(int x=0;x<64;++x)
            pixel(read,x,y,y<32 ? std::array<int,4>{0,0,255,255} : std::array<int,4>{255,128,0,255},1);
        bool rejected=false;
        try { gpu.read(bgra.texture,bgra.format,64,64,{63,0,2,1}); } catch(const std::invalid_argument&) { rejected=true; }
        require(rejected,"out-of-bounds readback rejection");
    }
    cb.reset(); cb.clear({0,0,0,1}); cb.loadProjectionMatrix(screenOrtho(64,64));
    ColoredTrianglesBuilder colors;
    colors.addTriangle({0,0},{1,0,0},{64,0},{0,1,0},{0,64},{0,0,1}); colors.generateCommands(cb);
    image=target.draw(gpu,cb);
    for(int y=0;y<30;++y) for(int x=0;x<30;++x) {
        float red=1-float(x+y+1)/64,green=(x+0.5f)/64,blue=(y+0.5f)/64;
        pixel(image,x,y,{int(std::lround(red*255)),int(std::lround(green*255)),int(std::lround(blue*255)),255},1);
    }
    cb.reset(); cb.clear({0,0,0,1}); cb.loadProjectionMatrix(screenOrtho(64,64)); cb.lineWidth(4);
    ColoredLinesBuilder coloredLine; coloredLine.addLine({8,32},{1,0,0},{56,32},{0,0,1}); coloredLine.generateCommands(cb);
    image=target.draw(gpu,cb);
    for(int x=8;x<56;++x) {
        float t=(x+0.5f-8)/48;
        pixel(image,x,32,{int(std::lround((1-t)*255)),0,int(std::lround(t*255)),255},1);
    }
    ZCmdBuffer layers;
    layers.at(-2).clear({0,0,1,1}); layers.at(-2).loadProjectionMatrix(screenOrtho(64,64));
    auto child=std::make_shared<CmdBuffer>(); child->disableBlend(); child->scissor({0,0,64,32});
    child->setRGBA({1,0,0,0.5f}); quad(*child,0,0,64,64); layers.at(0).call(child);
    auto& top=layers.at(1); top.setRGBA({0,1,0,0.25f}); quad(top,16,16,48,48);
    image=target.draw(gpu,layers);
    pixel(image,1,1,{0,0,255,255}); pixel(image,1,63,{255,0,0,128},1); pixel(image,32,40,{0,255,0,64},1);
    // Black outline behind the foreground uses each atlas' independent offsets.
    for(auto name:{"a","b"}) {
        auto foreground=Font::load(resources/"bmp"/(std::string(name)+".bin.zst"));
        auto outline=Font::load(resources/"bmp"/(std::string("o")+name+".bin.zst"));
        for(unsigned size=0;size<6;++size) {
            auto& fg=foreground.sizes[size]; auto& bg=outline.sizes[size];
            auto ft=gpu.texture(fg.atlasWidth,fg.atlasHeight,SDL_GPU_TEXTUREFORMAT_R8_UNORM,fg.pixels);
            auto bt=gpu.texture(bg.atlasWidth,bg.atlasHeight,SDL_GPU_TEXTUREFORMAT_R8_UNORM,bg.pixels);
            cb.reset(); cb.clear(rgba8(64,64,64,255)); cb.loadProjectionMatrix(screenOrtho(64,64));
            TextBuilder text; text.addText(bg,"A",{20,20},{{0,0,0,1},{}}); text.generateCommands(cb,bt);
            text.reset(); text.addText(fg,"A",{20,20}); text.generateCommands(cb,ft); image=target.draw(gpu,cb);
            auto coverage=[](const FontSize& f,int x,int y) {
                auto g=f.glyph('A'); x-=20+g->offsetX; y-=20+g->offsetY;
                return x>=0 && y>=0 && unsigned(x)<g->width && unsigned(y)<g->height && f.pixels[(g->y+y)*f.atlasWidth+g->x+x]!=0;
            };
            for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
                int value=coverage(fg,x,y)?255:coverage(bg,x,y)?0:64;
                pixel(image,x,y,{value,value,value,255});
            }
            gpu.remove(ft); gpu.remove(bt);
        }
    }
    std::cout << "SDL GPU " << SDL_GetGPUDeviceDriver(device) << ": replay, viewport, scissor, RGBA texture, R8 font, wide lines, blending and all five STARS stipple masks, 24 font atlases, 6 cursors, cropped readback, polygon holes, colored interpolation, nested state and text outlines verified\n";
}
void presentation(SDL_GPUDevice* device) {
    auto* window=SDL_CreateWindow("STARS Renderer Test",320,240,SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY);
    require(window,"create presentation window");
    try {
        Window output(device,window);
        for(int i=0;i<3;++i) {
            if(i==1) { require(SDL_SetWindowSize(window,400,300),"resize window"); require(SDL_SyncWindow(window),"sync resize"); }
            SDL_PumpEvents();
            auto size=output.pixels(); require(size.x>0 && size.y>0,"framebuffer dimensions");
            CmdBuffer cb; cb.clear({0,0,0,1}); cb.loadProjectionMatrix(screenOrtho(size.x,size.y)); cb.setRGB({0,1,0});
            quad(cb,10,10,size.x-10,size.y-10);
            require(output.draw(cb),"present window");
            require(output.stats().buffers==1 && output.stats().triangles==2 && output.stats().gpuDrawCalls==2,"window frame statistics");
        }
        require(SDL_WaitForGPUIdle(device),"wait for presentation");
    } catch(...) { SDL_DestroyWindow(window); throw; }
    SDL_DestroyWindow(window);
    std::cout<<"SDL window presentation and resize verified\n";
}
int main() {
    SDL_GPUDevice* device=nullptr; int result=0;
    try {
        require(SDL_Init(SDL_INIT_VIDEO),"SDL_Init");
        if(SDL_GetHint(SDL_HINT_VULKAN_LIBRARY)) require(SDL_Vulkan_LoadLibrary(nullptr),"load Vulkan test library");
        device=SDL_CreateGPUDevice(Gpu::shaderFormats,true,nullptr); require(device,"SDL_CreateGPUDevice");
        test(device);
        presentation(device);
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; result=1; }
    if(device) SDL_DestroyGPUDevice(device);
    if(SDL_GetHint(SDL_HINT_VULKAN_LIBRARY)) SDL_Vulkan_UnloadLibrary();
    SDL_Quit(); return result;
}
