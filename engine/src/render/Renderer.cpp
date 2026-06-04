#include "novacore/render/Renderer.hpp"

#include "novacore/core/Log.hpp"

namespace novacore::render {

bool Renderer::create(platform::Window& window, const RendererCreateInfo& info) {
    clearColor_ = info.clearColor;

#if NOVACORE_HAS_VULKAN
    vulkanCapable_ = !window.isHeadless();
    if (vulkanCapable_) {
        ready_ = true;
        core::logInfo("render", "Vulkan renderer placeholder created");
        return true;
    }
#else
    (void)window;
#endif

    ready_ = true;
    vulkanCapable_ = false;
    core::logWarning("render", "Using null renderer fallback");
    return true;
}

void Renderer::beginFrame(const RenderFrameInfo& info) {
    clearColor_ = info.clearColor;
}

void Renderer::endFrame() {
}

void Renderer::shutdown() {
    if (ready_) {
        core::logInfo("render", "Renderer shutdown");
    }
    ready_ = false;
    vulkanCapable_ = false;
}

std::string_view Renderer::backendName() const {
    return vulkanCapable_ ? "vulkan-placeholder" : "null";
}

bool Renderer::isReady() const {
    return ready_;
}

} // namespace novacore::render







