#include "VulkanBackend.hpp"

#include "novacore/core/Log.hpp"
#include "novacore/render/Renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
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

struct Mat4 final {
    std::array<float, 16> value{};

    [[nodiscard]] float& at(std::size_t row, std::size_t column) {
        return value[(column * 4U) + row];
    }

    [[nodiscard]] float at(std::size_t row, std::size_t column) const {
        return value[(column * 4U) + row];
    }
};

struct WorldBoxPushConstants final {
    std::array<float, 16> viewProjection{};
    std::array<float, 4> center{};
    std::array<float, 4> halfExtents{};
    std::array<float, 4> color{};
};

struct WorldMeshPushConstants final {
    std::array<float, 16> worldViewProjection{};
    std::array<float, 4> color{};
};

struct VulkanMeshVertex final {
    float position[3]{};
    float normal[3]{0.0F, 1.0F, 0.0F};
};

[[nodiscard]] float degreesToRadians(float degrees) {
    constexpr float kPi = 3.14159265358979323846F;
    return degrees * (kPi / 180.0F);
}

[[nodiscard]] novacore::math::Vec3 normalized(novacore::math::Vec3 value) {
    const float lengthSquared = value.lengthSquared();
    if (lengthSquared <= 0.000001F) {
        return novacore::math::Vec3{0.0F, 0.0F, 1.0F};
    }
    const float invLength = 1.0F / std::sqrt(lengthSquared);
    return novacore::math::Vec3{value.x * invLength, value.y * invLength, value.z * invLength};
}

[[nodiscard]] float dot(novacore::math::Vec3 a, novacore::math::Vec3 b) {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

[[nodiscard]] novacore::math::Vec3 cross(novacore::math::Vec3 a, novacore::math::Vec3 b) {
    return novacore::math::Vec3{
        (a.y * b.z) - (a.z * b.y),
        (a.z * b.x) - (a.x * b.z),
        (a.x * b.y) - (a.y * b.x),
    };
}

[[nodiscard]] Mat4 identity() {
    Mat4 result{};
    result.at(0, 0) = 1.0F;
    result.at(1, 1) = 1.0F;
    result.at(2, 2) = 1.0F;
    result.at(3, 3) = 1.0F;
    return result;
}

[[nodiscard]] Mat4 multiply(const Mat4& lhs, const Mat4& rhs) {
    Mat4 result{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            float value = 0.0F;
            for (std::size_t k = 0; k < 4; ++k) {
                value += lhs.at(row, k) * rhs.at(k, column);
            }
            result.at(row, column) = value;
        }
    }
    return result;
}

[[nodiscard]] Mat4 translation(novacore::math::Vec3 value) {
    Mat4 result = identity();
    result.at(0, 3) = value.x;
    result.at(1, 3) = value.y;
    result.at(2, 3) = value.z;
    return result;
}

[[nodiscard]] Mat4 scale(novacore::math::Vec3 value) {
    Mat4 result{};
    result.at(0, 0) = value.x;
    result.at(1, 1) = value.y;
    result.at(2, 2) = value.z;
    result.at(3, 3) = 1.0F;
    return result;
}

[[nodiscard]] Mat4 rotationY(float yawDegrees) {
    const float yaw = degreesToRadians(yawDegrees);
    const float s = std::sin(yaw);
    const float c = std::cos(yaw);

    Mat4 result = identity();
    result.at(0, 0) = c;
    result.at(0, 2) = s;
    result.at(2, 0) = -s;
    result.at(2, 2) = c;
    return result;
}

[[nodiscard]] Mat4 modelMatrixForMesh(const RenderMesh3D& mesh) {
    return multiply(translation(mesh.position), multiply(rotationY(mesh.yawDegrees), scale(mesh.scale)));
}

