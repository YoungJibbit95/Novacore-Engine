#include "novacore/assets/AssetStreamer.hpp"

#include <algorithm>
#include <utility>

namespace novacore::assets {

void AssetStreamer::clear() {
    pending_.clear();
    indexById_.clear();
}

void AssetStreamer::request(AssetId id, std::uint32_t priority, std::uint64_t frameIndex) {
    if (id.empty()) {
        return;
    }

    const auto it = indexById_.find(id);
    if (it != indexById_.end()) {
        auto& existing = pending_[it->second];
        existing.priority = std::max(existing.priority, priority);
        existing.requestedFrame = std::min(existing.requestedFrame, frameIndex);
        return;
    }

    indexById_.emplace(id, pending_.size());
    pending_.push_back(AssetStreamRequest{std::move(id), priority, frameIndex});
}

void AssetStreamer::requestZone(const AssetStreamingZone& zone, std::uint64_t frameIndex) {
    for (const auto& id : zone.preloadAssets) {
        request(id, 100, frameIndex);
    }
}

bool AssetStreamer::hasPending() const {
    return !pending_.empty();
}

std::size_t AssetStreamer::pendingCount() const {
    return pending_.size();
}

bool AssetStreamer::isPending(std::string_view id) const {
    return indexById_.contains(std::string(id));
}

std::vector<AssetStreamRequest> AssetStreamer::popBudgeted(std::size_t maxRequests) {
    std::sort(
        pending_.begin(),
        pending_.end(),
        [](const AssetStreamRequest& left, const AssetStreamRequest& right) {
            if (left.priority != right.priority) {
                return left.priority > right.priority;
            }
            return left.requestedFrame < right.requestedFrame;
        });

    const auto count = std::min(maxRequests, pending_.size());
    std::vector<AssetStreamRequest> selected;
    selected.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        selected.push_back(std::move(pending_[index]));
    }

    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(count));
    indexById_.clear();
    for (std::size_t index = 0; index < pending_.size(); ++index) {
        indexById_.emplace(pending_[index].id, index);
    }

    return selected;
}

} // namespace novacore::assets
