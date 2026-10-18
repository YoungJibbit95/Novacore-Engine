#include "VulkanUiRenderer.hpp"
#if NOVACORE_HAS_VULKAN && NOVACORE_HAS_SDL3
#include "novacore/core/Log.hpp"
#include "DynamicMeshUpdates.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

namespace novacore::render {
namespace {
void check(VkResult result) {
    if (result != VK_SUCCESS) throw std::runtime_error("Vulkan UI operation failed: " + std::to_string(result));
}
std::vector<std::uint32_t> shader(const char* name) {
    std::ifstream file(std::filesystem::path(NOVACORE_SHADER_BINARY_DIR) / name, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() <= 0 || file.tellg() % 4 != 0) throw std::runtime_error("Missing Vulkan UI shader");
    std::vector<std::uint32_t> words(static_cast<std::size_t>(file.tellg()) / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(words.size() * 4));
    if (!file) throw std::runtime_error("Cannot read Vulkan UI shader");
    return words;
}
}
struct VulkanUiRenderer::Impl {
    VkDevice device{};
    VkPhysicalDevice physical{};
    VkDescriptorSetLayout setLayout{};
    VkDescriptorPool descriptors{};
    VkSampler sampler{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{};
    struct Buffer { VkBuffer handle{}; VkDeviceMemory memory{}; VkDeviceSize capacity{}; };
    struct Texture {
        VkImage image{}; VkDeviceMemory memory{}; VkImageView view{}; VkDescriptorSet set{};
        std::weak_ptr<const UiTexture> source;
    };
    struct Frame {
        Buffer vertices, indices;
        std::vector<std::shared_ptr<const UiTexture>> retained;
        bool ready = false;
    };
    std::array<Frame, detail::kDynamicMeshFrameCount> frames;
    std::unordered_map<const UiTexture*, Texture> textures;
    std::shared_ptr<UiTexture> white = std::make_shared<UiTexture>(UiTexture{1, 1, {255,255,255,255}});

    std::uint32_t memoryType(std::uint32_t bits, VkMemoryPropertyFlags flags) {
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(physical, &properties);
        for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i)
            if ((bits & (1U << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags) return i;
        throw std::runtime_error("No suitable Vulkan UI memory type");
    }
    void destroy(Buffer& buffer) {
        if (buffer.handle) vkDestroyBuffer(device, buffer.handle, nullptr);
        if (buffer.memory) vkFreeMemory(device, buffer.memory, nullptr);
        buffer = {};
    }
    void resize(Buffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage) {
        if (size <= buffer.capacity) return;
        destroy(buffer); // only called on a completed frame slot
        VkBufferCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = std::max<VkDeviceSize>(65536, size * 2);
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device, &info, nullptr, &buffer.handle));
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, buffer.handle, &requirements);
        VkMemoryAllocateInfo allocation{}; allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(vkAllocateMemory(device, &allocation, nullptr, &buffer.memory));
        check(vkBindBufferMemory(device, buffer.handle, buffer.memory, 0));
        buffer.capacity = info.size;
    }
    void write(Buffer& buffer, const void* data, std::size_t size) {
        if (!size) return;
        void* mapped = nullptr;
        check(vkMapMemory(device, buffer.memory, 0, size, 0, &mapped));
        std::memcpy(mapped, data, size);
        vkUnmapMemory(device, buffer.memory);
    }
    void destroy(Texture& texture) {
        if (texture.set) vkFreeDescriptorSets(device, descriptors, 1, &texture.set);
        if (texture.view) vkDestroyImageView(device, texture.view, nullptr);
        if (texture.image) vkDestroyImage(device, texture.image, nullptr);
        if (texture.memory) vkFreeMemory(device, texture.memory, nullptr);
        texture = {};
    }
    void upload(const std::shared_ptr<const UiTexture>& source, VkCommandPool pool, VkQueue queue) {
        if (textures.contains(source.get())) return;
        Texture texture{};
        Buffer staging{};
        VkCommandBuffer command{};
        bool submitted = false;
        try {
            resize(staging, source->rgba.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
            write(staging, source->rgba.data(), source->rgba.size());
            VkImageCreateInfo image{}; image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            image.imageType = VK_IMAGE_TYPE_2D;
            image.format = VK_FORMAT_R8G8B8A8_UNORM;
            image.extent = {source->width, source->height, 1};
            image.mipLevels = image.arrayLayers = 1;
            image.samples = VK_SAMPLE_COUNT_1_BIT;
            image.tiling = VK_IMAGE_TILING_OPTIMAL;
            image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            check(vkCreateImage(device, &image, nullptr, &texture.image));
            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements(device, texture.image, &requirements);
            VkMemoryAllocateInfo allocation{}; allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            check(vkAllocateMemory(device, &allocation, nullptr, &texture.memory));
            check(vkBindImageMemory(device, texture.image, texture.memory, 0));
            VkCommandBufferAllocateInfo commandInfo{}; commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            commandInfo.commandPool = pool; commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; commandInfo.commandBufferCount = 1;
            check(vkAllocateCommandBuffers(device, &commandInfo, &command));
            VkCommandBufferBeginInfo begin{}; begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            check(vkBeginCommandBuffer(command, &begin));
            VkImageMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = texture.image;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                0, nullptr, 0, nullptr, 1, &barrier);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = image.extent;
            vkCmdCopyBufferToImage(command, staging.handle, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                0, nullptr, 0, nullptr, 1, &barrier);
            check(vkEndCommandBuffer(command));
            VkSubmitInfo submit{}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
            check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE)); submitted = true;
            // Atlas creation is infrequent. Use the existing queue; no second submission lifecycle.
            check(vkQueueWaitIdle(queue)); submitted = false;
            vkFreeCommandBuffers(device, pool, 1, &command); command = VK_NULL_HANDLE;
            destroy(staging);
            VkImageViewCreateInfo view{}; view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image = texture.image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = image.format;
            view.subresourceRange = barrier.subresourceRange;
            check(vkCreateImageView(device, &view, nullptr, &texture.view));
            VkDescriptorSetAllocateInfo set{}; set.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            set.descriptorPool = descriptors; set.descriptorSetCount = 1; set.pSetLayouts = &setLayout;
            check(vkAllocateDescriptorSets(device, &set, &texture.set));
            VkDescriptorImageInfo sampled{sampler, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet update{}; update.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            update.dstSet = texture.set; update.descriptorCount = 1;
            update.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; update.pImageInfo = &sampled;
            vkUpdateDescriptorSets(device, 1, &update, 0, nullptr);
            texture.source = source;
            textures.emplace(source.get(), texture);
        } catch (...) {
            if (submitted) vkQueueWaitIdle(queue);
            if (command) vkFreeCommandBuffers(device, pool, 1, &command);
            destroy(staging); destroy(texture); throw;
        }
    }
};
VulkanUiRenderer::VulkanUiRenderer() : impl_(std::make_unique<Impl>()) {}
VulkanUiRenderer::~VulkanUiRenderer() { shutdown(); }
bool VulkanUiRenderer::create(VkPhysicalDevice physical, VkDevice device, VkRenderPass pass) {
    shutdown();
    auto& p = *impl_; p.physical = physical; p.device = device;
    VkShaderModule vert{}, frag{};
    try {
        VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo set{}; set.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        set.bindingCount = 1; set.pBindings = &binding;
        check(vkCreateDescriptorSetLayout(device, &set, nullptr, &p.setLayout));
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256};
        VkDescriptorPoolCreateInfo pool{}; pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; pool.maxSets = 256; pool.poolSizeCount = 1; pool.pPoolSizes = &size;
        check(vkCreateDescriptorPool(device, &pool, nullptr, &p.descriptors));
        VkSamplerCreateInfo sampler{}; sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        check(vkCreateSampler(device, &sampler, nullptr, &p.sampler));
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, 8};
        VkPipelineLayoutCreateInfo layout{}; layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.setLayoutCount = 1; layout.pSetLayouts = &p.setLayout; layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
        check(vkCreatePipelineLayout(device, &layout, nullptr, &p.layout));
        const auto vertexCode = shader("ui_geometry.vert.spv"), fragmentCode = shader("ui_geometry.frag.spv");
        VkShaderModuleCreateInfo module{}; module.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        module.codeSize = vertexCode.size() * 4; module.pCode = vertexCode.data();
        check(vkCreateShaderModule(device, &module, nullptr, &vert));
        module.codeSize = fragmentCode.size() * 4; module.pCode = fragmentCode.data();
        check(vkCreateShaderModule(device, &module, nullptr, &frag));
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vert; stages[0].pName = "main";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = frag; stages[1].pName = "main";
        VkVertexInputBindingDescription vb{0, sizeof(UiVertex), VK_VERTEX_INPUT_RATE_VERTEX};
        std::array<VkVertexInputAttributeDescription,3> attrs{{
            {0,0,VK_FORMAT_R32G32_SFLOAT,offsetof(UiVertex,position)},
            {1,0,VK_FORMAT_R32G32_SFLOAT,offsetof(UiVertex,texcoord)},
            {2,0,VK_FORMAT_R8G8B8A8_UNORM,offsetof(UiVertex,color)}}};
        VkPipelineVertexInputStateCreateInfo input{}; input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        input.vertexBindingDescriptionCount = 1; input.pVertexBindingDescriptions = &vb;
        input.vertexAttributeDescriptionCount = 3; input.pVertexAttributeDescriptions = attrs.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{}; assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{}; viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{}; raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = VK_CULL_MODE_NONE; raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo samples{}; samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{}; depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        VkPipelineColorBlendAttachmentState blend{};
        blend.blendEnable = VK_TRUE; blend.srcColorBlendFactor = blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstColorBlendFactor = blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = blend.alphaBlendOp = VK_BLEND_OP_ADD; blend.colorWriteMask = 15;
        VkPipelineColorBlendStateCreateInfo color{}; color.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color.attachmentCount = 1; color.pAttachments = &blend;
        const VkDynamicState states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{}; dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = states;
        VkGraphicsPipelineCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount = 2; info.pStages = stages; info.pVertexInputState = &input; info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport; info.pRasterizationState = &raster; info.pMultisampleState = &samples;
        info.pDepthStencilState = &depth; info.pColorBlendState = &color; info.pDynamicState = &dynamic;
        info.layout = p.layout; info.renderPass = pass;
        check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &p.pipeline));
        vkDestroyShaderModule(device, vert, nullptr); vkDestroyShaderModule(device, frag, nullptr);
        return true;
    } catch (const std::exception& error) {
        if (vert) vkDestroyShaderModule(device, vert, nullptr);
        if (frag) vkDestroyShaderModule(device, frag, nullptr);
        core::logWarning("ui", error.what()); shutdown(); return false;
    }
}
void VulkanUiRenderer::shutdown() {
    auto& p = *impl_;
    if (!p.device) return;
    for (auto& frame : p.frames) { p.destroy(frame.vertices); p.destroy(frame.indices); frame.retained.clear(); frame.ready = false; }
    for (auto& [key, texture] : p.textures) { (void)key; p.destroy(texture); }
    p.textures.clear();
    if (p.pipeline) vkDestroyPipeline(p.device, p.pipeline, nullptr);
    if (p.layout) vkDestroyPipelineLayout(p.device, p.layout, nullptr);
    if (p.sampler) vkDestroySampler(p.device, p.sampler, nullptr);
    if (p.descriptors) vkDestroyDescriptorPool(p.device, p.descriptors, nullptr);
    if (p.setLayout) vkDestroyDescriptorSetLayout(p.device, p.setLayout, nullptr);
    p.pipeline = {}; p.layout = {}; p.sampler = {}; p.descriptors = {}; p.setLayout = {}; p.device = {};
}
bool VulkanUiRenderer::prepare(const UiDrawData& data, std::uint32_t slot, VkCommandPool pool, VkQueue queue) {
    auto& p = *impl_;
    if (!p.pipeline || slot >= p.frames.size()) return false;
    auto& frame = p.frames[slot];
    frame.ready = false;
    frame.retained.clear(); // the engine has waited this slot's fence
    for (auto it = p.textures.begin(); it != p.textures.end();) {
        if (it->second.source.expired()) { p.destroy(it->second); it = p.textures.erase(it); }
        else ++it;
    }
    if (data.commands.empty() || data.indices.empty()) return true;
    if (!data.valid()) { core::logWarning("ui", "Rejected invalid UI draw data"); return false; }
    try {
        p.resize(frame.vertices, data.vertices.size() * sizeof(UiVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        p.resize(frame.indices, data.indices.size() * sizeof(std::uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        p.write(frame.vertices, data.vertices.data(), data.vertices.size() * sizeof(UiVertex));
        p.write(frame.indices, data.indices.data(), data.indices.size() * sizeof(std::uint32_t));
        for (const auto& draw : data.commands) {
            auto texture = draw.texture ? draw.texture : p.white;
            p.upload(texture, pool, queue); frame.retained.push_back(std::move(texture));
        }
        frame.ready = true;
        return true;
    } catch (const std::exception& error) { core::logWarning("ui", error.what()); return false; }
}
std::size_t VulkanUiRenderer::record(VkCommandBuffer command, const UiDrawData& data, std::uint32_t slot, VkExtent2D extent) {
    auto& p = *impl_;
    if (slot >= p.frames.size() || !p.frames[slot].ready || data.commands.empty()) return 0;
    const auto& frame = p.frames[slot];
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, p.pipeline);
    VkViewport viewport{0,0,static_cast<float>(extent.width),static_cast<float>(extent.height),0,1};
    vkCmdSetViewport(command, 0, 1, &viewport);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &frame.vertices.handle, &offset);
    vkCmdBindIndexBuffer(command, frame.indices.handle, 0, VK_INDEX_TYPE_UINT32);
    const float size[]{data.width, data.height};
    vkCmdPushConstants(command, p.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(size), size);
    const float sx = viewport.width / data.width, sy = viewport.height / data.height;
    std::size_t draws = 0;
    for (std::size_t i = 0; i < data.commands.size(); ++i) {
        const auto& draw = data.commands[i];
        const auto left = static_cast<std::int32_t>(std::clamp(std::floor(draw.clip[0]*sx),0.0F,viewport.width));
        const auto top = static_cast<std::int32_t>(std::clamp(std::floor(draw.clip[1]*sy),0.0F,viewport.height));
        const auto right = static_cast<std::int32_t>(std::clamp(std::ceil((draw.clip[0]+draw.clip[2])*sx),0.0F,viewport.width));
        const auto bottom = static_cast<std::int32_t>(std::clamp(std::ceil((draw.clip[1]+draw.clip[3])*sy),0.0F,viewport.height));
        if (!draw.indexCount || right <= left || bottom <= top) continue;
        VkRect2D scissor{{left,top},{static_cast<std::uint32_t>(right-left),static_cast<std::uint32_t>(bottom-top)}};
        vkCmdSetScissor(command, 0, 1, &scissor);
        const auto set = p.textures.at(frame.retained[i].get()).set;
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, 0, 1, &set, 0, nullptr);
        vkCmdDrawIndexed(command, draw.indexCount, 1, draw.firstIndex, 0, 0); ++draws;
    }
    return draws;
}
} // namespace novacore::render
#endif
