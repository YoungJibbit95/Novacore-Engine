#pragma once

#if NOVACORE_HAS_VULKAN && NOVACORE_HAS_SDL3
#include "novacore/render/Renderer.hpp"
#include <vulkan/vulkan.h>

namespace novacore::render {

// Material image/descriptors follow mesh residency, surviving swapchain
// recreation. UI and directional-shadow descriptors remain separate owners.
class VulkanMaterialTextures final {
  public:
    struct Asset;
    VulkanMaterialTextures();
    ~VulkanMaterialTextures();
    void create(VkPhysicalDevice physical, VkDevice device, std::uint32_t slots);
    // Call before backend teardown/recreation destroys any Vulkan resources.
    // Unconfirmed completion after bounded retries is terminal, never cleanup.
    void finishPendingUpload();
    void shutdown(); // after device idle and every mesh/deferred Asset release
    bool prepare(std::uint32_t slot, const RenderFrameInfo& frame, VkExtent2D extent, VkFormat outputFormat,
                 VkCommandPool pool, VkQueue queue);
    [[nodiscard]] std::shared_ptr<Asset> upload(const assets::GltfMeshData& mesh, VkCommandPool pool, VkQueue queue);
    [[nodiscard]] VkDescriptorSetLayout layout() const;
    [[nodiscard]] VkDescriptorSetLayout emptyLayout() const;
    void bind(VkCommandBuffer command, VkPipelineLayout pipeline, const Asset& asset, int material,
              std::uint32_t slot) const;
    [[nodiscard]] bool hasTextures(const Asset& asset, int material) const;
    [[nodiscard]] bool usesFallback(const Asset& asset, int material) const;
    [[nodiscard]] std::size_t residentImageCount() const;
    [[nodiscard]] std::uint64_t residentImageBytes() const;
    [[nodiscard]] std::uint64_t imageBudgetBytes() const;
    [[nodiscard]] std::uint64_t imageBudgetRejectCount() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace novacore::render
#endif
