#pragma once

#include "fps/platform/Window.hpp"

#include <array>
#include <string_view>

namespace riftline::render {

struct RendererCreateInfo final {
    std::array<float, 4> clearColor{0.03F, 0.04F, 0.06F, 1.0F};
};

struct RenderFrameInfo final {
    std::array<float, 4> clearColor{0.03F, 0.04F, 0.06F, 1.0F};
};

class Renderer final {
public:
    bool create(platform::Window& window, const RendererCreateInfo& info);
    void beginFrame(const RenderFrameInfo& info);
    void endFrame();
    void shutdown();

    [[nodiscard]] std::string_view backendName() const;
    [[nodiscard]] bool isReady() const;

private:
    bool ready_ = false;
    bool vulkanCapable_ = false;
    std::array<float, 4> clearColor_{0.03F, 0.04F, 0.06F, 1.0F};
};

} // namespace riftline::render

