#pragma once

#include "frame.h"
#include <SDL3/SDL_gpu.h>
#include <memory>

namespace stars::renderer {

// SDL video and device lifetime belong to the platform. Destroy Gpu before its
// device. Calls use the device's render thread; no Qt/OpenGL runtime is needed.
class Gpu {
public:
    static constexpr SDL_GPUShaderFormat shaderFormats = SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_SPIRV;
    explicit Gpu(SDL_GPUDevice* device);
    ~Gpu();
    Gpu(const Gpu&) = delete;
    Gpu& operator=(const Gpu&) = delete;

    TextureID texture(int width, int height, SDL_GPUTextureFormat format,
                      std::span<const std::uint8_t> pixels, bool linear = false);
    // Replace image/size/filtering while retaining the application handle.
    void update(TextureID id, int width, int height, SDL_GPUTextureFormat format,
                std::span<const std::uint8_t> pixels, bool linear = false);
    void remove(TextureID id);
    // Synchronous RGBA8 readback from an offscreen texture after its render
    // commands have been submitted. Rect and returned row order are bottom-up,
    // matching REDS ReadPixels. SDL swapchain textures cannot be downloaded.
    std::vector<std::uint8_t> read(SDL_GPUTexture* target, SDL_GPUTextureFormat format,
                                   int width, int height, Rect rectangle);
    // Encodes uploads and draws into the caller's command buffer. The caller
    // submits it (and may present a swapchain). Target contents are preserved
    // unless the command stream explicitly clears them.
    void encode(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target,
                SDL_GPUTextureFormat format, int width, int height, const Frame& frame);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace stars::renderer
