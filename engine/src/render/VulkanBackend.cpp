#include "novacore/render/VulkanBackend.hpp"

#include "novacore/core/Log.hpp"
#include "novacore/render/Renderer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#if NOVACORE_HAS_VULKAN && NOVACORE_HAS_SDL3
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <vulkan/vulkan.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <cstring>
#endif

#ifndef NOVACORE_SHADER_BINARY_DIR
#define NOVACORE_SHADER_BINARY_DIR ""
#endif

namespace novacore::render {

namespace {

constexpr std::uint32_t kMaxFramesInFlight = 2;

#if NOVACORE_HAS_VULKAN && NOVACORE_HAS_SDL3

[[nodiscard]] std::string vkResultName(VkResult result) {
    switch (result) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_EVENT_SET: return "VK_EVENT_SET";
    case VK_EVENT_RESET: return "VK_EVENT_RESET";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    default: return "VkResult(" + std::to_string(static_cast<int>(result)) + ")";
    }
}

[[nodiscard]] bool vkOk(VkResult result, std::string_view label) {
    if (result == VK_SUCCESS) {
        return true;
    }
    core::logWarning("render", std::string(label) + " failed: " + vkResultName(result));
    return false;
}

[[nodiscard]] std::string deviceTypeName(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "cpu";
    default: return "other";
    }
}

[[nodiscard]] std::vector<char> readBinaryFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) {
        core::logWarning("render", "Failed to open shader binary: " + path.generic_string());
        return {};
    }

    const auto size = static_cast<std::size_t>(file.tellg());
    if (size == 0) {
        core::logWarning("render", "Shader binary is empty: " + path.generic_string());
        return {};
    }

    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    return buffer;
}

struct QueueFamilySelection final {
    std::optional<std::uint32_t> graphics;
    std::optional<std::uint32_t> present;

    [[nodiscard]] bool complete() const {
        return graphics.has_value() && present.has_value();
    }
};

struct SwapchainSupport final {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;

    [[nodiscard]] bool usable() const {
        return !formats.empty() && !presentModes.empty();
    }
};

[[nodiscard]] QueueFamilySelection findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
    std::uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());

    QueueFamilySelection selection{};
    for (std::uint32_t index = 0; index < familyCount; ++index) {
        if ((families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            selection.graphics = index;
        }

        VkBool32 supportsPresent = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &supportsPresent);
        if (supportsPresent == VK_TRUE) {
            selection.present = index;
        }

        if (selection.complete()) {
            break;
        }
    }
    return selection;
}

[[nodiscard]] bool hasRequiredDeviceExtensions(VkPhysicalDevice device) {
    std::uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());

    for (const auto& extension : extensions) {
        if (std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] SwapchainSupport querySwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
    SwapchainSupport support{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &support.capabilities);

    std::uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    support.formats.resize(formatCount);
    if (formatCount > 0) {
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, support.formats.data());
    }

    std::uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    support.presentModes.resize(presentModeCount);
    if (presentModeCount > 0) {
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, support.presentModes.data());
    }
    return support;
}

[[nodiscard]] VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }

    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }

    return formats.front();
}

[[nodiscard]] VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes) {
    for (const auto mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

[[nodiscard]] VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, int width, int height) {
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    VkExtent2D extent{
        static_cast<std::uint32_t>(std::max(1, width)),
        static_cast<std::uint32_t>(std::max(1, height)),
    };
    extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return extent;
}

#endif

} // namespace

