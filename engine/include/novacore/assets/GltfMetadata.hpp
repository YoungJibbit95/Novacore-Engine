#pragma once

#include "novacore/assets/AssetTypes.hpp"
#include "novacore/core/ConfigDocument.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace novacore::assets {

struct GltfAssetMetadata final {
    AssetId id;
    std::filesystem::path sourcePath;
    std::filesystem::path exportPath;
    std::string category;
    bool scaleMeters = false;
    std::string runtimeUpAxis;
    std::string gameplayForwardAxis;
    std::vector<std::string> sockets;
    std::string collision;
    std::vector<std::string> lods;
    std::string license;
    std::string generatedBy;
};

struct GltfAssetMetadataLoadResult final {
    std::vector<std::string> errors;

    [[nodiscard]] bool ok() const {
        return errors.empty();
    }
};

[[nodiscard]] std::filesystem::path metadataPathForCookedAsset(const AssetRecord& record);

[[nodiscard]] GltfAssetMetadataLoadResult parseGltfAssetMetadata(
    const core::ConfigDocument& document,
    GltfAssetMetadata& outMetadata);

[[nodiscard]] GltfAssetMetadataLoadResult loadGltfAssetMetadataFromJson(
    const std::filesystem::path& path,
    GltfAssetMetadata& outMetadata);

[[nodiscard]] std::vector<std::string> validateGltfAssetMetadata(
    const AssetRecord& record,
    const GltfAssetMetadata& metadata);

} // namespace novacore::assets
