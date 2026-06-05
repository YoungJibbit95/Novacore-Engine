#pragma once

#include "novacore/assets/AssetTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace novacore::assets {

struct AssetStreamRequest final {
    AssetId id;
    std::uint32_t priority = 0;
    std::uint64_t requestedFrame = 0;
};

class AssetStreamer final {
public:
    void clear();
    void request(AssetId id, std::uint32_t priority, std::uint64_t frameIndex);
    void requestZone(const AssetStreamingZone& zone, std::uint64_t frameIndex);

    [[nodiscard]] bool hasPending() const;
    [[nodiscard]] std::size_t pendingCount() const;
    [[nodiscard]] bool isPending(std::string_view id) const;
    [[nodiscard]] std::vector<AssetStreamRequest> popBudgeted(std::size_t maxRequests);

private:
    std::vector<AssetStreamRequest> pending_;
    std::unordered_map<std::string, std::size_t> indexById_;
};

} // namespace novacore::assets
