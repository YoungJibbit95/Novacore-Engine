#include "VulkanDirectionalShadowMap.hpp"

#if NOVACORE_HAS_VULKAN && NOVACORE_HAS_SDL3
#include "novacore/core/Log.hpp"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace novacore::render {
namespace {

VkShaderModule shadowShader(VkDevice device, const char* name) {
    std::ifstream file(std::filesystem::path(NOVACORE_SHADER_BINARY_DIR) / name, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() <= 0 || static_cast<std::size_t>(file.tellg()) % sizeof(std::uint32_t) != 0) {
        return VK_NULL_HANDLE;
    }
    std::vector<std::uint32_t> bytes(static_cast<std::size_t>(file.tellg()) / sizeof(std::uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size() * sizeof(std::uint32_t)));
    if (!file) {
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = bytes.size() * sizeof(std::uint32_t);
    info.pCode = bytes.data();
    VkShaderModule module = VK_NULL_HANDLE;
    (void)vkCreateShaderModule(device, &info, nullptr, &module);
    return module;
}

struct ShadowBoxPush final {
    std::array<float, 16> lightViewProjection;
    std::array<float, 4> center;
    std::array<float, 4> halfExtents;
};
static_assert(sizeof(ShadowBoxPush) == 96);

} // namespace

struct VulkanDirectionalShadowMap::Impl final {
    struct Slot final {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory imageMemory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkBuffer uniforms = VK_NULL_HANDLE;
        VkDeviceMemory uniformMemory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
    };
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkRenderPass pass = VK_NULL_HANDLE;
    VkDescriptorSetLayout receiverLayout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkPipelineLayout casterLayout = VK_NULL_HANDLE;
    VkPipeline meshPipeline = VK_NULL_HANDLE;
    VkPipeline boxPipeline = VK_NULL_HANDLE;
    std::vector<Slot> slots;
    VkDeviceSize uniformStride = 0;
    std::uint32_t resolution = 0;
    std::uint32_t failedResolution = 0;
    detail::DirectionalShadowCamera light{};
    bool loggedInvalid = false;
    bool loggedAllocation = false;
    bool attemptedCore = false;
    bool coreReady = false;

    std::optional<std::uint32_t> memoryType(std::uint32_t mask, VkMemoryPropertyFlags flags) const {
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(physical, &properties);
        for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
            if ((mask & (1U << i)) != 0 && (properties.memoryTypes[i].propertyFlags & flags) == flags) {
                return i;
            }
        }
        return std::nullopt;
    }

    bool allocate(const VkMemoryRequirements& requirements, VkMemoryPropertyFlags flags, VkDeviceMemory& memory) {
        const auto type = memoryType(requirements.memoryTypeBits, flags);
        if (!type) {
            return false;
        }
        VkMemoryAllocateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        info.allocationSize = requirements.size;
        info.memoryTypeIndex = *type;
        return vkAllocateMemory(device, &info, nullptr, &memory) == VK_SUCCESS;
    }

