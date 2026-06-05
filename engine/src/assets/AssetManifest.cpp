#include "novacore/assets/AssetManifest.hpp"

#include "novacore/io/FileSystem.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

namespace novacore::assets {

namespace {

[[nodiscard]] std::string assetKey(std::size_t index, std::string_view field) {
    return "assets." + std::to_string(index) + "." + std::string(field);
}

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

[[nodiscard]] std::uint64_t nonNegativeIntOr(
    const core::ConfigDocument& document,
    std::string_view key,
    std::uint64_t fallback) {
    const auto value = document.intValue(key);
    if (!value.has_value() || *value < 0) {
        return fallback;
    }
    return static_cast<std::uint64_t>(*value);
}

} // namespace

void AssetManifest::clear() {
    name_.clear();
    root_.clear();
    records_.clear();
    indexById_.clear();
}

void AssetManifest::setName(std::string name) {
    name_ = std::move(name);
}

void AssetManifest::setRoot(std::filesystem::path root) {
    root_ = std::move(root);
}

const std::string& AssetManifest::name() const {
    return name_;
}

const std::filesystem::path& AssetManifest::root() const {
    return root_;
}

bool AssetManifest::addRecord(AssetRecord record) {
    if (record.id.empty() || indexById_.contains(record.id)) {
        return false;
    }

    indexById_.emplace(record.id, records_.size());
    records_.push_back(std::move(record));
    return true;
}

const AssetRecord* AssetManifest::find(std::string_view id) const {
    const auto it = indexById_.find(std::string(id));
    if (it == indexById_.end()) {
        return nullptr;
    }
    return &records_[it->second];
}

bool AssetManifest::contains(std::string_view id) const {
    return find(id) != nullptr;
}

std::size_t AssetManifest::recordCount() const {
    return records_.size();
}

const std::vector<AssetRecord>& AssetManifest::records() const {
    return records_;
}

AssetManifestLoadResult parseAssetManifest(
    const core::ConfigDocument& document,
    AssetManifest& outManifest) {
    AssetManifest manifest;
    AssetManifestLoadResult result;

    manifest.setName(document.stringOr("manifest.name", "unnamed_manifest"));
    manifest.setRoot(document.stringOr("manifest.root", "assets"));

    for (std::size_t index = 0;; ++index) {
        const auto idKey = assetKey(index, "id");
        if (!document.contains(idKey)) {
            break;
        }

        AssetRecord record{};
        record.id = document.stringOr(idKey, "");
        if (record.id.empty()) {
            result.errors.push_back("Asset at index " + std::to_string(index) + " is missing id");
            continue;
        }

        const auto kindText = document.stringOr(assetKey(index, "kind"), "data");
        const auto kind = assetKindFromString(kindText);
        if (!kind.has_value()) {
            result.errors.push_back("Asset '" + record.id + "' has unknown kind '" + kindText + "'");
            continue;
        }
        record.kind = *kind;

        record.sourcePath = document.stringOr(assetKey(index, "source"), "");
        record.cookedPath = document.stringOr(assetKey(index, "cooked"), "");
        record.streamable = document.boolOr(assetKey(index, "streamable"), false);
        record.priority = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            nonNegativeIntOr(document, assetKey(index, "priority"), 0),
            1000000U));
        record.estimatedBytes = nonNegativeIntOr(document, assetKey(index, "estimated_bytes"), 0);
        record.dependencies = readStringArray(document, assetKey(index, "dependencies"));
        record.tags = readStringArray(document, assetKey(index, "tags"));

        if (!manifest.addRecord(std::move(record))) {
            result.errors.push_back("Duplicate asset id at index " + std::to_string(index));
        }
    }

    if (manifest.recordCount() == 0) {
        result.errors.push_back("Asset manifest contains no assets");
    }

    if (result.ok()) {
        outManifest = std::move(manifest);
    }
    return result;
}

AssetManifestLoadResult loadAssetManifestFromJson(
    const std::filesystem::path& path,
    AssetManifest& outManifest) {
    const auto file = io::readTextFile(path);
    if (!file.has_value()) {
        return AssetManifestLoadResult{{"Could not read asset manifest: " + path.string()}};
    }

    core::ConfigDocument document;
    const auto parseResult = core::parseJsonConfig(file->text, document);
    if (!parseResult.ok()) {
        AssetManifestLoadResult result;
        for (const auto& error : parseResult.errors) {
            result.errors.push_back(error.message + " at offset " + std::to_string(error.offset));
        }
        return result;
    }

    return parseAssetManifest(document, outManifest);
}

} // namespace novacore::assets
