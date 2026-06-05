#pragma once

#include "novacore/assets/AssetTypes.hpp"
#include "novacore/core/ConfigDocument.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace novacore::assets {

struct AssetManifestLoadResult final {
    std::vector<std::string> errors;

    [[nodiscard]] bool ok() const {
        return errors.empty();
    }
};

class AssetManifest final {
public:
    void clear();
    void setName(std::string name);
    void setRoot(std::filesystem::path root);

    [[nodiscard]] const std::string& name() const;
    [[nodiscard]] const std::filesystem::path& root() const;
    [[nodiscard]] bool addRecord(AssetRecord record);
    [[nodiscard]] const AssetRecord* find(std::string_view id) const;
    [[nodiscard]] bool contains(std::string_view id) const;
    [[nodiscard]] std::size_t recordCount() const;
    [[nodiscard]] const std::vector<AssetRecord>& records() const;

private:
    std::string name_;
    std::filesystem::path root_;
    std::vector<AssetRecord> records_;
    std::unordered_map<std::string, std::size_t> indexById_;
};

[[nodiscard]] AssetManifestLoadResult parseAssetManifest(
    const core::ConfigDocument& document,
    AssetManifest& outManifest);

[[nodiscard]] AssetManifestLoadResult loadAssetManifestFromJson(
    const std::filesystem::path& path,
    AssetManifest& outManifest);

} // namespace novacore::assets
