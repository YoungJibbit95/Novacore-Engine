#pragma once

#if NOVACORE_HAS_VULKAN && NOVACORE_HAS_SDL3
#include "DirectionalShadowMath.hpp"
#include <memory>
#include <vulkan/vulkan.h>

namespace novacore::render {

// Owns only the directional depth pass. The backend retains submission, fences,
// world receiver pipelines, UI and swapchain ownership.
class VulkanDirectionalShadowMap final {
  public:
    VulkanDirectionalShadowMap();
    ~VulkanDirectionalShadowMap();
    bool create(VkPhysicalDevice physical, VkDevice device, std::uint32_t slots);
    void shutdown(); // caller waits for device idle
    // Called after the slot fence. Resolution changes wait for device idle before
    // replacing every slot's image/descriptor resources.
    bool prepare(std::uint32_t slot, const RenderFrameInfo& frame, VkExtent2D extent);
    [[nodiscard]] VkDescriptorSetLayout receiverLayout() const;
    [[nodiscard]] const detail::DirectionalShadowCamera& lightCamera() const;
    [[nodiscard]] std::uint32_t resolution() const;
    void begin(VkCommandBuffer command, std::uint32_t slot);
    void drawMesh(VkCommandBuffer command, const detail::Mat4& model, VkBuffer vertices, VkBuffer indices,
                  std::uint32_t indexCount);
    void drawBox(VkCommandBuffer command, const RenderBox3D& box);
    void end(VkCommandBuffer command);
    void bindReceiver(VkCommandBuffer command, VkPipelineLayout layout, std::uint32_t slot, bool receives) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace novacore::render
#endif
