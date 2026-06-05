#pragma once

#include "novacore/platform/Window.hpp"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace novacore::render {

struct DebugRect final {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    std::array<float, 4> color{1.0F, 1.0F, 1.0F, 1.0F};
};

struct DebugLine final {
    float x0 = 0.0F;
    float y0 = 0.0F;
    float x1 = 0.0F;
    float y1 = 0.0F;
    std::array<float, 4> color{1.0F, 1.0F, 1.0F, 1.0F};
};

struct DebugText final {
    float x = 0.0F;
    float y = 0.0F;
    float scale = 2.0F;
    std::array<float, 4> color{1.0F, 1.0F, 1.0F, 1.0F};
    std::string text;
};

struct RendererCreateInfo final {
    std::array<float, 4> clearColor{0.03F, 0.04F, 0.06F, 1.0F};
};

struct RenderFrameInfo final {
    std::array<float, 4> clearColor{0.03F, 0.04F, 0.06F, 1.0F};
    std::vector<DebugRect> debugRects;
    std::vector<DebugLine> debugLines;
    std::vector<DebugText> debugTexts;
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
    void* sdlRenderer_ = nullptr;
    std::array<float, 4> clearColor_{0.03F, 0.04F, 0.06F, 1.0F};
};

} // namespace novacore::render