struct VulkanBackend::Impl final {
#if NOVACORE_HAS_VULKAN && NOVACORE_HAS_SDL3
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    std::uint32_t graphicsFamily = 0;
    std::uint32_t presentFamily = 0;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent{};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkImageLayout> imageLayouts;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kMaxFramesInFlight> commandBuffers{};
    std::array<VkSemaphore, kMaxFramesInFlight> imageAvailable{};
    std::array<VkSemaphore, kMaxFramesInFlight> renderFinished{};
    std::array<VkFence, kMaxFramesInFlight> inFlight{};
    std::uint32_t currentFrame = 0;
    std::uint32_t acquiredImageIndex = 0;
    bool frameActive = false;
#endif
    std::array<float, 4> clearColor{0.03F, 0.04F, 0.06F, 1.0F};
    std::string selectedDeviceName = "none";
    bool isReady = false;

#if NOVACORE_HAS_VULKAN && NOVACORE_HAS_SDL3
    [[nodiscard]] bool createInstance() {
        std::uint32_t extensionCount = 0;
        const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
        if (sdlExtensions == nullptr || extensionCount == 0) {
            core::logWarning("render", "SDL did not provide Vulkan instance extensions: " + std::string(SDL_GetError()));
            return false;
        }

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "NovaCore";
        appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        appInfo.pEngineName = "NovaCore";
        appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        appInfo.apiVersion = VK_API_VERSION_1_2;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = extensionCount;
        createInfo.ppEnabledExtensionNames = sdlExtensions;

        return vkOk(vkCreateInstance(&createInfo, nullptr, &instance), "vkCreateInstance");
    }

    [[nodiscard]] bool createSurface(platform::Window& window) {
        auto* sdlWindow = static_cast<SDL_Window*>(window.nativeHandle());
        if (sdlWindow == nullptr) {
            core::logWarning("render", "Cannot create Vulkan surface without an SDL window");
            return false;
        }

        if (!SDL_Vulkan_CreateSurface(sdlWindow, instance, nullptr, &surface)) {
            core::logWarning("render", "SDL_Vulkan_CreateSurface failed: " + std::string(SDL_GetError()));
            return false;
        }
        return true;
    }

