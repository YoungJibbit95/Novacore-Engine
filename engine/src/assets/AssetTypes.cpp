#include "novacore/assets/AssetTypes.hpp"

namespace novacore::assets {

std::string_view assetKindName(AssetKind kind) {
    switch (kind) {
    case AssetKind::Mesh:
        return "mesh";
    case AssetKind::Material:
        return "material";
    case AssetKind::Texture:
        return "texture";
    case AssetKind::Scene:
        return "scene";
    case AssetKind::Animation:
        return "animation";
    case AssetKind::Audio:
        return "audio";
    case AssetKind::Shader:
        return "shader";
    case AssetKind::Data:
        return "data";
    }
    return "data";
}

std::optional<AssetKind> assetKindFromString(std::string_view text) {
    if (text == "mesh") {
        return AssetKind::Mesh;
    }
    if (text == "material") {
        return AssetKind::Material;
    }
    if (text == "texture") {
        return AssetKind::Texture;
    }
    if (text == "scene") {
        return AssetKind::Scene;
    }
    if (text == "animation") {
        return AssetKind::Animation;
    }
    if (text == "audio") {
        return AssetKind::Audio;
    }
    if (text == "shader") {
        return AssetKind::Shader;
    }
    if (text == "data") {
        return AssetKind::Data;
    }
    return std::nullopt;
}

std::string_view assetLoadStateName(AssetLoadState state) {
    switch (state) {
    case AssetLoadState::Unloaded:
        return "unloaded";
    case AssetLoadState::Queued:
        return "queued";
    case AssetLoadState::Loading:
        return "loading";
    case AssetLoadState::Resident:
        return "resident";
    case AssetLoadState::Failed:
        return "failed";
    }
    return "failed";
}

} // namespace novacore::assets
