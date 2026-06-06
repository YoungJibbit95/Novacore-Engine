#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace novacore::render {

struct VulkanDeviceInfo final {
    std::string name;
    std::string type;
    std::uint32_t apiVersion = 0;
    std::uint32_t driverVersion = 0;
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
};

struct VulkanRuntimeInfo final {
    bool loaderAvailable = false;
    std::uint32_t instanceApiVersion = 0;
    std::vector<VulkanDeviceInfo> devices;
    std::vector<std::string> errors;

    [[nodiscard]] bool usable() const {
        return loaderAvailable && !devices.empty();
    }
};

[[nodiscard]] VulkanRuntimeInfo probeVulkanRuntime();
[[nodiscard]] std::string vulkanVersionString(std::uint32_t version);
[[nodiscard]] std::string vulkanRuntimeSummary(const VulkanRuntimeInfo& info);

} // namespace novacore::render
