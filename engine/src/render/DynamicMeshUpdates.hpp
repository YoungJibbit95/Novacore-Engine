#pragma once

#include "novacore/render/Renderer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace novacore::render::detail {

inline constexpr std::uint32_t kDynamicMeshFrameCount = 2U;

struct PendingDynamicPrimitiveUpdate final {
    std::vector<math::Vec3> positions;
    std::vector<math::Vec3> normals;
};

struct PendingDynamicMeshUpdate final {
    MeshResourceHandle handle{};
    std::vector<PendingDynamicPrimitiveUpdate> primitives;
    std::array<bool, kDynamicMeshFrameCount> dirtyFrames{};
};

inline void queuePendingDynamicMeshUpdate(
    std::vector<PendingDynamicMeshUpdate>& pending, PendingDynamicMeshUpdate replacement) {
    replacement.dirtyFrames.fill(true);
    const auto existing = std::ranges::find_if(pending, [&](const auto& update) {
        return update.handle == replacement.handle;
    });
    if (existing == pending.end()) {
        pending.push_back(std::move(replacement));
    } else {
        *existing = std::move(replacement);
    }
}

inline void discardPendingDynamicMeshUpdate(
    std::vector<PendingDynamicMeshUpdate>& pending, MeshResourceHandle handle) {
    std::erase_if(pending, [handle](const auto& update) { return update.handle == handle; });
}

enum class DynamicMeshUploadResult { Uploaded, Retry, Discard };

// Called only for the current slot after its fence completes. Keep the latest
// payload until every slot consumes it; a newer pose dirties every slot again.
template <typename Upload>
void processPendingDynamicMeshUpdates(
    std::vector<PendingDynamicMeshUpdate>& pending, std::size_t safeFrameSlot, Upload&& upload) {
    if (safeFrameSlot >= kDynamicMeshFrameCount) {
        return;
    }
    for (auto update = pending.begin(); update != pending.end();) {
        if (!update->dirtyFrames[safeFrameSlot]) {
            ++update;
            continue;
        }
        const auto result = upload(*update, safeFrameSlot);
        if (result == DynamicMeshUploadResult::Uploaded) {
            update->dirtyFrames[safeFrameSlot] = false;
        }
        if (result == DynamicMeshUploadResult::Discard ||
            std::ranges::none_of(update->dirtyFrames, [](bool dirty) { return dirty; })) {
            update = pending.erase(update);
        } else {
            ++update;
        }
    }
}

} // namespace novacore::render::detail