[[nodiscard]] Mat4 perspectiveVulkan(float verticalFovDegrees, float aspect, float nearPlane, float farPlane) {
    const float safeAspect = std::max(0.001F, aspect);
    const float safeNear = std::max(0.001F, nearPlane);
    const float safeFar = std::max(safeNear + 0.001F, farPlane);
    const float f = 1.0F / std::tan(degreesToRadians(verticalFovDegrees) * 0.5F);

    Mat4 result{};
    result.at(0, 0) = f / safeAspect;
    result.at(1, 1) = -f;
    result.at(2, 2) = safeFar / (safeFar - safeNear);
    result.at(2, 3) = -(safeNear * safeFar) / (safeFar - safeNear);
    result.at(3, 2) = 1.0F;
    return result;
}

[[nodiscard]] Mat4 lookAtLeftHanded(novacore::math::Vec3 eye, novacore::math::Vec3 forward) {
    constexpr novacore::math::Vec3 kWorldUp{0.0F, 1.0F, 0.0F};
    const auto zAxis = normalized(forward);
    auto xAxis = normalized(cross(kWorldUp, zAxis));
    if (xAxis.lengthSquared() <= 0.000001F) {
        xAxis = novacore::math::Vec3{1.0F, 0.0F, 0.0F};
    }
    const auto yAxis = cross(zAxis, xAxis);

    Mat4 result = identity();
    result.at(0, 0) = xAxis.x;
    result.at(0, 1) = xAxis.y;
    result.at(0, 2) = xAxis.z;
    result.at(0, 3) = -dot(xAxis, eye);

    result.at(1, 0) = yAxis.x;
    result.at(1, 1) = yAxis.y;
    result.at(1, 2) = yAxis.z;
    result.at(1, 3) = -dot(yAxis, eye);

    result.at(2, 0) = zAxis.x;
    result.at(2, 1) = zAxis.y;
    result.at(2, 2) = zAxis.z;
    result.at(2, 3) = -dot(zAxis, eye);
    return result;
}

[[nodiscard]] novacore::math::Vec3 cameraForward(const RenderCamera3D& camera) {
    const float yaw = degreesToRadians(camera.yawDegrees);
    const float pitch = degreesToRadians(camera.pitchDegrees);
    const float cosPitch = std::cos(pitch);
    return normalized(novacore::math::Vec3{
        std::sin(yaw) * cosPitch,
        std::sin(pitch),
        std::cos(yaw) * cosPitch,
    });
}

[[nodiscard]] Mat4 viewProjectionForCamera(const RenderCamera3D& camera, VkExtent2D extent) {
    const float aspect = static_cast<float>(std::max<std::uint32_t>(extent.width, 1U)) /
        static_cast<float>(std::max<std::uint32_t>(extent.height, 1U));
    const auto projection = perspectiveVulkan(camera.verticalFovDegrees, aspect, camera.nearPlane, camera.farPlane);
    const auto view = lookAtLeftHanded(camera.position, cameraForward(camera));
    return multiply(projection, view);
}

[[nodiscard]] WorldBoxPushConstants pushConstantsForBox(const Mat4& viewProjection, const RenderBox3D& box) {
    return WorldBoxPushConstants{
        viewProjection.value,
        {box.center.x, box.center.y, box.center.z, 0.0F},
        {
            std::max(0.001F, box.halfExtents.x),
            std::max(0.001F, box.halfExtents.y),
            std::max(0.001F, box.halfExtents.z),
            0.0F,
        },
        box.color,
    };
}