    [[nodiscard]] bool selectPhysicalDevice() {
        std::uint32_t deviceCount = 0;
        if (!vkOk(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr), "vkEnumeratePhysicalDevices")) {
            return false;
        }
        if (deviceCount == 0) {
            core::logWarning("render", "No Vulkan physical devices found");
            return false;
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

        int bestScore = -1;
        VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
        QueueFamilySelection bestQueues{};
        std::string bestName;
        std::string bestType;

        for (const auto candidate : devices) {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);

            const auto queues = findQueueFamilies(candidate, surface);
            const bool extensionsOk = hasRequiredDeviceExtensions(candidate);
            const auto swapSupport = extensionsOk ? querySwapchainSupport(candidate, surface) : SwapchainSupport{};
            if (!queues.complete() || !extensionsOk || !swapSupport.usable()) {
                continue;
            }

            int score = 10;
            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                score += 1000;
            } else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
                score += 500;
            }
            score += static_cast<int>(properties.limits.maxImageDimension2D / 1024U);

            if (score > bestScore) {
                bestScore = score;
                bestDevice = candidate;
                bestQueues = queues;
                bestName = properties.deviceName;
                bestType = deviceTypeName(properties.deviceType);
            }
        }

        if (bestDevice == VK_NULL_HANDLE || !bestQueues.complete()) {
            core::logWarning("render", "No suitable Vulkan physical device supports graphics, present and swapchain");
            return false;
        }

        physicalDevice = bestDevice;
        graphicsFamily = *bestQueues.graphics;
        presentFamily = *bestQueues.present;
        selectedDeviceName = bestName;
        core::logInfo("render", "Vulkan physical device selected: " + bestName + " (" + bestType + ")");
        return true;
    }

    [[nodiscard]] bool createLogicalDevice() {
        const float queuePriority = 1.0F;
        std::array<std::uint32_t, 2> uniqueFamilies{graphicsFamily, presentFamily};
        std::uint32_t queueCreateInfoCount = 1;
        if (graphicsFamily != presentFamily) {
            queueCreateInfoCount = 2;
        }

        std::array<VkDeviceQueueCreateInfo, 2> queueCreateInfos{};
        for (std::uint32_t index = 0; index < queueCreateInfoCount; ++index) {
            queueCreateInfos[index].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfos[index].queueFamilyIndex = uniqueFamilies[index];
            queueCreateInfos[index].queueCount = 1;
            queueCreateInfos[index].pQueuePriorities = &queuePriority;
        }

        VkPhysicalDeviceFeatures features{};
        const std::array<const char*, 1> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = queueCreateInfoCount;
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &features;
        createInfo.enabledExtensionCount = static_cast<std::uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();

        if (!vkOk(vkCreateDevice(physicalDevice, &createInfo, nullptr, &device), "vkCreateDevice")) {
            return false;
        }

        vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
        vkGetDeviceQueue(device, presentFamily, 0, &presentQueue);
        return true;
    }

    [[nodiscard]] bool createSwapchain(platform::Window& window) {
        const auto support = querySwapchainSupport(physicalDevice, surface);
        if (!support.usable()) {
            core::logWarning("render", "Selected Vulkan device lost swapchain support during creation");
            return false;
        }

        const auto surfaceFormat = chooseSurfaceFormat(support.formats);
        const auto presentMode = choosePresentMode(support.presentModes);
        const auto extent = chooseExtent(support.capabilities, window.width(), window.height());

        std::uint32_t imageCount = support.capabilities.minImageCount + 1U;
        if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount) {
            imageCount = support.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        const std::array<std::uint32_t, 2> queueFamilyIndices{graphicsFamily, presentFamily};
        if (graphicsFamily != presentFamily) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = static_cast<std::uint32_t>(queueFamilyIndices.size());
            createInfo.pQueueFamilyIndices = queueFamilyIndices.data();
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        createInfo.preTransform = support.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        if (!vkOk(vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain), "vkCreateSwapchainKHR")) {
            return false;
        }

        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
        swapchainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());

        swapchainFormat = surfaceFormat.format;
        swapchainExtent = extent;
        imageLayouts.assign(imageCount, VK_IMAGE_LAYOUT_UNDEFINED);

        core::logInfo(
            "render",
            "Vulkan swapchain created: " + std::to_string(extent.width) + "x" + std::to_string(extent.height) +
                " images=" + std::to_string(imageCount));
        return true;
    }

    [[nodiscard]] bool createImageViews() {
        swapchainImageViews.resize(swapchainImages.size());
        for (std::size_t index = 0; index < swapchainImages.size(); ++index) {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = swapchainImages[index];
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = swapchainFormat;
            createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            if (!vkOk(vkCreateImageView(device, &createInfo, nullptr, &swapchainImageViews[index]), "vkCreateImageView")) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool createRenderPass() {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = swapchainFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorReference{};
        colorReference.attachment = 0;
        colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        createInfo.attachmentCount = 1;
        createInfo.pAttachments = &colorAttachment;
        createInfo.subpassCount = 1;
        createInfo.pSubpasses = &subpass;
        createInfo.dependencyCount = 1;
        createInfo.pDependencies = &dependency;

        return vkOk(vkCreateRenderPass(device, &createInfo, nullptr, &renderPass), "vkCreateRenderPass");
    }

    [[nodiscard]] bool createFramebuffers() {
        framebuffers.resize(swapchainImageViews.size());
        for (std::size_t index = 0; index < swapchainImageViews.size(); ++index) {
            const VkImageView attachments[] = {swapchainImageViews[index]};

            VkFramebufferCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            createInfo.renderPass = renderPass;
            createInfo.attachmentCount = 1;
            createInfo.pAttachments = attachments;
            createInfo.width = swapchainExtent.width;
            createInfo.height = swapchainExtent.height;
            createInfo.layers = 1;

            if (!vkOk(vkCreateFramebuffer(device, &createInfo, nullptr, &framebuffers[index]), "vkCreateFramebuffer")) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] VkShaderModule createShaderModule(const std::vector<char>& bytes) {
        if (bytes.empty() || (bytes.size() % sizeof(std::uint32_t)) != 0) {
            core::logWarning("render", "Invalid SPIR-V bytecode size");
            return VK_NULL_HANDLE;
        }

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = bytes.size();
        createInfo.pCode = reinterpret_cast<const std::uint32_t*>(bytes.data());

        VkShaderModule module = VK_NULL_HANDLE;
        if (!vkOk(vkCreateShaderModule(device, &createInfo, nullptr, &module), "vkCreateShaderModule")) {
            return VK_NULL_HANDLE;
        }
        return module;
    }

    [[nodiscard]] bool createGraphicsPipeline() {
        const auto shaderDirectory = std::filesystem::path(NOVACORE_SHADER_BINARY_DIR);
        const auto vertexShaderBytes = readBinaryFile(shaderDirectory / "debug_triangle.vert.spv");
        const auto fragmentShaderBytes = readBinaryFile(shaderDirectory / "debug_triangle.frag.spv");
        if (vertexShaderBytes.empty() || fragmentShaderBytes.empty()) {
            core::logWarning("render", "Vulkan debug triangle pipeline skipped because shader binaries are missing");
            return false;
        }

        const VkShaderModule vertexShader = createShaderModule(vertexShaderBytes);
        const VkShaderModule fragmentShader = createShaderModule(fragmentShaderBytes);
        if (vertexShader == VK_NULL_HANDLE || fragmentShader == VK_NULL_HANDLE) {
            if (vertexShader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, vertexShader, nullptr);
            }
            if (fragmentShader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, fragmentShader, nullptr);
            }
            return false;
        }

        VkPipelineShaderStageCreateInfo vertexStage{};
        vertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertexStage.module = vertexShader;
        vertexStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragmentStage{};
        fragmentStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragmentStage.module = fragmentShader;
        fragmentStage.pName = "main";

        const VkPipelineShaderStageCreateInfo shaderStages[] = {vertexStage, fragmentStage};

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport{};
        viewport.x = 0.0F;
        viewport.y = 0.0F;
        viewport.width = static_cast<float>(swapchainExtent.width);
        viewport.height = static_cast<float>(swapchainExtent.height);
        viewport.minDepth = 0.0F;
        viewport.maxDepth = 1.0F;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = swapchainExtent;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0F;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        if (!vkOk(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout), "vkCreatePipelineLayout")) {
            vkDestroyShaderModule(device, fragmentShader, nullptr);
            vkDestroyShaderModule(device, vertexShader, nullptr);
            return false;
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        const bool success = vkOk(
            vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline),
            "vkCreateGraphicsPipelines");

        vkDestroyShaderModule(device, fragmentShader, nullptr);
        vkDestroyShaderModule(device, vertexShader, nullptr);

        if (success) {
            core::logInfo("render", "Vulkan debug triangle graphics pipeline created");
        }
        return success;
    }

    [[nodiscard]] bool createCommandPoolAndBuffers() {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = graphicsFamily;

        if (!vkOk(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool), "vkCreateCommandPool")) {
            return false;
        }

        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool = commandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = static_cast<std::uint32_t>(commandBuffers.size());

        return vkOk(vkAllocateCommandBuffers(device, &allocateInfo, commandBuffers.data()), "vkAllocateCommandBuffers");
    }

    [[nodiscard]] bool createSyncObjects() {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (std::size_t index = 0; index < kMaxFramesInFlight; ++index) {
            if (!vkOk(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailable[index]), "vkCreateSemaphore(imageAvailable)") ||
                !vkOk(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinished[index]), "vkCreateSemaphore(renderFinished)") ||
                !vkOk(vkCreateFence(device, &fenceInfo, nullptr, &inFlight[index]), "vkCreateFence")) {
                return false;
            }
        }
        return true;
    }

    void recordClearCommandBuffer(VkCommandBuffer commandBuffer, std::uint32_t imageIndex, std::array<float, 4> color) {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        VkClearValue clearValue{};
        clearValue.color = {{color[0], color[1], color[2], color[3]}};

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffers[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapchainExtent;
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        if (graphicsPipeline != VK_NULL_HANDLE) {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
            vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        }

        vkCmdEndRenderPass(commandBuffer);

        imageLayouts[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkEndCommandBuffer(commandBuffer);
    }

    void destroySwapchainResources() {
        if (device == VK_NULL_HANDLE) {
            return;
        }

        if (graphicsPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, graphicsPipeline, nullptr);
            graphicsPipeline = VK_NULL_HANDLE;
        }

        if (pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            pipelineLayout = VK_NULL_HANDLE;
        }

        for (auto framebuffer : framebuffers) {
            if (framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device, framebuffer, nullptr);
            }
        }
        framebuffers.clear();

        if (renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, renderPass, nullptr);
            renderPass = VK_NULL_HANDLE;
        }

        for (auto imageView : swapchainImageViews) {
            if (imageView != VK_NULL_HANDLE) {
                vkDestroyImageView(device, imageView, nullptr);
            }
        }
        swapchainImageViews.clear();
        swapchainImages.clear();
        imageLayouts.clear();

        if (swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }
    }
