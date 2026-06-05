#include "novacore/assets/AssetRegistry.hpp"

#include <algorithm>
#include <utility>

namespace novacore::assets {

void AssetRegistry::clear() {
    manifestNames_.clear();
    recordsById_.clear();
}

void AssetRegistry::mountManifest(const AssetManifest& manifest) {
    manifestNames_.push_back(manifest.name());
    for (const auto& record : manifest.records()) {
        recordsById_.insert_or_assign(record.id, record);
    }
}

const AssetRecord* AssetRegistry::find(std::string_view id) const {
    const auto it = recordsById_.find(std::string(id));
    if (it == recordsById_.end()) {
        return nullptr;
    }
    return &it->second;
}

bool AssetRegistry::contains(std::string_view id) const {
    return find(id) != nullptr;
}

std::size_t AssetRegistry::assetCount() const {
    return recordsById_.size();
}

std::size_t AssetRegistry::manifestCount() const {
    return manifestNames_.size();
}

std::vector<AssetId> AssetRegistry::idsForTag(std::string_view tag) const {
    std::vector<AssetId> ids;
    const std::string wantedTag(tag);
    for (const auto& [id, record] : recordsById_) {
        if (std::find(record.tags.begin(), record.tags.end(), wantedTag) != record.tags.end()) {
            ids.push_back(id);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<AssetId> AssetRegistry::streamableAssetIds() const {
    std::vector<AssetId> ids;
    for (const auto& [id, record] : recordsById_) {
        if (record.streamable) {
            ids.push_back(id);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

} // namespace novacore::assets
