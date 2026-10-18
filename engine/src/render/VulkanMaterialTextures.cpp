#include "VulkanMaterialTextures.hpp"

#if NOVACORE_HAS_VULKAN && NOVACORE_HAS_SDL3
#include "../assets/ImageDecoder.hpp"
#include "MaterialTextureMath.hpp"
#include "novacore/core/Log.hpp"
#include <bit>
#include <cstring>
#include <map>

namespace novacore::render {
namespace {
struct TextureBuffer final {
    VkDevice device{};
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    void* mapped = nullptr;
    ~TextureBuffer() {
        if (mapped) {
            vkUnmapMemory(device, memory);
        }
        if (buffer) {
            vkDestroyBuffer(device, buffer, nullptr);
        }
        if (memory) {
            vkFreeMemory(device, memory, nullptr);
        }
    }
};
struct TextureImage final {
    VkDevice device{};
    VkImage image{};
    VkDeviceMemory memory{};
    VkImageView view{};
    VkFormat format{};
    std::uint32_t mipLevels = 1;
    bool completeMipChain = false;
    std::uint64_t hash = 0;
    std::shared_ptr<detail::MaterialImageBudget> budget;
    std::uint64_t reservedBytes = 0;
    std::shared_ptr<const std::vector<std::uint8_t>> source;
    ~TextureImage() {
        if (view) {
            vkDestroyImageView(device, view, nullptr);
        }
        if (image) {
            vkDestroyImage(device, image, nullptr);
        }
        if (memory) {
            vkFreeMemory(device, memory, nullptr);
        }
        if (budget && reservedBytes) {
            (void)budget->release(reservedBytes);
        }
    }
};
bool mipmapped(const assets::GltfSamplerData& sampler) {
    return sampler.minFilter >= 9984 && sampler.minFilter <= 9987;
}
VkSamplerAddressMode addressMode(int value) {
    return value == 33071   ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
           : value == 33648 ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT
                            : VK_SAMPLER_ADDRESS_MODE_REPEAT;
}
bool validSampler(const assets::GltfSamplerData& sampler) {
    const auto wrap = [](int value) { return value == 10497 || value == 33071 || value == 33648; };
    return (sampler.magFilter == 9728 || sampler.magFilter == 9729) &&
           (sampler.minFilter == 9728 || sampler.minFilter == 9729 || mipmapped(sampler)) && wrap(sampler.wrapS) &&
           wrap(sampler.wrapT);
}
std::array<int, 4> textureIndices(const assets::GltfMaterialData& material) {
    return {material.baseColorTextureIndex, material.metallicRoughnessTextureIndex, material.normalTextureIndex,
            material.emissiveTextureIndex};
}
bool hasRequiredUvs(const assets::GltfMeshData& mesh, std::size_t material,
                    const detail::MaterialTextureUniforms& uniforms) {
    const auto indices = textureIndices(mesh.materials[material]);
    for (const auto& primitive : mesh.primitives) {
        if (primitive.materialIndex != static_cast<int>(material)) {
            continue;
        }
        if (indices[2] >= 0 && primitive.normals.size() != primitive.positions.size()) {
            return false;
        }
        for (std::size_t slot = 0; slot < indices.size(); ++slot) {
            if (indices[slot] < 0) {
                continue;
            }
            const auto& uv = uniforms.uvSets[slot] == 1 ? primitive.texcoords1 : primitive.texcoords;
            if (uv.size() != primitive.positions.size() || uv.empty()) {
                return false;
            }
        }
    }
    return true;
}
} // namespace

struct VulkanMaterialTextures::Asset final {
    VkDevice device{};
    VkDescriptorPool pool{};
    std::unique_ptr<TextureBuffer> uniforms;
    std::vector<std::vector<VkDescriptorSet>> sets;
    std::vector<std::shared_ptr<TextureImage>> images;
    std::vector<bool> textured;
    std::vector<bool> fallback;
    ~Asset() {
        if (pool) {
            vkDestroyDescriptorPool(device, pool, nullptr);
        }
    }
    [[nodiscard]] std::size_t index(int material) const {
        const auto count = sets.size() - 1;
        return material >= 0 && static_cast<std::size_t>(material) < count ? static_cast<std::size_t>(material) : count;
    }
};

struct VulkanMaterialTextures::Impl final {
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkDescriptorSetLayout layout{};
    VkDescriptorSetLayout emptyLayout{};
    VkDeviceSize materialStride = 0;
    std::uint32_t slotCount = 0;
    std::vector<std::unique_ptr<TextureBuffer>> frames;
    std::array<std::shared_ptr<TextureImage>, 4> defaults;
    std::vector<std::weak_ptr<TextureImage>> cache;
    std::map<std::array<int, 5>, VkSampler> samplers;
    bool attemptedCore = false;
    bool ready = false;
    bool loggedOutputFormat = false;
    std::shared_ptr<detail::MaterialImageBudget> budget;

