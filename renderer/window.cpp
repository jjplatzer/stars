#include "window.h"
#include <SDL3/SDL_error.h>
#include <stdexcept>
#include <string>

namespace stars::renderer {
namespace {
void check(bool success,const char* action) {
    if(!success) throw std::runtime_error(std::string(action)+": "+SDL_GetError());
}
}
Window::Window(SDL_GPUDevice* device,SDL_Window* window) : device_(device),window_(window),gpu_(device) {
    if(!window_) throw std::invalid_argument("null SDL window");
    check(SDL_ClaimWindowForGPUDevice(device_,window_),"claim SDL window");
}
Window::~Window() { SDL_ReleaseWindowFromGPUDevice(device_,window_); }
PointVertex Window::pixels() const {
    int w{},h{}; check(SDL_GetWindowSizeInPixels(window_,&w,&h),"query framebuffer size");
    return {float(w),float(h)};
}
template<class Buffer> bool Window::present(const Buffer& buffer) {
    stats_={};
    auto* commands=SDL_AcquireGPUCommandBuffer(device_); check(commands,"acquire presentation commands");
    SDL_GPUTexture* swapchain=nullptr; Uint32 width{},height{};
    if(!SDL_WaitAndAcquireGPUSwapchainTexture(commands,window_,&swapchain,&width,&height)) {
        SDL_CancelGPUCommandBuffer(commands); check(false,"acquire swapchain");
    }
    if(!swapchain) { SDL_CancelGPUCommandBuffer(commands); return false; }
    // SDL disallows cancelling a command buffer after acquiring a swapchain
    // image. Submit on error as well so that image is released for presentation.
    try {
        auto frame=prepare(buffer,int(width),int(height));
        gpu_.encode(commands,swapchain,SDL_GetGPUSwapchainTextureFormat(device_,window_),int(width),int(height),frame);
        stats_=frame.stats;
    } catch(...) { SDL_SubmitGPUCommandBuffer(commands); throw; }
    check(SDL_SubmitGPUCommandBuffer(commands),"submit presentation");
    return true;
}
bool Window::draw(const CmdBuffer& buffer) { return present(buffer); }
bool Window::draw(const ZCmdBuffer& buffer) { return present(buffer); }
} // namespace stars::renderer