    void destroyMaps() {
        if (pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, pool, nullptr);
            pool = VK_NULL_HANDLE;
        }
        for (auto& slot : slots) {
            if (slot.mapped != nullptr) {
                vkUnmapMemory(device, slot.uniformMemory);
            }
            if (slot.framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device, slot.framebuffer, nullptr);
            }
            if (slot.view != VK_NULL_HANDLE) {
                vkDestroyImageView(device, slot.view, nullptr);
            }
            if (slot.image != VK_NULL_HANDLE) {
                vkDestroyImage(device, slot.image, nullptr);
            }
            if (slot.imageMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, slot.imageMemory, nullptr);
            }
            if (slot.uniforms != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, slot.uniforms, nullptr);
            }
            if (slot.uniformMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, slot.uniformMemory, nullptr);
            }
            slot = {};
        }
        resolution = 0;
    }

    bool createPass() {
        constexpr auto required = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        for (const auto candidate : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM}) {
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(physical, candidate, &properties);
            if ((properties.optimalTilingFeatures & required) == required) {
                format = candidate;
                break;
            }
        }
        if (format == VK_FORMAT_UNDEFINED) {
            return false;
        }
        VkAttachmentDescription attachment{};
        attachment.format = format;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        const VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.pDepthStencilAttachment = &reference;
        // Whole-frame dependencies: light-map texels are unrelated to screen
        // pixels. BY_REGION would not cover arbitrary receiver texture reads.
        std::array<VkSubpassDependency, 2> dependencies{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstStageMask =
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = dependencies[0].dstStageMask;
        dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 1;
        info.pAttachments = &attachment;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = static_cast<std::uint32_t>(dependencies.size());
        info.pDependencies = dependencies.data();
        return vkCreateRenderPass(device, &info, nullptr, &pass) == VK_SUCCESS;
    }

    bool createLayouts() {
        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
        bindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        bindings[1] = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout.bindingCount = static_cast<std::uint32_t>(bindings.size());
        layout.pBindings = bindings.data();
        if (vkCreateDescriptorSetLayout(device, &layout, nullptr, &receiverLayout) != VK_SUCCESS) {
            return false;
        }
        const VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowBoxPush)};
        VkPipelineLayoutCreateInfo pipelineLayout{};
        pipelineLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayout.pushConstantRangeCount = 1;
        pipelineLayout.pPushConstantRanges = &range;
        if (vkCreatePipelineLayout(device, &pipelineLayout, nullptr, &casterLayout) != VK_SUCCESS) {
            return false;
        }
        VkSamplerCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = VK_FILTER_NEAREST;
        info.minFilter = VK_FILTER_NEAREST;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        info.compareEnable = VK_TRUE;
        info.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        return vkCreateSampler(device, &info, nullptr, &sampler) == VK_SUCCESS;
    }

    bool createPipeline(bool mesh, VkPipeline& pipeline) {
        const auto shader = shadowShader(device, mesh ? "shadow_mesh.vert.spv" : "shadow_box.vert.spv");
        if (shader == VK_NULL_HANDLE) {
            return false;
        }
        VkPipelineShaderStageCreateInfo stage{};
        stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        stage.module = shader;
        stage.pName = "main";
        // Same position/normal/UV vertex layout as the backend, position only.
        const VkVertexInputBindingDescription binding{0, 8 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX};
        const VkVertexInputAttributeDescription attribute{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
        VkPipelineVertexInputStateCreateInfo vertex{};
        vertex.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex.vertexBindingDescriptionCount = mesh ? 1U : 0U;
        vertex.pVertexBindingDescriptions = mesh ? &binding : nullptr;
        vertex.vertexAttributeDescriptionCount = mesh ? 1U : 0U;
        vertex.pVertexAttributeDescriptions = mesh ? &attribute : nullptr;
        VkPipelineInputAssemblyStateCreateInfo assembly{};
        assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{};
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0F;
        raster.depthBiasEnable = VK_TRUE;
        raster.depthBiasConstantFactor = 1.25F;
        raster.depthBiasSlopeFactor = 1.75F;
        VkPipelineMultisampleStateCreateInfo samples{};
        samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        const VkDynamicState states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = states;
        VkGraphicsPipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount = 1;
        info.pStages = &stage;
        info.pVertexInputState = &vertex;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &samples;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = casterLayout;
        info.renderPass = pass;
        const auto result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
        vkDestroyShaderModule(device, shader, nullptr);
        return result == VK_SUCCESS;
    }

    bool createMaps(std::uint32_t size) {
        const auto count = static_cast<std::uint32_t>(slots.size());
        const VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, count},
                                           {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count}};
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = count;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = sizes;
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
            return false;
        }
        for (auto& slot : slots) {
            VkImageCreateInfo image{};
            image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            image.imageType = VK_IMAGE_TYPE_2D;
            image.format = format;
            image.extent = {size, size, 1};
            image.mipLevels = 1;
            image.arrayLayers = 1;
            image.samples = VK_SAMPLE_COUNT_1_BIT;
            image.tiling = VK_IMAGE_TILING_OPTIMAL;
            image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (vkCreateImage(device, &image, nullptr, &slot.image) != VK_SUCCESS) {
                return false;
            }
            VkMemoryRequirements imageRequirements{};
            vkGetImageMemoryRequirements(device, slot.image, &imageRequirements);
            if (!allocate(imageRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, slot.imageMemory) ||
                vkBindImageMemory(device, slot.image, slot.imageMemory, 0) != VK_SUCCESS) {
                return false;
            }
            VkImageViewCreateInfo view{};
            view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image = slot.image;
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = format;
            view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            if (vkCreateImageView(device, &view, nullptr, &slot.view) != VK_SUCCESS) {
                return false;
            }
            VkFramebufferCreateInfo framebuffer{};
            framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebuffer.renderPass = pass;
            framebuffer.attachmentCount = 1;
            framebuffer.pAttachments = &slot.view;
            framebuffer.width = size;
            framebuffer.height = size;
            framebuffer.layers = 1;
            if (vkCreateFramebuffer(device, &framebuffer, nullptr, &slot.framebuffer) != VK_SUCCESS) {
                return false;
            }
            VkBufferCreateInfo buffer{};
            buffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            buffer.size = uniformStride * 2;
            buffer.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (vkCreateBuffer(device, &buffer, nullptr, &slot.uniforms) != VK_SUCCESS) {
                return false;
            }
            VkMemoryRequirements bufferRequirements{};
            vkGetBufferMemoryRequirements(device, slot.uniforms, &bufferRequirements);
            if (!allocate(bufferRequirements,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          slot.uniformMemory) ||
                vkBindBufferMemory(device, slot.uniforms, slot.uniformMemory, 0) != VK_SUCCESS ||
                vkMapMemory(device, slot.uniformMemory, 0, VK_WHOLE_SIZE, 0, &slot.mapped) != VK_SUCCESS) {
                return false;
            }
            VkDescriptorSetAllocateInfo allocation{};
            allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocation.descriptorPool = pool;
            allocation.descriptorSetCount = 1;
            allocation.pSetLayouts = &receiverLayout;
            if (vkAllocateDescriptorSets(device, &allocation, &slot.descriptor) != VK_SUCCESS) {
                return false;
            }
            const VkDescriptorBufferInfo bufferInfo{slot.uniforms, 0, sizeof(detail::DirectionalShadowUniforms)};
            const VkDescriptorImageInfo imageInfo{sampler, slot.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
            std::array<VkWriteDescriptorSet, 2> writes{};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = slot.descriptor;
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            writes[0].pBufferInfo = &bufferInfo;
            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = slot.descriptor;
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].pImageInfo = &imageInfo;
            vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
        resolution = size;
        return true;
    }
};

