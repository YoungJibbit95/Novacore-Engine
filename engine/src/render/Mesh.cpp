#include "novacore/render/Mesh.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

namespace novacore::render {

namespace {

[[nodiscard]] std::string lowercaseExtension(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}

[[nodiscard]] bool isRenderableKind(assets::AssetKind kind) {
    return kind == assets::AssetKind::Mesh || kind == assets::AssetKind::Scene;
}

} // namespace

bool isRenderableAssetRecord(const assets::AssetRecord& record) {
    return validateRenderableAssetRecord(record).empty();
}

std::vector<std::string> validateRenderableAssetRecord(const assets::AssetRecord& record) {
    std::vector<std::string> errors;
    if (record.id.empty()) {
        errors.push_back("Renderable asset record is missing id");
    }
    if (!isRenderableKind(record.kind)) {
        errors.push_back("Asset '" + record.id + "' is not a mesh or scene asset");
    }
    if (record.cookedPath.empty()) {
        errors.push_back("Asset '" + record.id + "' is missing cooked glTF path");
    } else {
        const auto extension = lowercaseExtension(record.cookedPath);
        if (extension != ".glb" && extension != ".gltf") {
            errors.push_back("Asset '" + record.id + "' cooked path must end in .glb or .gltf");
        }
    }
    return errors;
}

void MeshCatalog::clear() {
    meshes_.clear();
    generations_.clear();
    handlesByAssetId_.clear();
}

MeshHandle MeshCatalog::registerAsset(const assets::AssetRecord& record) {
    return registerAssetInternal(record, std::nullopt);
}

MeshHandle MeshCatalog::registerGltfAsset(
    const assets::AssetRecord& record,
    assets::GltfAssetMetadata metadata) {
    return registerAssetInternal(record, std::move(metadata));
}

const MeshAssetSource* MeshCatalog::find(MeshHandle handle) const {
    if (!handle.isValid() || handle.index >= meshes_.size()) {
        return nullptr;
    }
    if (handle.index >= generations_.size() || generations_[handle.index] != handle.generation) {
        return nullptr;
    }
    return &meshes_[handle.index];
}

const MeshAssetSource* MeshCatalog::findByAssetId(std::string_view assetId) const {
    const auto it = handlesByAssetId_.find(std::string(assetId));
    if (it == handlesByAssetId_.end()) {
        return nullptr;
    }
    return find(it->second);
}

bool MeshCatalog::contains(std::string_view assetId) const {
    return findByAssetId(assetId) != nullptr;
}

std::size_t MeshCatalog::meshCount() const {
    return meshes_.size();
}

MeshHandle MeshCatalog::registerAssetInternal(
    const assets::AssetRecord& record,
    std::optional<assets::GltfAssetMetadata> metadata) {
    if (!isRenderableAssetRecord(record)) {
        return {};
    }

    const auto existing = handlesByAssetId_.find(record.id);
    if (existing != handlesByAssetId_.end()) {
        return existing->second;
    }

    const auto index = static_cast<std::uint32_t>(meshes_.size());
    constexpr std::uint32_t kInitialGeneration = 1;
    MeshHandle handle{index, kInitialGeneration};

    meshes_.push_back(MeshAssetSource{
        handle,
        record.id,
        record.kind,
        record.sourcePath,
        record.cookedPath,
        record.estimatedBytes,
        record.tags,
        std::move(metadata),
    });
    generations_.push_back(kInitialGeneration);
    handlesByAssetId_.emplace(record.id, handle);
    return handle;
}

} // namespace novacore::render
