#include "novacore/assets/GltfMetadata.hpp"

#include "novacore/io/FileSystem.hpp"

#include <string>
#include <utility>

namespace novacore::assets {

namespace {

[[nodiscard]] std::vector<std::string> readStringArray(
    const core::ConfigDocument& document,
    std::string_view prefix) {
    std::vector<std::string> values;
    for (std::size_t index = 0;; ++index) {
        const auto key = std::string(prefix) + "." + std::to_string(index);
        auto value = document.stringValue(key);
        if (!value.has_value()) {
            break;
        }
        values.push_back(std::move(*value));
    }
    return values;
}

[[nodiscard]] std::string pathKey(const std::filesystem::path& path) {
    return path.generic_string();
}

} // namespace

std::filesystem::path metadataPathForCookedAsset(const AssetRecord& record) {
    auto path = record.cookedPath;
    path.replace_extension(".metadata.json");
    return path;
}

GltfAssetMetadataLoadResult parseGltfAssetMetadata(
    const core::ConfigDocument& document,
    GltfAssetMetadata& outMetadata) {
    GltfAssetMetadata metadata{};
    GltfAssetMetadataLoadResult result;

    metadata.id = document.stringOr("id", "");
    metadata.sourcePath = document.stringOr("source", "");
    metadata.exportPath = document.stringOr("export", "");
    metadata.category = document.stringOr("category", "");
    metadata.scaleMeters = document.boolOr("scale_meters", false);
    metadata.runtimeUpAxis = document.stringOr("runtime_up_axis", "");
    metadata.gameplayForwardAxis = document.stringOr("gameplay_forward_axis", "");
    metadata.sockets = readStringArray(document, "sockets");
    metadata.collision = document.stringOr("collision", "");
    metadata.lods = readStringArray(document, "lods");
    metadata.license = document.stringOr("license", "");
    metadata.generatedBy = document.stringOr("generated_by", "");

    if (metadata.id.empty()) {
        result.errors.push_back("glTF metadata is missing id");
    }
    if (metadata.exportPath.empty()) {
        result.errors.push_back("glTF metadata '" + metadata.id + "' is missing export path");
    }
    if (!metadata.scaleMeters) {
        result.errors.push_back("glTF metadata '" + metadata.id + "' must declare scale_meters=true");
    }
    if (metadata.runtimeUpAxis != "Y") {
        result.errors.push_back("glTF metadata '" + metadata.id + "' must declare runtime_up_axis=Y");
    }
    if (metadata.gameplayForwardAxis.empty()) {
        result.errors.push_back("glTF metadata '" + metadata.id + "' is missing gameplay_forward_axis");
    }
    if (metadata.license.empty()) {
        result.errors.push_back("glTF metadata '" + metadata.id + "' is missing license");
    }

    if (result.ok()) {
        outMetadata = std::move(metadata);
    }
    return result;
}

GltfAssetMetadataLoadResult loadGltfAssetMetadataFromJson(
    const std::filesystem::path& path,
    GltfAssetMetadata& outMetadata) {
    const auto file = io::readTextFile(path);
    if (!file.has_value()) {
        return GltfAssetMetadataLoadResult{{"Could not read glTF metadata: " + path.string()}};
    }

    core::ConfigDocument document;
    const auto parseResult = core::parseJsonConfig(file->text, document);
    if (!parseResult.ok()) {
        GltfAssetMetadataLoadResult result;
        for (const auto& error : parseResult.errors) {
            result.errors.push_back(error.message + " at offset " + std::to_string(error.offset));
        }
        return result;
    }

    return parseGltfAssetMetadata(document, outMetadata);
}

std::vector<std::string> validateGltfAssetMetadata(
    const AssetRecord& record,
    const GltfAssetMetadata& metadata) {
    std::vector<std::string> errors;
    if (metadata.id != record.id) {
        errors.push_back("glTF metadata id '" + metadata.id + "' does not match asset id '" + record.id + "'");
    }
    if (!record.cookedPath.empty() && pathKey(metadata.exportPath) != pathKey(record.cookedPath)) {
        errors.push_back("glTF metadata export path does not match cooked path for '" + record.id + "'");
    }
    if (!record.sourcePath.empty() && pathKey(metadata.sourcePath) != pathKey(record.sourcePath)) {
        errors.push_back("glTF metadata source path does not match source path for '" + record.id + "'");
    }
    if (metadata.license != "original_project_asset") {
        errors.push_back("glTF metadata '" + record.id + "' has unsupported license '" + metadata.license + "'");
    }
    return errors;
}

} // namespace novacore::assets
