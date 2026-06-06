#include "novacore/render/VulkanRuntime.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace novacore::render {

namespace {

using VkFlags = std::uint32_t;
using VkBool32 = std::uint32_t;
using VkResult = std::int32_t;
using VkInstance = struct VkInstance_T*;
using VkPhysicalDevice = struct VkPhysicalDevice_T*;
using VkAllocationCallbacks = struct VkAllocationCallbacks_T;

constexpr VkResult kVkSuccess = 0;
constexpr std::uint32_t kVkStructureTypeApplicationInfo = 0;
constexpr std::uint32_t kVkStructureTypeInstanceCreateInfo = 1;
constexpr std::size_t kVkMaxPhysicalDeviceNameSize = 256;
constexpr std::size_t kVkUuidSize = 16;

struct VkApplicationInfo final {
    std::uint32_t sType = kVkStructureTypeApplicationInfo;
    const void* pNext = nullptr;
    const char* pApplicationName = nullptr;
    std::uint32_t applicationVersion = 0;
    const char* pEngineName = nullptr;
    std::uint32_t engineVersion = 0;
    std::uint32_t apiVersion = 0;
};

struct VkInstanceCreateInfo final {
    std::uint32_t sType = kVkStructureTypeInstanceCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    const VkApplicationInfo* pApplicationInfo = nullptr;
    std::uint32_t enabledLayerCount = 0;
    const char* const* ppEnabledLayerNames = nullptr;
    std::uint32_t enabledExtensionCount = 0;
    const char* const* ppEnabledExtensionNames = nullptr;
};

struct VkPhysicalDeviceProperties final {
    std::uint32_t apiVersion = 0;
    std::uint32_t driverVersion = 0;
    std::uint32_t vendorID = 0;
    std::uint32_t deviceID = 0;
    std::uint32_t deviceType = 0;
    char deviceName[kVkMaxPhysicalDeviceNameSize]{};
    std::uint8_t pipelineCacheUUID[kVkUuidSize]{};
    alignas(8) std::array<std::uint8_t, 4096> limits{};
    std::array<std::uint8_t, 64> sparseProperties{};
};

using PFN_vkEnumerateInstanceVersion = VkResult (*)(std::uint32_t*);
using PFN_vkCreateInstance = VkResult (*)(const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance*);
using PFN_vkDestroyInstance = void (*)(VkInstance, const VkAllocationCallbacks*);
using PFN_vkEnumeratePhysicalDevices = VkResult (*)(VkInstance, std::uint32_t*, VkPhysicalDevice*);
using PFN_vkGetPhysicalDeviceProperties = void (*)(VkPhysicalDevice, VkPhysicalDeviceProperties*);

class DynamicLibrary final {
public:
    explicit DynamicLibrary(const char* name) {
#if defined(_WIN32)
        handle_ = LoadLibraryA(name);
#else
        handle_ = dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
    }

    ~DynamicLibrary() {
#if defined(_WIN32)
        if (handle_ != nullptr) {
            FreeLibrary(static_cast<HMODULE>(handle_));
        }
#else
        if (handle_ != nullptr) {
            dlclose(handle_);
        }
#endif
    }

    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    [[nodiscard]] bool valid() const {
        return handle_ != nullptr;
    }

    template <typename Function>
    [[nodiscard]] Function load(const char* name) const {
        Function function = nullptr;
#if defined(_WIN32)
        auto* raw = GetProcAddress(static_cast<HMODULE>(handle_), name);
#else
        auto* raw = dlsym(handle_, name);
#endif
        static_assert(sizeof(function) == sizeof(raw));
        std::memcpy(&function, &raw, sizeof(function));
        return function;
    }

private:
    void* handle_ = nullptr;
};

[[nodiscard]] std::string deviceTypeName(std::uint32_t type) {
    switch (type) {
    case 1:
        return "integrated";
    case 2:
        return "discrete";
    case 3:
        return "virtual";
    case 4:
        return "cpu";
    default:
        return "other";
    }
}

[[nodiscard]] std::uint32_t makeVersion(std::uint32_t major, std::uint32_t minor, std::uint32_t patch) {
    return (major << 22U) | (minor << 12U) | patch;
}

} // namespace

