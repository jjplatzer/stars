#include "gpu.h"
#include "vert.msl.h"
#include "vert.spirv.h"
#include "frag.msl.h"
#include "frag.spirv.h"

#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>

namespace stars::renderer {
namespace {
void check(bool ok, const char* action) {
    if (!ok) throw std::runtime_error(std::string(action) + ": " + SDL_GetError());
}
}
struct Gpu::Impl {
    SDL_GPUDevice* device;
    SDL_GPUShader *vertex{}, *fragment{};
    SDL_GPUSampler *nearest{}, *linear{};
    SDL_GPUBuffer* vertices{};
    SDL_GPUTransferBuffer* transfer{};
    Uint32 capacity{};
    struct Texture { SDL_GPUTexture* handle; bool linear; };
    std::map<TextureID, Texture> textures;
    std::map<std::pair<SDL_GPUTextureFormat,bool>, SDL_GPUGraphicsPipeline*> pipelines;
    TextureID next = 1;

    explicit Impl(SDL_GPUDevice* d) : device(d) {}
    ~Impl() {
        for (auto [key,p] : pipelines) SDL_ReleaseGPUGraphicsPipeline(device,p);
        for (auto [id,t] : textures) SDL_ReleaseGPUTexture(device,t.handle);
        if (vertices) SDL_ReleaseGPUBuffer(device,vertices);
        if (transfer) SDL_ReleaseGPUTransferBuffer(device,transfer);
        if (nearest) SDL_ReleaseGPUSampler(device,nearest);
        if (linear) SDL_ReleaseGPUSampler(device,linear);
        if (vertex) SDL_ReleaseGPUShader(device,vertex);
        if (fragment) SDL_ReleaseGPUShader(device,fragment);
    }
    SDL_GPUShader* shader(bool frag) {
        const bool msl = (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_MSL) != 0;
        if (!msl && !(SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_SPIRV))
            throw std::runtime_error("GPU device does not support MSL or SPIR-V");
        SDL_GPUShaderCreateInfo info{};
        info.code = msl ? (frag ? frag_msl : vert_msl) : (frag ? frag_spirv : vert_spirv);
        info.code_size = msl ? (frag ? frag_msl_size : vert_msl_size) : (frag ? frag_spirv_size : vert_spirv_size);
        info.entrypoint = msl ? "main0" : "main";
        info.format = msl ? SDL_GPU_SHADERFORMAT_MSL : SDL_GPU_SHADERFORMAT_SPIRV;
        info.stage = frag ? SDL_GPU_SHADERSTAGE_FRAGMENT : SDL_GPU_SHADERSTAGE_VERTEX;
        info.num_samplers = frag ? 1 : 0;
        info.num_uniform_buffers = frag ? 1 : 0;
        auto* result=SDL_CreateGPUShader(device,&info); check(result,"create shader"); return result;
    }
    SDL_GPUSampler* sampler(bool smooth) {
        SDL_GPUSamplerCreateInfo info{};
        info.min_filter=info.mag_filter=smooth ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
        info.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        info.address_mode_u=info.address_mode_v=info.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        auto* result=SDL_CreateGPUSampler(device,&info); check(result,"create sampler"); return result;
    }
    SDL_GPUGraphicsPipeline* pipeline(SDL_GPUTextureFormat format, bool blend) {
        auto key=std::make_pair(format,blend);
        if (auto it=pipelines.find(key); it!=pipelines.end()) return it->second;
        SDL_GPUVertexBufferDescription buffer{0,sizeof(Vertex),SDL_GPU_VERTEXINPUTRATE_VERTEX,0};
        SDL_GPUVertexAttribute attributes[] = {
            {0,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,offsetof(Vertex,x)},
            {1,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,offsetof(Vertex,u)},
            {2,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,offsetof(Vertex,color)},
            {3,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,offsetof(Vertex,background)}};
        SDL_GPUColorTargetDescription target{};
        target.format=format;
        auto& b=target.blend_state;
        b.enable_blend=blend;
        b.src_color_blendfactor=b.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        b.dst_color_blendfactor=b.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        b.color_blend_op=b.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader=vertex; info.fragment_shader=fragment;
        info.vertex_input_state={&buffer,1,attributes,4};
        info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
        info.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;
        info.target_info.color_target_descriptions=&target;
        info.target_info.num_color_targets=1;
        auto* result=SDL_CreateGPUGraphicsPipeline(device,&info); check(result,"create pipeline");
        try { pipelines.emplace(key,result); } catch (...) { SDL_ReleaseGPUGraphicsPipeline(device,result); throw; }
        return result;
    }
    void reserve(std::size_t bytes) {
        if (bytes > std::numeric_limits<Uint32>::max()) throw std::length_error("GPU vertex buffer too large");
        if (bytes <= capacity) return;
        auto size=static_cast<Uint32>(bytes);
        SDL_GPUBufferCreateInfo vb{}; vb.usage=SDL_GPU_BUFFERUSAGE_VERTEX; vb.size=size;
        auto* v=SDL_CreateGPUBuffer(device,&vb); check(v,"create vertex buffer");
        SDL_GPUTransferBufferCreateInfo tb{}; tb.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; tb.size=size;
        auto* t=SDL_CreateGPUTransferBuffer(device,&tb);
        if (!t) { SDL_ReleaseGPUBuffer(device,v); check(false,"create vertex transfer buffer"); }
        if (vertices) SDL_ReleaseGPUBuffer(device,vertices);
        if (transfer) SDL_ReleaseGPUTransferBuffer(device,transfer);
        vertices=v; transfer=t; capacity=size;
    }
};
Gpu::Gpu(SDL_GPUDevice* device) {
    if (!device) throw std::invalid_argument("null GPU device");
    impl_=std::make_unique<Impl>(device);
    impl_->vertex=impl_->shader(false); impl_->fragment=impl_->shader(true);
    impl_->nearest=impl_->sampler(false); impl_->linear=impl_->sampler(true);
    // Sampler bindings must be valid even for solid draws.
    const std::uint8_t white[] = {255,255,255,255};
    auto id=texture(1,1,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,white);
    auto node=impl_->textures.extract(id); node.key()=0; impl_->textures.insert(std::move(node));
}
Gpu::~Gpu() = default;
TextureID Gpu::texture(int width, int height, SDL_GPUTextureFormat format,
                       std::span<const std::uint8_t> pixels, bool linear) {
    auto& p=*impl_;
    const unsigned channels=format==SDL_GPU_TEXTUREFORMAT_R8_UNORM ? 1 :
        format==SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM ? 4 : 0;
    if (width<=0 || height<=0 || !channels || std::uint64_t(width)*height*channels!=pixels.size())
        throw std::invalid_argument("invalid texture size or format");
    // SDL 3.2 Metal uploads use tightly packed rows even when pixels_per_row
    // is provided. Vulkan accepts padding; D3D12 requires 256-byte row alignment.
    const std::uint64_t rowBytes=std::uint64_t(width)*channels;
    const std::uint64_t stride=std::string(SDL_GetGPUDeviceDriver(p.device))=="metal" ? rowBytes : (rowBytes+255)&~std::uint64_t(255);
    if (stride*height>std::numeric_limits<Uint32>::max()) throw std::length_error("texture upload too large");
    if (!p.next) throw std::length_error("texture handles exhausted");
    SDL_GPUTextureCreateInfo info{};
    info.type=SDL_GPU_TEXTURETYPE_2D; info.format=format; info.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width=width; info.height=height; info.layer_count_or_depth=1; info.num_levels=1;
    auto* texture=SDL_CreateGPUTexture(p.device,&info); check(texture,"create texture");
    SDL_GPUTransferBuffer* transfer=nullptr;
    SDL_GPUCommandBuffer* command=nullptr;
    try {
        SDL_GPUTransferBufferCreateInfo ti{}; ti.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; ti.size=Uint32(stride*height);
        transfer=SDL_CreateGPUTransferBuffer(p.device,&ti); check(transfer,"create texture transfer buffer");
        auto* data=static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(p.device,transfer,false)); check(data,"map texture transfer buffer");
        for (int y=0; y<height; ++y) std::memcpy(data+y*stride,pixels.data()+std::size_t(y)*width*channels,std::size_t(width)*channels);
        SDL_UnmapGPUTransferBuffer(p.device,transfer);
        command=SDL_AcquireGPUCommandBuffer(p.device); check(command,"acquire texture upload");
        auto* pass=SDL_BeginGPUCopyPass(command); check(pass,"begin texture upload");
        SDL_GPUTextureTransferInfo source{}; source.transfer_buffer=transfer; source.pixels_per_row=Uint32(stride/channels); source.rows_per_layer=height;
        SDL_GPUTextureRegion dest{}; dest.texture=texture; dest.w=width; dest.h=height; dest.d=1;
        SDL_UploadToGPUTexture(pass,&source,&dest,false);
        SDL_EndGPUCopyPass(pass);
        auto* submitted=command; command=nullptr;
        check(SDL_SubmitGPUCommandBuffer(submitted),"submit texture upload");
        p.textures.emplace(p.next,Impl::Texture{texture,linear});
    } catch (...) {
        if (command) SDL_CancelGPUCommandBuffer(command);
        if (transfer) SDL_ReleaseGPUTransferBuffer(p.device,transfer);
        SDL_ReleaseGPUTexture(p.device,texture); throw;
    }
    SDL_ReleaseGPUTransferBuffer(p.device,transfer);
    return p.next++;
}
void Gpu::update(TextureID id, int width, int height, SDL_GPUTextureFormat format,
                 std::span<const std::uint8_t> pixels, bool linear) {
    if (!id || !impl_->textures.contains(id)) throw std::invalid_argument("unknown GPU texture");
    const auto replacement=texture(width,height,format,pixels,linear);
    std::swap(impl_->textures.at(id),impl_->textures.at(replacement));
    remove(replacement);
}
void Gpu::remove(TextureID id) {
    if (!id) return;
    auto& p=*impl_; auto it=p.textures.find(id);
    if (it!=p.textures.end()) { SDL_ReleaseGPUTexture(p.device,it->second.handle); p.textures.erase(it); }
}
std::vector<std::uint8_t> Gpu::read(SDL_GPUTexture* target, SDL_GPUTextureFormat format,
                                     int width, int height, Rect rectangle) {
    const bool bgra=format==SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM || format==SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
    if (!target || width<=0 || height<=0 || rectangle.x<0 || rectangle.y<0 || rectangle.w<=0 || rectangle.h<=0 ||
        std::int64_t(rectangle.x)+rectangle.w>width || std::int64_t(rectangle.y)+rectangle.h>height ||
        (!bgra && format!=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM && format!=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB))
        throw std::invalid_argument("invalid readback target or rectangle");
    const std::uint64_t stride=(std::uint64_t(rectangle.w)*4+255)&~std::uint64_t(255);
    if (stride*rectangle.h>std::numeric_limits<Uint32>::max()) throw std::length_error("readback too large");
    auto* device=impl_->device;
    std::vector<std::uint8_t> result(std::size_t(rectangle.w)*rectangle.h*4);
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD; info.size=Uint32(stride*rectangle.h);
    auto* transfer=SDL_CreateGPUTransferBuffer(device,&info); check(transfer,"create readback buffer");
    SDL_GPUCommandBuffer* command=nullptr;
    SDL_GPUFence* fence=nullptr;
    try {
        command=SDL_AcquireGPUCommandBuffer(device); check(command,"acquire readback commands");
        auto* copy=SDL_BeginGPUCopyPass(command); check(copy,"begin readback");
        SDL_GPUTextureRegion source{}; source.texture=target;
        source.x=rectangle.x; source.y=height-rectangle.y-rectangle.h;
        source.w=rectangle.w; source.h=rectangle.h; source.d=1;
        SDL_GPUTextureTransferInfo dest{}; dest.transfer_buffer=transfer;
        dest.pixels_per_row=Uint32(stride/4); dest.rows_per_layer=rectangle.h;
        SDL_DownloadFromGPUTexture(copy,&source,&dest); SDL_EndGPUCopyPass(copy);
        auto* submitted=command; command=nullptr;
        fence=SDL_SubmitGPUCommandBufferAndAcquireFence(submitted); check(fence,"submit readback");
        check(SDL_WaitForGPUFences(device,true,&fence,1),"wait for readback");
        const auto* data=static_cast<const std::uint8_t*>(SDL_MapGPUTransferBuffer(device,transfer,false)); check(data,"map readback");
        for(int y=0;y<rectangle.h;++y) {
            auto* row=result.data()+std::size_t(y)*rectangle.w*4;
            std::memcpy(row,data+std::size_t(rectangle.h-1-y)*stride,std::size_t(rectangle.w)*4);
            if(bgra) for(int x=0;x<rectangle.w;++x) std::swap(row[x*4],row[x*4+2]);
        }
        SDL_UnmapGPUTransferBuffer(device,transfer);
    } catch(...) {
        if(command) SDL_CancelGPUCommandBuffer(command);
        if(fence) SDL_ReleaseGPUFence(device,fence);
        SDL_ReleaseGPUTransferBuffer(device,transfer); throw;
    }
    SDL_ReleaseGPUFence(device,fence); SDL_ReleaseGPUTransferBuffer(device,transfer);
    return result;
}
void Gpu::encode(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target,
                 SDL_GPUTextureFormat format, int width, int height, const Frame& frame) {
    if (!commands || !target || width<=0 || height<=0) throw std::invalid_argument("invalid GPU render target");
    if (frame.batches.empty()) return;
    if (frame.vertices.empty()) throw std::invalid_argument("draw batches have no vertices");
    auto& p=*impl_;
    // Validate/allocate before opening passes so exceptions leave no active pass.
    for (auto& batch : frame.batches) {
        if (std::uint64_t(batch.first)+batch.count>frame.vertices.size() || batch.count%3 || batch.mode>7 ||
            batch.viewport.w<=0 || batch.viewport.h<=0 || batch.scissor.x<0 || batch.scissor.y<0 ||
            batch.scissor.w<0 || batch.scissor.h<0 ||
            std::int64_t(batch.scissor.x)+batch.scissor.w>width || std::int64_t(batch.scissor.y)+batch.scissor.h>height)
            throw std::invalid_argument("invalid GPU draw batch");
        if (!p.textures.contains(batch.texture)) throw std::invalid_argument("unknown GPU texture");
        p.pipeline(format,batch.blend);
    }
    p.reserve(frame.vertices.size()*sizeof(Vertex));
    auto* mapped=SDL_MapGPUTransferBuffer(p.device,p.transfer,true); check(mapped,"map vertices");
    std::memcpy(mapped,frame.vertices.data(),frame.vertices.size()*sizeof(Vertex));
    SDL_UnmapGPUTransferBuffer(p.device,p.transfer);
    auto* copy=SDL_BeginGPUCopyPass(commands); check(copy,"begin vertex upload");
    SDL_GPUTransferBufferLocation source{p.transfer,0};
    SDL_GPUBufferRegion dest{p.vertices,0,Uint32(frame.vertices.size()*sizeof(Vertex))};
    SDL_UploadToGPUBuffer(copy,&source,&dest,true);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUColorTargetInfo color{}; color.texture=target; color.load_op=SDL_GPU_LOADOP_LOAD; color.store_op=SDL_GPU_STOREOP_STORE;
    auto* pass=SDL_BeginGPURenderPass(commands,&color,1,nullptr); check(pass,"begin render pass");
    SDL_GPUBufferBinding binding{p.vertices,0}; SDL_BindGPUVertexBuffers(pass,0,&binding,1);
    for (auto& b : frame.batches) {
        if (!b.scissor.w || !b.scissor.h || !b.count) continue;
        SDL_BindGPUGraphicsPipeline(pass,p.pipelines.at({format,b.blend}));
        SDL_GPUViewport viewport{float(b.viewport.x),float(b.viewport.y),float(b.viewport.w),float(b.viewport.h),0,1};
        SDL_SetGPUViewport(pass,&viewport);
        SDL_Rect scissor{b.scissor.x,b.scissor.y,b.scissor.w,b.scissor.h}; SDL_SetGPUScissor(pass,&scissor);
        auto t=p.textures.at(b.texture);
        SDL_GPUTextureSamplerBinding sample{t.handle,t.linear?p.linear:p.nearest}; SDL_BindGPUFragmentSamplers(pass,0,&sample,1);
        const float options[4]={float(b.mode),float(height),0,0};
        SDL_PushGPUFragmentUniformData(commands,0,options,sizeof(options));
        SDL_DrawGPUPrimitives(pass,b.count,1,b.first,0);
    }
    SDL_EndGPURenderPass(pass);
}
} // namespace stars::renderer
