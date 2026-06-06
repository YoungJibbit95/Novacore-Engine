#pragma once

#include "novacore/platform/Window.hpp"

#include <array>
#include <memory>
#include <string_view>

namespace novacore::render {

struct RenderFrameInfo;

class VulkanBackend final {
public:
    VulkanBackend();
    ~VulkanBackend();

    VulkanBackend(const VulkanBackend&) = delete;
    VulkanBackend& operator=(const VulkanBackend&) = delete;

    VulkanBackend(VulkanBackend&&) noexcept;
    VulkanBackend& operator=(VulkanBackend&&) noexcept;

    [[nodiscard]] bool create(platform::Window& window, std::array<float, 4> clearColor);
    void beginFrame(const RenderFrameInfo& frame);
    void endFrame();
    void shutdown();

    [[nodiscard]] bool ready() const;
    [[nodiscard]] std::string_view deviceName() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace novacore::render