std::string vulkanVersionString(std::uint32_t version) {
    std::ostringstream stream;
    stream << ((version >> 22U) & 0x7FU)
           << '.'
           << ((version >> 12U) & 0x3FFU)
           << '.'
           << (version & 0xFFFU);
    return stream.str();
}

std::string vulkanRuntimeSummary(const VulkanRuntimeInfo& info) {
    if (!info.loaderAvailable) {
        return "loader unavailable";
    }
    if (info.devices.empty()) {
        return "loader available, no devices";
    }

    const auto& device = info.devices.front();
    return vulkanVersionString(info.instanceApiVersion) + " / " +
        device.name + " (" + device.type + ")";
}

VulkanRuntimeInfo probeVulkanRuntime() {
#if defined(_WIN32)
    DynamicLibrary library("vulkan-1.dll");
#elif defined(__APPLE__)
    DynamicLibrary library("libvulkan.1.dylib");
#else
    DynamicLibrary library("libvulkan.so.1");
#endif

    VulkanRuntimeInfo info{};
    if (!library.valid()) {
        info.errors.push_back("Vulkan loader library was not found");
        return info;
    }
    info.loaderAvailable = true;

    const auto enumerateInstanceVersion =
        library.load<PFN_vkEnumerateInstanceVersion>("vkEnumerateInstanceVersion");
    const auto createInstance = library.load<PFN_vkCreateInstance>("vkCreateInstance");
    const auto destroyInstance = library.load<PFN_vkDestroyInstance>("vkDestroyInstance");
    const auto enumeratePhysicalDevices =
        library.load<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");
    const auto getPhysicalDeviceProperties =
        library.load<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties");

    if (createInstance == nullptr ||
        destroyInstance == nullptr ||
        enumeratePhysicalDevices == nullptr ||
        getPhysicalDeviceProperties == nullptr) {
        info.errors.push_back("Vulkan loader is missing required core entry points");
        return info;
    }

    info.instanceApiVersion = makeVersion(1, 0, 0);
    if (enumerateInstanceVersion != nullptr) {
        std::uint32_t apiVersion = 0;
        if (enumerateInstanceVersion(&apiVersion) == kVkSuccess && apiVersion != 0) {
            info.instanceApiVersion = apiVersion;
        }
    }

    VkApplicationInfo applicationInfo{};
    applicationInfo.pApplicationName = "NovaCore Vulkan Probe";
    applicationInfo.applicationVersion = makeVersion(0, 1, 0);
    applicationInfo.pEngineName = "NovaCore";
    applicationInfo.engineVersion = makeVersion(0, 1, 0);
    applicationInfo.apiVersion = info.instanceApiVersion;

    VkInstanceCreateInfo createInfo{};
    createInfo.pApplicationInfo = &applicationInfo;

    VkInstance instance = nullptr;
    const VkResult createResult = createInstance(&createInfo, nullptr, &instance);
    if (createResult != kVkSuccess || instance == nullptr) {
        info.errors.push_back("vkCreateInstance failed with result " + std::to_string(createResult));
        return info;
    }

    std::uint32_t deviceCount = 0;
    VkResult enumerateResult = enumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (enumerateResult != kVkSuccess || deviceCount == 0) {
        info.errors.push_back("vkEnumeratePhysicalDevices found no devices");
        destroyInstance(instance, nullptr);
        return info;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    enumerateResult = enumeratePhysicalDevices(instance, &deviceCount, devices.data());
    if (enumerateResult != kVkSuccess) {
        info.errors.push_back("vkEnumeratePhysicalDevices failed with result " + std::to_string(enumerateResult));
        destroyInstance(instance, nullptr);
        return info;
    }

    info.devices.reserve(deviceCount);
    for (std::uint32_t index = 0; index < deviceCount; ++index) {
        VkPhysicalDeviceProperties properties{};
        getPhysicalDeviceProperties(devices[index], &properties);

        info.devices.push_back(VulkanDeviceInfo{
            properties.deviceName,
            deviceTypeName(properties.deviceType),
            properties.apiVersion,
            properties.driverVersion,
            properties.vendorID,
            properties.deviceID,
        });
    }

    destroyInstance(instance, nullptr);
    return info;
}

} // namespace novacore::render