    std::optional<std::uint32_t> memoryType(std::uint32_t mask, VkMemoryPropertyFlags flags) const {
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(physical, &properties);
        for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
            if ((mask & (1U << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags) {
                return i;
            }
        }
        return std::nullopt;
    }
    bool allocate(const VkMemoryRequirements& requirements, VkMemoryPropertyFlags flags, VkDeviceMemory& result) {
        const auto type = memoryType(requirements.memoryTypeBits, flags);
        if (!type) {
            return false;
        }
        VkMemoryAllocateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        info.allocationSize = requirements.size;
        info.memoryTypeIndex = *type;
        return vkAllocateMemory(device, &info, nullptr, &result) == VK_SUCCESS;
    }
    std::unique_ptr<TextureBuffer> buffer(VkDeviceSize size, VkBufferUsageFlags usage) {
        auto result = std::make_unique<TextureBuffer>();
        result->device = device;
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &info, nullptr, &result->buffer) != VK_SUCCESS) {
            return {};
        }
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, result->buffer, &requirements);
        if (!allocate(requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      result->memory) ||
            vkBindBufferMemory(device, result->buffer, result->memory, 0) != VK_SUCCESS ||
            vkMapMemory(device, result->memory, 0, VK_WHOLE_SIZE, 0, &result->mapped) != VK_SUCCESS) {
            return {};
        }
        return result;
    }

    std::shared_ptr<TextureImage> uploadPixels(std::uint32_t width, std::uint32_t height,
                                               std::span<const std::uint8_t> pixels, VkFormat format, bool generateMips,
                                               VkCommandPool pool, VkQueue queue) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physical, format, &properties);
        auto required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if (generateMips) {
            required |= VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
        }
        if ((properties.optimalTilingFeatures & required) != static_cast<VkFormatFeatureFlags>(required)) {
            return {};
        }
        auto result = std::make_shared<TextureImage>();
        result->device = device;
        result->format = format;
        result->mipLevels = generateMips ? static_cast<std::uint32_t>(std::bit_width(std::max(width, height))) : 1;
        result->completeMipChain = generateMips || (width == 1 && height == 1);
        VkImageCreateInfo image{};
        image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = format;
        image.extent = {width, height, 1};
        image.mipLevels = result->mipLevels;
        image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                      (generateMips ? VkImageUsageFlags{VK_IMAGE_USAGE_TRANSFER_SRC_BIT} : VkImageUsageFlags{0});
        image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(device, &image, nullptr, &result->image) != VK_SUCCESS) {
            return {};
        }
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device, result->image, &requirements);
        if (!budget || !budget->reserve(requirements.size)) {
            core::logWarning("render", "Material image budget rejected " + std::to_string(requirements.size) +
                                           " bytes (used " + std::to_string(budget ? budget->used : 0) + ", limit " +
                                           std::to_string(budget ? budget->limit : 0) + "); explicit texture fallback");
            return {};
        }
        result->budget = budget;
        result->reservedBytes = requirements.size;
        if (!allocate(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, result->memory) ||
            vkBindImageMemory(device, result->image, result->memory, 0) != VK_SUCCESS) {
            return {};
        }
        const auto staging = buffer(pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        if (!staging) {
            return {};
        }
        std::memcpy(staging->mapped, pixels.data(), pixels.size());
        VkCommandBufferAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocation.commandPool = pool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = 1;
        VkCommandBuffer command{};
        if (vkAllocateCommandBuffers(device, &allocation, &command) != VK_SUCCESS) {
            return {};
        }
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        bool success = vkBeginCommandBuffer(command, &begin) == VK_SUCCESS;
        const auto transition = [&](std::uint32_t first, std::uint32_t count, VkImageLayout before, VkImageLayout after,
                                    VkAccessFlags sourceAccess, VkAccessFlags targetAccess,
                                    VkPipelineStageFlags sourceStage, VkPipelineStageFlags targetStage) {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = sourceAccess;
            barrier.dstAccessMask = targetAccess;
            barrier.oldLayout = before;
            barrier.newLayout = after;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = result->image;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, first, count, 0, 1};
            vkCmdPipelineBarrier(command, sourceStage, targetStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        };
        if (success) {
            transition(0, result->mipLevels, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                       VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {width, height, 1};
            vkCmdCopyBufferToImage(command, staging->buffer, result->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                   &copy);
            auto previousWidth = static_cast<std::int32_t>(width);
            auto previousHeight = static_cast<std::int32_t>(height);
            for (std::uint32_t level = 1; level < result->mipLevels; ++level) {
                transition(level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT);
                const auto nextWidth = std::max(1, previousWidth / 2);
                const auto nextHeight = std::max(1, previousHeight / 2);
                VkImageBlit blit{};
                blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1};
                blit.srcOffsets[1] = {previousWidth, previousHeight, 1};
                blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
                blit.dstOffsets[1] = {nextWidth, nextHeight, 1};
                vkCmdBlitImage(command, result->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, result->image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
                transition(level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
                previousWidth = nextWidth;
                previousHeight = nextHeight;
            }
            transition(result->mipLevels - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            success = vkEndCommandBuffer(command) == VK_SUCCESS;
            if (success) {
                VkSubmitInfo submit{};
                submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submit.commandBufferCount = 1;
                submit.pCommandBuffers = &command;
                success = vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS &&
                          vkQueueWaitIdle(queue) == VK_SUCCESS;
            }
        }
        vkFreeCommandBuffers(device, pool, 1, &command);
        if (!success) {
            return {};
        }
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = result->image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, result->mipLevels, 0, 1};
        if (vkCreateImageView(device, &view, nullptr, &result->view) != VK_SUCCESS) {
            return {};
        }
        return result;
    }

    VkSampler sampler(const assets::GltfSamplerData& source, std::uint32_t levels) {
        if (!validSampler(source)) {
            return VK_NULL_HANDLE;
        }
        const std::array key{source.magFilter, source.minFilter, source.wrapS, source.wrapT, static_cast<int>(levels)};
        if (const auto it = samplers.find(key); it != samplers.end()) {
            return it->second;
        }
        VkSamplerCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = source.magFilter == 9728 ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        info.minFilter = source.minFilter == 9728 || source.minFilter == 9984 || source.minFilter == 9986
                             ? VK_FILTER_NEAREST
                             : VK_FILTER_LINEAR;
        info.mipmapMode = source.minFilter == 9986 || source.minFilter == 9987 ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                                                               : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = addressMode(source.wrapS);
        info.addressModeV = addressMode(source.wrapT);
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.maxLod = mipmapped(source) ? static_cast<float>(levels - 1) : 0;
        VkSampler result{};
        if (vkCreateSampler(device, &info, nullptr, &result) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
        samplers.emplace(key, result);
        return result;
    }

    std::shared_ptr<TextureImage> image(const assets::GltfImageData& source, VkFormat format, bool mips,
                                        VkCommandPool pool, VkQueue queue) {
        if (!source.encodedBytes || source.encodedBytes->empty()) {
            return {};
        }
        const auto hash = detail::imageContentHash(*source.encodedBytes);
        std::erase_if(cache, [](const auto& entry) { return entry.expired(); });
        for (const auto& entry : cache) {
            const auto candidate = entry.lock();
            if (candidate && candidate->format == format && (!mips || candidate->completeMipChain) &&
                candidate->hash == hash && candidate->source &&
                detail::sameImageContent(candidate->source, source.encodedBytes)) {
                return candidate;
            }
        }
        const auto decoded = assets::detail::decodeGltfImage(*source.encodedBytes, source.mimeType);
        if (!decoded.ok()) {
            core::logWarning("render", "glTF image fallback: " + decoded.error);
            return {};
        }
        const auto result = uploadPixels(decoded.width, decoded.height, decoded.pixels, format, mips, pool, queue);
        if (result) {
            result->hash = hash;
            result->source = source.encodedBytes;
            cache.push_back(result);
        }
        return result;
    }

    bool createCore(VkCommandPool pool, VkQueue queue) {
        VkDescriptorSetLayoutCreateInfo empty{};
        empty.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        if (vkCreateDescriptorSetLayout(device, &empty, nullptr, &emptyLayout) != VK_SUCCESS) {
            return false;
        }
        std::array<VkDescriptorSetLayoutBinding, 6> bindings{};
        bindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        bindings[1] = {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        for (std::uint32_t i = 2; i < bindings.size(); ++i) {
            bindings[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        }
        VkDescriptorSetLayoutCreateInfo descriptor{};
        descriptor.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptor.bindingCount = static_cast<std::uint32_t>(bindings.size());
        descriptor.pBindings = bindings.data();
        if (vkCreateDescriptorSetLayout(device, &descriptor, nullptr, &layout) != VK_SUCCESS) {
            return false;
        }
        for (std::uint32_t slot = 0; slot < slotCount; ++slot) {
            auto frame = buffer(sizeof(detail::MaterialFrameUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
            if (!frame) {
                return false;
            }
            frames.push_back(std::move(frame));
        }
        constexpr std::array<std::array<std::uint8_t, 4>, 4> pixels{
            {{255, 255, 255, 255}, {255, 255, 255, 255}, {128, 128, 255, 255}, {0, 0, 0, 255}}};
        for (std::size_t i = 0; i < defaults.size(); ++i) {
            defaults[i] =
                uploadPixels(1, 1, pixels[i], i == 0 || i == 3 ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM,
                             false, pool, queue);
            if (!defaults[i]) {
                return false;
            }
        }
        return true;
    }
};

VulkanMaterialTextures::VulkanMaterialTextures() : impl_(std::make_unique<Impl>()) {}
VulkanMaterialTextures::~VulkanMaterialTextures() = default;
void VulkanMaterialTextures::create(VkPhysicalDevice physical, VkDevice device, std::uint32_t slots) {
    impl_->physical = physical;
    impl_->device = device;
    impl_->slotCount = slots;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical, &properties);
    const auto alignment = std::max<VkDeviceSize>(1, properties.limits.minUniformBufferOffsetAlignment);
    impl_->materialStride = (sizeof(detail::MaterialTextureUniforms) + alignment - 1) / alignment * alignment;
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    VkDeviceSize largestLocalHeap = 0;
    for (std::uint32_t i = 0; i < memory.memoryHeapCount; ++i) {
        if ((memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
            largestLocalHeap = std::max(largestLocalHeap, memory.memoryHeaps[i].size);
        }
    }
    impl_->budget = std::make_shared<detail::MaterialImageBudget>();
    impl_->budget->limit = std::min<VkDeviceSize>(1024ULL * 1024ULL * 1024ULL, largestLocalHeap / 4);
}
void VulkanMaterialTextures::shutdown() {
    if (!impl_->device) {
        return;
    }
    impl_->defaults = {};
    impl_->cache.clear();
    impl_->frames.clear();
    for (const auto& [_, sampler] : impl_->samplers) {
        vkDestroySampler(impl_->device, sampler, nullptr);
    }
    if (impl_->layout) {
        vkDestroyDescriptorSetLayout(impl_->device, impl_->layout, nullptr);
    }
    if (impl_->emptyLayout) {
        vkDestroyDescriptorSetLayout(impl_->device, impl_->emptyLayout, nullptr);
    }
    *impl_ = {};
}

bool VulkanMaterialTextures::prepare(std::uint32_t slot, const RenderFrameInfo& frame, VkExtent2D extent,
                                     VkFormat outputFormat, VkCommandPool pool, VkQueue queue) {
    if (!frame.materialTexturesEnabled || !frame.camera3D.enabled || slot >= impl_->slotCount ||
        !detail::validShadowReceiverCamera(frame.camera3D)) {
        return false;
    }
    if (outputFormat != VK_FORMAT_B8G8R8A8_UNORM && outputFormat != VK_FORMAT_R8G8B8A8_UNORM &&
        outputFormat != VK_FORMAT_B8G8R8A8_SRGB && outputFormat != VK_FORMAT_R8G8B8A8_SRGB) {
        if (!impl_->loggedOutputFormat) {
            core::logWarning("render", "Material texture output requires supported RGBA8/BGRA8 SDR presentation; "
                                       "retaining baseline rendering");
            impl_->loggedOutputFormat = true;
        }
        return false;
    }
    if (!impl_->attemptedCore) {
        impl_->attemptedCore = true;
        impl_->ready = impl_->createCore(pool, queue);
        if (!impl_->ready) {
            core::logWarning("render", "glTF material textures unavailable; retaining baseline rendering");
        }
    }
    if (!impl_->ready) {
        return false;
    }
    const bool encode = outputFormat != VK_FORMAT_B8G8R8A8_SRGB && outputFormat != VK_FORMAT_R8G8B8A8_SRGB;
    const auto constants = detail::materialFrameUniforms(frame.camera3D, extent.width, extent.height, encode);
    std::memcpy(impl_->frames[slot]->mapped, &constants, sizeof(constants));
    return true;
}

std::shared_ptr<VulkanMaterialTextures::Asset> VulkanMaterialTextures::upload(const assets::GltfMeshData& mesh,
                                                                              VkCommandPool pool, VkQueue queue) {
    if (!impl_->ready || mesh.materials.size() > 4096) {
        return {};
    }
    const auto count = mesh.materials.size() + 1;
    auto result = std::make_shared<Asset>();
    result->device = impl_->device;
    result->sets.resize(count);
    result->textured.resize(count, false);
    result->fallback.resize(count, true);
    result->uniforms = impl_->buffer(impl_->materialStride * count, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    if (!result->uniforms) {
        return {};
    }
    const auto setCount = static_cast<std::uint32_t>(count * impl_->slotCount);
    const VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, setCount * 2},
                                       {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, setCount * 4}};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = sizes;
    poolInfo.maxSets = setCount;
    if (vkCreateDescriptorPool(impl_->device, &poolInfo, nullptr, &result->pool) != VK_SUCCESS) {
        return {};
    }
    for (std::size_t material = 0; material < count; ++material) {
        detail::MaterialTextureUniforms constants{};
        constants.emissiveNormalScale[3] = 1;
        std::array<std::shared_ptr<TextureImage>, 4> images = impl_->defaults;
        std::array<VkSampler, 4> samplers{};
        for (auto& sampler : samplers) {
            sampler = impl_->sampler({}, 1);
            if (!sampler) {
                return {};
            }
        }
        const bool referenced =
            material < mesh.materials.size() &&
            std::ranges::any_of(
                mesh.primitives,
                [&](const auto& primitive) { return primitive.materialIndex == static_cast<int>(material); });
        if (referenced) {
            const auto& source = mesh.materials[material];
            // An unsupported map/alpha/UV path still has real material factors.
            // The textured shader adds emission separately from albedo, so do
            // not lose it merely because every image falls back.
            constants = detail::materialFactorFallbackUniforms(source);
            const auto candidate = detail::materialTextureUniforms(source);
            if (!candidate || !hasRequiredUvs(mesh, material, *candidate)) {
                core::logWarning("render", "glTF material " + std::to_string(material) +
                                               " uses unsupported alpha/UV/texture-transform/normal/non-finite data; "
                                               "explicit opaque factor fallback");
            } else {
                constants = *candidate;
                const auto indices = textureIndices(source);
                bool failed = false;
                bool requested = false;
                for (std::size_t slot = 0; slot < indices.size(); ++slot) {
                    if (indices[slot] == -1) {
                        continue;
                    }
                    requested = true;
                    if (indices[slot] < 0 || static_cast<std::size_t>(indices[slot]) >= mesh.textures.size()) {
                        failed = true;
                        continue;
                    }
                    const auto& texture = mesh.textures[static_cast<std::size_t>(indices[slot])];
                    if (texture.imageIndex < 0 || static_cast<std::size_t>(texture.imageIndex) >= mesh.images.size() ||
                        texture.samplerIndex < -1 ||
                        (texture.samplerIndex >= 0 &&
                         static_cast<std::size_t>(texture.samplerIndex) >= mesh.samplers.size())) {
                        failed = true;
                        continue;
                    }
                    const auto samplerSource = texture.samplerIndex >= 0
                                                   ? mesh.samplers[static_cast<std::size_t>(texture.samplerIndex)]
                                                   : assets::GltfSamplerData{};
                    if (!validSampler(samplerSource)) {
                        failed = true;
                        continue;
                    }
                    const auto image =
                        impl_->image(mesh.images[static_cast<std::size_t>(texture.imageIndex)],
                                     slot == 0 || slot == 3 ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM,
                                     mipmapped(samplerSource), pool, queue);
                    const auto sampler = image ? impl_->sampler(samplerSource, image->mipLevels) : VK_NULL_HANDLE;
                    if (!image || !sampler) {
                        failed = true;
                        continue;
                    }
                    images[slot] = image;
                    samplers[slot] = sampler;
                    constants.mapEnabled[slot] = 1;
                    result->textured[material] = true;
                }
                result->fallback[material] = failed || !requested;
                if (failed) {
                    core::logWarning("render", "glTF material " + std::to_string(material) +
                                                   " has an unavailable image/sampler; explicit per-map fallback");
                }
            }
        }
        std::memcpy(static_cast<std::byte*>(result->uniforms->mapped) + impl_->materialStride * material, &constants,
                    sizeof(constants));
        result->images.insert(result->images.end(), images.begin(), images.end());
        for (std::uint32_t slot = 0; slot < impl_->slotCount; ++slot) {
            VkDescriptorSetAllocateInfo allocation{};
            allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocation.descriptorPool = result->pool;
            allocation.descriptorSetCount = 1;
            allocation.pSetLayouts = &impl_->layout;
            VkDescriptorSet descriptor{};
            if (vkAllocateDescriptorSets(impl_->device, &allocation, &descriptor) != VK_SUCCESS) {
                return {};
            }
            result->sets[material].push_back(descriptor);
            const VkDescriptorBufferInfo buffers[]{
                {impl_->frames[slot]->buffer, 0, sizeof(detail::MaterialFrameUniforms)},
                {result->uniforms->buffer, impl_->materialStride * material, sizeof(detail::MaterialTextureUniforms)}};
            std::array<VkDescriptorImageInfo, 4> imageInfos{};
            std::array<VkWriteDescriptorSet, 6> writes{};
            for (std::uint32_t binding = 0; binding < writes.size(); ++binding) {
                auto& write = writes[binding];
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = descriptor;
                write.dstBinding = binding;
                write.descriptorCount = 1;
                write.descriptorType =
                    binding < 2 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                if (binding < 2) {
                    write.pBufferInfo = &buffers[binding];
                } else {
                    imageInfos[binding - 2] = {samplers[binding - 2], images[binding - 2]->view,
                                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                    write.pImageInfo = &imageInfos[binding - 2];
                }
            }
            vkUpdateDescriptorSets(impl_->device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }
    return result;
}

VkDescriptorSetLayout VulkanMaterialTextures::layout() const { return impl_->layout; }
VkDescriptorSetLayout VulkanMaterialTextures::emptyLayout() const { return impl_->emptyLayout; }
void VulkanMaterialTextures::bind(VkCommandBuffer command, VkPipelineLayout pipeline, const Asset& asset, int material,
                                  std::uint32_t slot) const {
    const auto descriptor = asset.sets[asset.index(material)][slot];
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline, 1, 1, &descriptor, 0, nullptr);
}
bool VulkanMaterialTextures::hasTextures(const Asset& asset, int material) const {
    return asset.textured[asset.index(material)];
}
bool VulkanMaterialTextures::usesFallback(const Asset& asset, int material) const {
    return asset.fallback[asset.index(material)];
}
std::size_t VulkanMaterialTextures::residentImageCount() const {
    return static_cast<std::size_t>(
        std::ranges::count_if(impl_->cache, [](const auto& entry) { return !entry.expired(); }));
}
std::uint64_t VulkanMaterialTextures::residentImageBytes() const { return impl_->budget ? impl_->budget->used : 0; }
std::uint64_t VulkanMaterialTextures::imageBudgetBytes() const { return impl_->budget ? impl_->budget->limit : 0; }
std::uint64_t VulkanMaterialTextures::imageBudgetRejectCount() const {
    return impl_->budget ? impl_->budget->rejected : 0;
}

} // namespace novacore::render
#endif
