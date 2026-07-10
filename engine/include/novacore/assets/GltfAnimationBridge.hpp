#pragma once

#include "novacore/animation/AnimationRuntime.hpp"
#include "novacore/assets/GltfDocument.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace novacore::assets {

struct GltfAnimationAsset final {
    std::filesystem::path sourcePath;
    std::size_t skinIndex = 0;
    animation::Skeleton skeleton;
    std::vector<animation::Mat4> inverseBindMatrices;
    std::vector<std::size_t> jointNodeIndices;
    std::vector<animation::AnimationClip> clips;

    [[nodiscard]] const animation::AnimationClip* findClip(std::string_view name) const;
};

struct GltfAnimationBridgeResult final {
    std::vector<std::string> warnings;
    std::vector<std::string> errors;

    [[nodiscard]] bool ok() const {
        return errors.empty();
    }
};

[[nodiscard]] GltfAnimationBridgeResult buildGltfAnimationAsset(
    const GltfAnimationData& source,
    std::size_t skinIndex,
    GltfAnimationAsset& outAsset);

[[nodiscard]] GltfAnimationBridgeResult skinGltfMesh(
    const GltfMeshData& source,
    const GltfAnimationAsset& animationAsset,
    const animation::GlobalPose& pose,
    GltfMeshData& outMesh);

} // namespace novacore::assets