VulkanDirectionalShadowMap::VulkanDirectionalShadowMap() : impl_(std::make_unique<Impl>()) {}
VulkanDirectionalShadowMap::~VulkanDirectionalShadowMap() = default;

bool VulkanDirectionalShadowMap::create(VkPhysicalDevice physical, VkDevice device, std::uint32_t slots) {
    impl_->physical = physical;
    impl_->device = device;
    impl_->slots.resize(slots);
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical, &properties);
    const auto alignment = std::max<VkDeviceSize>(1, properties.limits.minUniformBufferOffsetAlignment);
    impl_->uniformStride = (sizeof(detail::DirectionalShadowUniforms) + alignment - 1) / alignment * alignment;
    if (slots == 0 || impl_->uniformStride > std::numeric_limits<std::uint32_t>::max()) {
        shutdown();
        return false;
    }
    return true;
}

void VulkanDirectionalShadowMap::shutdown() {
    if (impl_->device == VK_NULL_HANDLE) {
        return;
    }
    impl_->destroyMaps();
    const auto device = impl_->device;
    if (impl_->meshPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, impl_->meshPipeline, nullptr);
    }
    if (impl_->boxPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, impl_->boxPipeline, nullptr);
    }
    if (impl_->casterLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, impl_->casterLayout, nullptr);
    }
    if (impl_->sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, impl_->sampler, nullptr);
    }
    if (impl_->receiverLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, impl_->receiverLayout, nullptr);
    }
    if (impl_->pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, impl_->pass, nullptr);
    }
    *impl_ = {};
}