[[nodiscard]] WorldMeshPushConstants pushConstantsForMesh(const Mat4& viewProjection, const RenderMesh3D& mesh) {
    const auto worldViewProjection = multiply(viewProjection, modelMatrixForMesh(mesh));
    return WorldMeshPushConstants{
        worldViewProjection.value,
        mesh.color,
    };
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

struct GpuBuffer final {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
};

struct GpuMeshPrimitive final {
    GpuBuffer vertexBuffer{};
    GpuBuffer indexBuffer{};
    std::uint32_t indexCount = 0;
};

struct GpuMeshAsset final {
    std::string assetId;
    std::vector<GpuMeshPrimitive> primitives;
    std::size_t vertexCount = 0;
    std::size_t indexCount = 0;
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
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    VkPipelineLayout worldBoxPipelineLayout = VK_NULL_HANDLE;
    VkPipeline worldBoxPipeline = VK_NULL_HANDLE;
    VkPipelineLayout worldMeshPipelineLayout = VK_NULL_HANDLE;
    VkPipeline worldMeshPipeline = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kMaxFramesInFlight> commandBuffers{};
    std::array<VkSemaphore, kMaxFramesInFlight> imageAvailable{};
    std::array<VkSemaphore, kMaxFramesInFlight> renderFinished{};
    std::array<VkFence, kMaxFramesInFlight> inFlight{};
    std::unordered_map<std::string, GpuMeshAsset> gpuMeshes;
    std::uint32_t currentFrame = 0;
    std::uint32_t acquiredImageIndex = 0;
    bool frameActive = false;
    bool loggedWorldDrawSubmission = false;
    bool loggedWorldMeshSubmission = false;
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

    [[nodiscard]] std::optional<std::uint32_t> findMemoryType(
        std::uint32_t typeFilter,
        VkMemoryPropertyFlags properties) const {
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

        for (std::uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
            if ((typeFilter & (1U << index)) != 0 &&
                (memoryProperties.memoryTypes[index].propertyFlags & properties) == properties) {
                return index;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool createDepthResources() {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = swapchainExtent.width;
        imageInfo.extent.height = swapchainExtent.height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = depthFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (!vkOk(vkCreateImage(device, &imageInfo, nullptr, &depthImage), "vkCreateImage(depth)")) {
            return false;
        }

        VkMemoryRequirements memoryRequirements{};
        vkGetImageMemoryRequirements(device, depthImage, &memoryRequirements);
        const auto memoryType = findMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (!memoryType.has_value()) {
            core::logWarning("render", "No suitable memory type for Vulkan depth image");
            return false;
        }

        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = memoryRequirements.size;
        allocateInfo.memoryTypeIndex = *memoryType;

        if (!vkOk(vkAllocateMemory(device, &allocateInfo, nullptr, &depthMemory), "vkAllocateMemory(depth)")) {
            return false;
        }
        if (!vkOk(vkBindImageMemory(device, depthImage, depthMemory, 0), "vkBindImageMemory(depth)")) {
            return false;
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = depthImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = depthFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        return vkOk(vkCreateImageView(device, &viewInfo, nullptr, &depthImageView), "vkCreateImageView(depth)");
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

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = depthFormat;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthReference{};
        depthReference.attachment = 1;
        depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;
        subpass.pDepthStencilAttachment = &depthReference;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        const std::array<VkAttachmentDescription, 2> attachments{colorAttachment, depthAttachment};

        VkRenderPassCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        createInfo.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        createInfo.pAttachments = attachments.data();
        createInfo.subpassCount = 1;
        createInfo.pSubpasses = &subpass;
        createInfo.dependencyCount = 1;
        createInfo.pDependencies = &dependency;

        return vkOk(vkCreateRenderPass(device, &createInfo, nullptr, &renderPass), "vkCreateRenderPass");
    }

    [[nodiscard]] bool createFramebuffers() {
        framebuffers.resize(swapchainImageViews.size());
        for (std::size_t index = 0; index < swapchainImageViews.size(); ++index) {
            const std::array<VkImageView, 2> attachments{swapchainImageViews[index], depthImageView};

            VkFramebufferCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            createInfo.renderPass = renderPass;
            createInfo.attachmentCount = static_cast<std::uint32_t>(attachments.size());
            createInfo.pAttachments = attachments.data();
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

    [[nodiscard]] bool createWorldBoxPipeline() {
        const auto shaderDirectory = std::filesystem::path(NOVACORE_SHADER_BINARY_DIR);
        const auto vertexShaderBytes = readBinaryFile(shaderDirectory / "world_box.vert.spv");
        const auto fragmentShaderBytes = readBinaryFile(shaderDirectory / "world_box.frag.spv");
        if (vertexShaderBytes.empty() || fragmentShaderBytes.empty()) {
            core::logWarning("render", "Vulkan world box pipeline skipped because shader binaries are missing");
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

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = static_cast<std::uint32_t>(sizeof(WorldBoxPushConstants));

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
        if (!vkOk(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &worldBoxPipelineLayout), "vkCreatePipelineLayout(worldBox)")) {
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
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = worldBoxPipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        const bool success = vkOk(
            vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &worldBoxPipeline),
            "vkCreateGraphicsPipelines(worldBox)");

        vkDestroyShaderModule(device, fragmentShader, nullptr);
        vkDestroyShaderModule(device, vertexShader, nullptr);

        if (success) {
            core::logInfo("render", "Vulkan world box graphics pipeline created");
        }
        return success;
    }

    [[nodiscard]] bool createWorldMeshPipeline() {
        const auto shaderDirectory = std::filesystem::path(NOVACORE_SHADER_BINARY_DIR);
        const auto vertexShaderBytes = readBinaryFile(shaderDirectory / "world_mesh.vert.spv");
        const auto fragmentShaderBytes = readBinaryFile(shaderDirectory / "world_mesh.frag.spv");
        if (vertexShaderBytes.empty() || fragmentShaderBytes.empty()) {
            core::logWarning("render", "Vulkan world mesh pipeline skipped because shader binaries are missing");
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

        VkVertexInputBindingDescription vertexBinding{};
        vertexBinding.binding = 0;
        vertexBinding.stride = sizeof(VulkanMeshVertex);
        vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 2> vertexAttributes{};
        vertexAttributes[0].binding = 0;
        vertexAttributes[0].location = 0;
        vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        vertexAttributes[0].offset = offsetof(VulkanMeshVertex, position);
        vertexAttributes[1].binding = 0;
        vertexAttributes[1].location = 1;
        vertexAttributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        vertexAttributes[1].offset = offsetof(VulkanMeshVertex, normal);

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &vertexBinding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(vertexAttributes.size());
        vertexInput.pVertexAttributeDescriptions = vertexAttributes.data();

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

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = static_cast<std::uint32_t>(sizeof(WorldMeshPushConstants));

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
        if (!vkOk(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &worldMeshPipelineLayout), "vkCreatePipelineLayout(worldMesh)")) {
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
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = worldMeshPipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        const bool success = vkOk(
            vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &worldMeshPipeline),
            "vkCreateGraphicsPipelines(worldMesh)");

        vkDestroyShaderModule(device, fragmentShader, nullptr);
        vkDestroyShaderModule(device, vertexShader, nullptr);

        if (success) {
            core::logInfo("render", "Vulkan world mesh graphics pipeline created");
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

    void destroyBuffer(GpuBuffer& buffer) {
        if (buffer.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, buffer.buffer, nullptr);
            buffer.buffer = VK_NULL_HANDLE;
        }
        if (buffer.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, buffer.memory, nullptr);
            buffer.memory = VK_NULL_HANDLE;
        }
        buffer.size = 0;
    }

    [[nodiscard]] bool createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        GpuBuffer& outBuffer) {
        if (size == 0) {
            core::logWarning("render", "Refusing to create zero-byte Vulkan buffer");
            return false;
        }

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (!vkOk(vkCreateBuffer(device, &bufferInfo, nullptr, &outBuffer.buffer), "vkCreateBuffer")) {
            return false;
        }

        VkMemoryRequirements memoryRequirements{};
        vkGetBufferMemoryRequirements(device, outBuffer.buffer, &memoryRequirements);
        const auto memoryType = findMemoryType(memoryRequirements.memoryTypeBits, properties);
        if (!memoryType.has_value()) {
            core::logWarning("render", "No suitable memory type for Vulkan buffer");
            destroyBuffer(outBuffer);
            return false;
        }

        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = memoryRequirements.size;
        allocateInfo.memoryTypeIndex = *memoryType;

        if (!vkOk(vkAllocateMemory(device, &allocateInfo, nullptr, &outBuffer.memory), "vkAllocateMemory(buffer)")) {
            destroyBuffer(outBuffer);
            return false;
        }
        if (!vkOk(vkBindBufferMemory(device, outBuffer.buffer, outBuffer.memory, 0), "vkBindBufferMemory")) {
            destroyBuffer(outBuffer);
            return false;
        }

        outBuffer.size = size;
        return true;
    }

    [[nodiscard]] bool writeHostVisibleBuffer(const void* data, VkDeviceSize size, const GpuBuffer& buffer) {
        void* mapped = nullptr;
        if (!vkOk(vkMapMemory(device, buffer.memory, 0, size, 0, &mapped), "vkMapMemory")) {
            return false;
        }

        std::memcpy(mapped, data, static_cast<std::size_t>(size));
        vkUnmapMemory(device, buffer.memory);
        return true;
    }

    [[nodiscard]] std::optional<VkCommandBuffer> beginSingleTimeCommands() {
        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandPool = commandPool;
        allocateInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (!vkOk(vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer), "vkAllocateCommandBuffers(singleTime)")) {
            return std::nullopt;
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (!vkOk(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer(singleTime)")) {
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
            return std::nullopt;
        }
        return commandBuffer;
    }

    [[nodiscard]] bool endSingleTimeCommands(VkCommandBuffer commandBuffer) {
        if (!vkOk(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer(singleTime)")) {
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
            return false;
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        const bool submitted = vkOk(vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit(singleTime)");
        if (submitted) {
            (void)vkQueueWaitIdle(graphicsQueue);
        }
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        return submitted;
    }

    [[nodiscard]] bool copyBuffer(VkBuffer source, VkBuffer destination, VkDeviceSize size) {
        const auto commandBuffer = beginSingleTimeCommands();
        if (!commandBuffer.has_value()) {
            return false;
        }

        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(*commandBuffer, source, destination, 1, &copyRegion);
        return endSingleTimeCommands(*commandBuffer);
    }

    [[nodiscard]] bool uploadDeviceLocalBuffer(
        const void* data,
        VkDeviceSize size,
        VkBufferUsageFlags finalUsage,
        GpuBuffer& outBuffer) {
        GpuBuffer staging{};
        if (!createBuffer(
                size,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                staging)) {
            return false;
        }

        if (!writeHostVisibleBuffer(data, size, staging)) {
            destroyBuffer(staging);
            return false;
        }

        if (!createBuffer(
                size,
                finalUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                outBuffer)) {
            destroyBuffer(staging);
            return false;
        }

        const bool copied = copyBuffer(staging.buffer, outBuffer.buffer, size);
        destroyBuffer(staging);
        if (!copied) {
            destroyBuffer(outBuffer);
            return false;
        }
        return true;
    }

    [[nodiscard]] std::vector<VulkanMeshVertex> buildVertices(
        const assets::GltfPrimitiveData& primitive) const {
        std::vector<VulkanMeshVertex> vertices;
        vertices.reserve(primitive.positions.size());
        for (std::size_t index = 0; index < primitive.positions.size(); ++index) {
            const auto position = primitive.positions[index];
            const auto normal = index < primitive.normals.size()
                ? normalized(primitive.normals[index])
                : novacore::math::Vec3{0.0F, 1.0F, 0.0F};
            vertices.push_back(VulkanMeshVertex{
                {position.x, position.y, position.z},
                {normal.x, normal.y, normal.z},
            });
        }
        return vertices;
    }

    [[nodiscard]] bool uploadMeshPrimitive(
        const assets::GltfPrimitiveData& source,
        GpuMeshPrimitive& outPrimitive) {
        if (source.positions.empty() || source.indices.empty()) {
            core::logWarning("render", "Skipping empty glTF primitive during Vulkan upload");
            return false;
        }

        const auto vertices = buildVertices(source);
        const VkDeviceSize vertexBytes = sizeof(VulkanMeshVertex) * vertices.size();
        const VkDeviceSize indexBytes = sizeof(std::uint32_t) * source.indices.size();

        if (!uploadDeviceLocalBuffer(
                vertices.data(),
                vertexBytes,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                outPrimitive.vertexBuffer)) {
            return false;
        }
        if (!uploadDeviceLocalBuffer(
                source.indices.data(),
                indexBytes,
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                outPrimitive.indexBuffer)) {
            destroyBuffer(outPrimitive.vertexBuffer);
            return false;
        }

        outPrimitive.indexCount = static_cast<std::uint32_t>(std::min<std::size_t>(
            source.indices.size(),
            std::numeric_limits<std::uint32_t>::max()));
        return true;
    }

    void destroyGpuMeshAsset(GpuMeshAsset& asset) {
        for (auto& primitive : asset.primitives) {
            destroyBuffer(primitive.vertexBuffer);
            destroyBuffer(primitive.indexBuffer);
            primitive.indexCount = 0;
        }
        asset.primitives.clear();
        asset.vertexCount = 0;
        asset.indexCount = 0;
    }

    void destroyGpuMeshes() {
        for (auto& [_, asset] : gpuMeshes) {
            destroyGpuMeshAsset(asset);
        }
        gpuMeshes.clear();
    }

    [[nodiscard]] bool ensureMeshUploaded(const RenderMesh3D& renderMesh) {
        if (renderMesh.assetId.empty()) {
            return false;
        }
        if (gpuMeshes.find(renderMesh.assetId) != gpuMeshes.end()) {
            return true;
        }
        if (renderMesh.meshData == nullptr) {
            return false;
        }

        GpuMeshAsset asset{};
        asset.assetId = renderMesh.assetId;
        asset.primitives.reserve(renderMesh.meshData->primitives.size());

        for (const auto& primitiveData : renderMesh.meshData->primitives) {
            GpuMeshPrimitive gpuPrimitive{};
            if (!uploadMeshPrimitive(primitiveData, gpuPrimitive)) {
                destroyGpuMeshAsset(asset);
                return false;
            }
            asset.vertexCount += primitiveData.positions.size();
            asset.indexCount += primitiveData.indices.size();
            asset.primitives.push_back(gpuPrimitive);
        }

        if (asset.primitives.empty()) {
            return false;
        }

        core::logInfo(
            "render",
            "Vulkan mesh uploaded: " + renderMesh.assetId +
                " primitives=" + std::to_string(asset.primitives.size()) +
                " vertices=" + std::to_string(asset.vertexCount) +
                " indices=" + std::to_string(asset.indexCount));
        gpuMeshes.emplace(renderMesh.assetId, std::move(asset));
        return true;
    }

    void ensureFrameMeshesUploaded(const RenderFrameInfo& frame) {
        if (worldMeshPipeline == VK_NULL_HANDLE || !frame.camera3D.enabled || frame.worldMeshes.empty()) {
            return;
        }

        for (const auto& mesh : frame.worldMeshes) {
            (void)ensureMeshUploaded(mesh);
        }
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

    void recordCommandBuffer(VkCommandBuffer commandBuffer, std::uint32_t imageIndex, const RenderFrameInfo& frame) {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{frame.clearColor[0], frame.clearColor[1], frame.clearColor[2], frame.clearColor[3]}};
        clearValues[1].depthStencil = {1.0F, 0};

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffers[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapchainExtent;
        renderPassInfo.clearValueCount = static_cast<std::uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        if (worldBoxPipeline != VK_NULL_HANDLE && frame.camera3D.enabled && !frame.worldBoxes.empty()) {
            if (!loggedWorldDrawSubmission) {
                core::logInfo(
                    "render",
                    "Vulkan world box draw submission active: boxes=" + std::to_string(frame.worldBoxes.size()));
                loggedWorldDrawSubmission = true;
            }
            const auto viewProjection = viewProjectionForCamera(frame.camera3D, swapchainExtent);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, worldBoxPipeline);
            for (const auto& box : frame.worldBoxes) {
                const auto constants = pushConstantsForBox(viewProjection, box);
                vkCmdPushConstants(
                    commandBuffer,
                    worldBoxPipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0,
                    static_cast<std::uint32_t>(sizeof(WorldBoxPushConstants)),
                    &constants);
                vkCmdDraw(commandBuffer, 36, 1, 0, 0);
            }
        }

        if (worldMeshPipeline != VK_NULL_HANDLE && frame.camera3D.enabled && !frame.worldMeshes.empty()) {
            if (!loggedWorldMeshSubmission) {
                core::logInfo(
                    "render",
                    "Vulkan world mesh draw submission active: meshes=" + std::to_string(frame.worldMeshes.size()));
                loggedWorldMeshSubmission = true;
            }

            const auto viewProjection = viewProjectionForCamera(frame.camera3D, swapchainExtent);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, worldMeshPipeline);
            for (const auto& mesh : frame.worldMeshes) {
                const auto uploaded = gpuMeshes.find(mesh.assetId);
                if (uploaded == gpuMeshes.end()) {
                    continue;
                }

                const auto constants = pushConstantsForMesh(viewProjection, mesh);
                vkCmdPushConstants(
                    commandBuffer,
                    worldMeshPipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0,
                    static_cast<std::uint32_t>(sizeof(WorldMeshPushConstants)),
                    &constants);

                for (const auto& primitive : uploaded->second.primitives) {
                    if (primitive.vertexBuffer.buffer == VK_NULL_HANDLE ||
                        primitive.indexBuffer.buffer == VK_NULL_HANDLE ||
                        primitive.indexCount == 0) {
                        continue;
                    }

                    const VkBuffer vertexBuffers[] = {primitive.vertexBuffer.buffer};
                    const VkDeviceSize offsets[] = {0};
                    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                    vkCmdBindIndexBuffer(commandBuffer, primitive.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(commandBuffer, primitive.indexCount, 1, 0, 0, 0);
                }
            }
        }

        vkCmdEndRenderPass(commandBuffer);

        imageLayouts[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkEndCommandBuffer(commandBuffer);
    }

    void destroySwapchainResources() {
        if (device == VK_NULL_HANDLE) {
            return;
        }

        if (worldMeshPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, worldMeshPipeline, nullptr);
            worldMeshPipeline = VK_NULL_HANDLE;
        }

        if (worldMeshPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, worldMeshPipelineLayout, nullptr);
            worldMeshPipelineLayout = VK_NULL_HANDLE;
        }

        if (worldBoxPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, worldBoxPipeline, nullptr);
            worldBoxPipeline = VK_NULL_HANDLE;
        }

        if (worldBoxPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, worldBoxPipelineLayout, nullptr);
            worldBoxPipelineLayout = VK_NULL_HANDLE;
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

        if (depthImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, depthImageView, nullptr);
            depthImageView = VK_NULL_HANDLE;
        }

        if (depthImage != VK_NULL_HANDLE) {
            vkDestroyImage(device, depthImage, nullptr);
            depthImage = VK_NULL_HANDLE;
        }

        if (depthMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, depthMemory, nullptr);
            depthMemory = VK_NULL_HANDLE;
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
        !impl_->createDepthResources() ||
        !impl_->createFramebuffers() ||
        !impl_->createCommandPoolAndBuffers() ||
        !impl_->createSyncObjects()) {
        shutdown();
        return false;
    }

    (void)impl_->createWorldBoxPipeline();
    (void)impl_->createWorldMeshPipeline();

    impl_->isReady = true;
    core::logInfo("render", "Vulkan world and mesh backend created");
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

    impl_->ensureFrameMeshesUploaded(frame);

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
    impl_->recordCommandBuffer(
        impl_->commandBuffers[impl_->currentFrame],
        impl_->acquiredImageIndex,
        frame);
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
    impl_->destroyGpuMeshes();

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
    impl_->loggedWorldDrawSubmission = false;
    impl_->loggedWorldMeshSubmission = false;
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