#endif
};

VulkanBackend::VulkanBackend()
    : impl_(std::make_unique<Impl>()) {}

VulkanBackend::~VulkanBackend() {
    shutdown();
}

VulkanBackend::VulkanBackend(VulkanBackend&&) noexcept = default;
VulkanBackend& VulkanBackend::operator=(VulkanBackend&&) noexcept = default;

bool VulkanBackend::create(platform::Window& window, std::array<float, 4> clearColor) {
    impl_->clearColor = clearColor;

#if NOVACORE_HAS_VULKAN && NOVACORE_HAS_SDL3
    if (window.isHeadless()) {
        core::logWarning("render", "Vulkan backend skipped because the window is headless");
        return false;
    }

    if (!impl_->createInstance() ||
        !impl_->createSurface(window) ||
        !impl_->selectPhysicalDevice() ||
        !impl_->createLogicalDevice() ||
        !impl_->createSwapchain(window) ||
        !impl_->createImageViews() ||
        !impl_->createRenderPass() ||
        !impl_->createFramebuffers() ||
        !impl_->createCommandPoolAndBuffers() ||
        !impl_->createSyncObjects()) {
        shutdown();
        return false;
    }

    (void)impl_->createGraphicsPipeline();

    impl_->isReady = true;
    core::logInfo("render", "Vulkan clear/present backend created");
    return true;
#else
    (void)window;
    core::logWarning("render", "Vulkan backend was not compiled into NovaCore");
    return false;
#endif
}