bool VulkanDirectionalShadowMap::prepare(std::uint32_t slot, const RenderFrameInfo& frame, VkExtent2D extent) {
    const auto& settings = frame.directionalShadows;
    if (!settings.enabled || !frame.camera3D.enabled || impl_->device == VK_NULL_HANDLE ||
        slot >= impl_->slots.size()) {
        return false;
    }
    const auto light = detail::directionalShadowCamera(frame.camera3D.position, frame.lighting.sunDirection,
                                                       settings.distance, settings.resolution);
    if (!detail::validDirectionalShadowSettings(settings.resolution, settings.distance, settings.depthBias,
                                                settings.normalBias) ||
        !light || !detail::validShadowReceiverCamera(frame.camera3D)) {
        if (!impl_->loggedInvalid) {
            core::logWarning("render",
                             "Invalid directional shadow settings or sun/camera; retaining baseline world rendering");
            impl_->loggedInvalid = true;
        }
        return false;
    }
    // Disabled frames allocate no shadow images, descriptors or pipelines.
    if (!impl_->attemptedCore) {
        impl_->attemptedCore = true;
        impl_->coreReady = impl_->createPass() && impl_->createLayouts() &&
                           impl_->createPipeline(true, impl_->meshPipeline) &&
                           impl_->createPipeline(false, impl_->boxPipeline);
        if (!impl_->coreReady) {
            core::logWarning("render", "Directional shadow depth pass unavailable; retaining baseline world rendering");
        }
    }
    if (!impl_->coreReady) {
        return false;
    }
    if (impl_->resolution != settings.resolution) {
        if (impl_->failedResolution == settings.resolution) {
            return false;
        }
        // Replacing other slots' descriptors/images requires more than this
        // slot's fence. Settings changes are rare; retain the existing queue.
        if (vkDeviceWaitIdle(impl_->device) != VK_SUCCESS) {
            return false;
        }
        impl_->destroyMaps();
        if (!impl_->createMaps(settings.resolution)) {
            impl_->destroyMaps();
            impl_->failedResolution = settings.resolution;
            if (!impl_->loggedAllocation) {
                core::logWarning("render",
                                 "Directional shadow resource allocation failed; retaining baseline world rendering");
                impl_->loggedAllocation = true;
            }
            return false;
        }
        impl_->failedResolution = 0;
    }
    impl_->light = *light;
    auto constants = detail::directionalShadowUniforms(*light, frame.camera3D, extent.width, extent.height,
                                                       settings.depthBias, settings.normalBias, true);
    auto* mapped = static_cast<std::byte*>(impl_->slots[slot].mapped);
    std::memcpy(mapped, &constants, sizeof(constants));
    constants.parameters[3] = 0;
    std::memcpy(mapped + impl_->uniformStride, &constants, sizeof(constants));
    return true;
}

VkDescriptorSetLayout VulkanDirectionalShadowMap::receiverLayout() const { return impl_->receiverLayout; }
const detail::DirectionalShadowCamera& VulkanDirectionalShadowMap::lightCamera() const { return impl_->light; }
std::uint32_t VulkanDirectionalShadowMap::resolution() const { return impl_->resolution; }

void VulkanDirectionalShadowMap::begin(VkCommandBuffer command, std::uint32_t slot) {
    const VkClearValue clear{.depthStencil = {1.0F, 0}};
    const VkExtent2D extent{impl_->resolution, impl_->resolution};
    VkRenderPassBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.renderPass = impl_->pass;
    info.framebuffer = impl_->slots[slot].framebuffer;
    info.renderArea = {{0, 0}, extent};
    info.clearValueCount = 1;
    info.pClearValues = &clear;
    vkCmdBeginRenderPass(command, &info, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
}

void VulkanDirectionalShadowMap::drawMesh(VkCommandBuffer command, const detail::Mat4& model, VkBuffer vertices,
                                          VkBuffer indices, std::uint32_t indexCount) {
    const auto transform = detail::multiply(impl_->light.viewProjection, model);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, impl_->meshPipeline);
    vkCmdPushConstants(command, impl_->casterLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(transform), &transform);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &vertices, &offset);
    vkCmdBindIndexBuffer(command, indices, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(command, indexCount, 1, 0, 0, 0);
}

void VulkanDirectionalShadowMap::drawBox(VkCommandBuffer command, const RenderBox3D& box) {
    const ShadowBoxPush push{impl_->light.viewProjection.value,
                             {box.center.x, box.center.y, box.center.z, 0},
                             {box.halfExtents.x, box.halfExtents.y, box.halfExtents.z, 0}};
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, impl_->boxPipeline);
    vkCmdPushConstants(command, impl_->casterLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
    vkCmdDraw(command, 36, 1, 0, 0);
}

void VulkanDirectionalShadowMap::end(VkCommandBuffer command) { vkCmdEndRenderPass(command); }

void VulkanDirectionalShadowMap::bindReceiver(VkCommandBuffer command, VkPipelineLayout layout, std::uint32_t slot,
                                              bool receives) const {
    const auto offset = receives ? 0U : static_cast<std::uint32_t>(impl_->uniformStride);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &impl_->slots[slot].descriptor, 1,
                            &offset);
}

} // namespace novacore::render
#endif
