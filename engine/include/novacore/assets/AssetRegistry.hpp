#pragma once

#include "novacore/assets/AssetManifest.hpp"
#include "novacore/assets/AssetTypes.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace novacore::assets {

class AssetRegistry final {
public:
    void clear();
    void mountManifest(const AssetManifest& manifest);

    [[nodiscard]] const AssetRecord* find(std::string_view id) const;
    [[nodiscard]] bool contains(std::string_view id) const;
    [[nodiscard]] std::size_t assetCount() const;
    [[nodiscard]] std::size_t manifestCount() const;
    [[nodiscard]] std::vector<AssetId> idsForTag(std::string_view tag) const;
    [[nodiscard]] std::vector<AssetId> streamableAssetIds() const;

private:
    std::vector<std::string> manifestNames_;
    std::unordered_map<std::string, AssetRecord> recordsById_;
};

} // namespace novacore::assets