void VulkanBackend::beginFrame(const RenderFrameInfo& frame) {
    impl_->clearColor = frame.clearColor;

#if NOVACORE_HAS_VULKAN && NOVACORE_HAS_SDL3
    if (!impl_->isReady || impl_->device == VK_NULL_HANDLE || impl_->swapchain == VK_NULL_HANDLE) {
        return;
    }

    vkWaitForFences(impl_->device, 1, &impl_->inFlight[impl_->currentFrame], VK_TRUE, UINT64_MAX);

    const VkResult acquireResult = vkAcquireNextImageKHR(
        impl_->device,
        impl_->swapchain,
        UINT64_MAX,
        impl_->imageAvailable[impl_->currentFrame],
        VK_NULL_HANDLE,
        &impl_->acquiredImageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || acquireResult == VK_SUBOPTIMAL_KHR) {
        core::logWarning("render", "Vulkan swapchain needs rebuild: " + vkResultName(acquireResult));
        impl_->frameActive = false;
        return;
    }
    if (acquireResult != VK_SUCCESS) {
        core::logWarning("render", "vkAcquireNextImageKHR failed: " + vkResultName(acquireResult));
        impl_->frameActive = false;
        return;
    }

    vkResetFences(impl_->device, 1, &impl_->inFlight[impl_->currentFrame]);
    vkResetCommandBuffer(impl_->commandBuffers[impl_->currentFrame], 0);
    impl_->recordClearCommandBuffer(
        impl_->commandBuffers[impl_->currentFrame],
        impl_->acquiredImageIndex,
        frame.clearColor);
    impl_->frameActive = true;
#else
    (void)frame;
#endif
}

