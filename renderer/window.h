#pragma once
#include "gpu.h"
#include <SDL3/SDL_video.h>

namespace stars::renderer {
// Claims an existing SDL window. The platform owns window/device lifetimes;
// destroy this object before either. All methods run on the SDL video thread.
class Window {
public:
    Window(SDL_GPUDevice* device, SDL_Window* window);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Gpu& gpu() { return gpu_; }
    // false means the window is minimized/not drawable. Commands use physical
    // framebuffer pixels; caller rebuilds projection/layout after a resize.
    bool draw(const CmdBuffer& buffer);
    bool draw(const ZCmdBuffer& buffer);
    PointVertex pixels() const;
    const Stats& stats() const { return stats_; }
private:
    template<class Buffer> bool present(const Buffer& buffer);
    SDL_GPUDevice* device_;
    SDL_Window* window_;
    Gpu gpu_;
    Stats stats_;
};
} // namespace stars::renderer
