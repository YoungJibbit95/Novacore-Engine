#pragma once
#if NOVACORE_HAS_VULKAN && NOVACORE_HAS_SDL3
#include <vulkan/vulkan.h>
#include "novacore/render/UiDrawData.hpp"
#include <memory>

namespace novacore::render {
// Records into the engine's render pass. No instance, device, queue, swapchain,
// render pass, submission or fence ownership is transferred to this adapter.
class VulkanUiRenderer final {
public:
    VulkanUiRenderer();
    ~VulkanUiRenderer();
    bool create(VkPhysicalDevice physical, VkDevice device, VkRenderPass pass, VkFormat colorFormat);
    void shutdown(); // caller waits for device idle first
    bool prepare(const UiDrawData& data, std::uint32_t slot, VkCommandPool pool, VkQueue queue);
    std::size_t record(VkCommandBuffer command, const UiDrawData& data,
        std::uint32_t slot, VkExtent2D extent);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
#endif