void VulkanBackend::endFrame() {
#if NOVACORE_HAS_VULKAN && NOVACORE_HAS_SDL3
    if (!impl_->isReady || !impl_->frameActive) {
        return;
    }

    const VkSemaphore waitSemaphores[] = {impl_->imageAvailable[impl_->currentFrame]};
    const VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    const VkSemaphore signalSemaphores[] = {impl_->renderFinished[impl_->currentFrame]};

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &impl_->commandBuffers[impl_->currentFrame];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (!vkOk(vkQueueSubmit(impl_->graphicsQueue, 1, &submitInfo, impl_->inFlight[impl_->currentFrame]), "vkQueueSubmit")) {
        impl_->frameActive = false;
        return;
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &impl_->swapchain;
    presentInfo.pImageIndices = &impl_->acquiredImageIndex;

    const VkResult presentResult = vkQueuePresentKHR(impl_->presentQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        core::logWarning("render", "Vulkan present requested swapchain rebuild: " + vkResultName(presentResult));
    } else if (presentResult != VK_SUCCESS) {
        core::logWarning("render", "vkQueuePresentKHR failed: " + vkResultName(presentResult));
    }

    impl_->currentFrame = (impl_->currentFrame + 1U) % kMaxFramesInFlight;
    impl_->frameActive = false;
#endif
}

void VulkanBackend::shutdown() {
#if NOVACORE_HAS_VULKAN && NOVACORE_HAS_SDL3
    if (impl_->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(impl_->device);
    }

    impl_->destroySwapchainResources();

    if (impl_->device != VK_NULL_HANDLE) {
        for (std::size_t index = 0; index < kMaxFramesInFlight; ++index) {
            if (impl_->imageAvailable[index] != VK_NULL_HANDLE) {
                vkDestroySemaphore(impl_->device, impl_->imageAvailable[index], nullptr);
                impl_->imageAvailable[index] = VK_NULL_HANDLE;
            }
            if (impl_->renderFinished[index] != VK_NULL_HANDLE) {
                vkDestroySemaphore(impl_->device, impl_->renderFinished[index], nullptr);
                impl_->renderFinished[index] = VK_NULL_HANDLE;
            }
            if (impl_->inFlight[index] != VK_NULL_HANDLE) {
                vkDestroyFence(impl_->device, impl_->inFlight[index], nullptr);
                impl_->inFlight[index] = VK_NULL_HANDLE;
            }
        }

        if (impl_->commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(impl_->device, impl_->commandPool, nullptr);
            impl_->commandPool = VK_NULL_HANDLE;
        }

        vkDestroyDevice(impl_->device, nullptr);
        impl_->device = VK_NULL_HANDLE;
    }

    if (impl_->surface != VK_NULL_HANDLE && impl_->instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(impl_->instance, impl_->surface, nullptr);
        impl_->surface = VK_NULL_HANDLE;
    }

    if (impl_->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(impl_->instance, nullptr);
        impl_->instance = VK_NULL_HANDLE;
    }

    impl_->physicalDevice = VK_NULL_HANDLE;
    impl_->graphicsQueue = VK_NULL_HANDLE;
    impl_->presentQueue = VK_NULL_HANDLE;
    impl_->commandBuffers = {};
    impl_->currentFrame = 0;
    impl_->acquiredImageIndex = 0;
    impl_->frameActive = false;
#endif

    impl_->isReady = false;
    impl_->selectedDeviceName = "none";
}

bool VulkanBackend::ready() const {
    return impl_->isReady;
}

std::string_view VulkanBackend::deviceName() const {
    return impl_->selectedDeviceName;
}

} // namespace novacore::render
