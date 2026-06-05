#pragma once

#include "novacore/math/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace novacore::assets {

using AssetId = std::string;

enum class AssetKind {
    Mesh,
    Material,
    Texture,
    Scene,
    Animation,
    Audio,
    Shader,
    Data,
};

enum class AssetLoadState {
    Unloaded,
    Queued,
    Loading,
    Resident,
    Failed,
};

struct AssetRecord final {
    AssetId id;
    AssetKind kind = AssetKind::Data;
    std::filesystem::path sourcePath;
    std::filesystem::path cookedPath;
    std::vector<AssetId> dependencies;
    std::vector<std::string> tags;
    std::uint32_t priority = 0;
    std::uint64_t estimatedBytes = 0;
    bool streamable = false;
};

struct AssetStreamingZone final {
    std::string name;
    math::Vec3 center{};
    float radiusMeters = 0.0F;
    std::vector<AssetId> preloadAssets;
};

[[nodiscard]] std::string_view assetKindName(AssetKind kind);
[[nodiscard]] std::optional<AssetKind> assetKindFromString(std::string_view text);
[[nodiscard]] std::string_view assetLoadStateName(AssetLoadState state);

} // namespace novacore::assets
