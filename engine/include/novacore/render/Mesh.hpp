#pragma once

#include "novacore/assets/AssetTypes.hpp"
#include "novacore/assets/GltfDocument.hpp"
#include "novacore/assets/GltfMetadata.hpp"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace novacore::render {

struct MeshHandle final {
    static constexpr std::uint32_t InvalidIndex = std::numeric_limits<std::uint32_t>::max();

    std::uint32_t index = InvalidIndex;
    std::uint32_t generation = 0;

    [[nodiscard]] bool isValid() const {
        return index != InvalidIndex && generation != 0;
    }
};

struct MeshAssetSource final {
    MeshHandle handle{};
    assets::AssetId assetId;
    assets::AssetKind assetKind = assets::AssetKind::Mesh;
    std::filesystem::path sourcePath;
    std::filesystem::path cookedPath;
    std::uint64_t estimatedBytes = 0;
    std::vector<std::string> tags;
    std::optional<assets::GltfAssetMetadata> metadata;
    std::optional<assets::GltfSceneInfo> sceneInfo;
    std::optional<assets::GltfMeshData> meshData;
};

[[nodiscard]] bool isRenderableAssetRecord(const assets::AssetRecord& record);
[[nodiscard]] std::vector<std::string> validateRenderableAssetRecord(const assets::AssetRecord& record);

class MeshCatalog final {
public:
    void clear();

    [[nodiscard]] MeshHandle registerAsset(const assets::AssetRecord& record);
    [[nodiscard]] MeshHandle registerGltfAsset(
        const assets::AssetRecord& record,
        assets::GltfAssetMetadata metadata);
    [[nodiscard]] MeshHandle registerImportedGltfAsset(
        const assets::AssetRecord& record,
        assets::GltfAssetMetadata metadata,
        assets::GltfSceneInfo sceneInfo);
    [[nodiscard]] MeshHandle registerImportedGltfAsset(
        const assets::AssetRecord& record,
        assets::GltfAssetMetadata metadata,
        assets::GltfMeshData meshData);

    [[nodiscard]] const MeshAssetSource* find(MeshHandle handle) const;
    [[nodiscard]] const MeshAssetSource* findByAssetId(std::string_view assetId) const;
    [[nodiscard]] bool contains(std::string_view assetId) const;
    [[nodiscard]] std::size_t meshCount() const;

private:
    [[nodiscard]] MeshHandle registerAssetInternal(
        const assets::AssetRecord& record,
        std::optional<assets::GltfAssetMetadata> metadata,
        std::optional<assets::GltfSceneInfo> sceneInfo,
        std::optional<assets::GltfMeshData> meshData);

    std::vector<MeshAssetSource> meshes_;
    std::vector<std::uint32_t> generations_;
    std::unordered_map<std::string, MeshHandle> handlesByAssetId_;
};

} // namespace novacore::render
