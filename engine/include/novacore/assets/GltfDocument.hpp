#pragma once

#include "novacore/core/ConfigDocument.hpp"
#include "novacore/math/Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace novacore::assets {

enum class GltfContainerKind {
    Text,
    BinaryGlb
};

struct GltfSceneInfo final {
    std::filesystem::path path;
    GltfContainerKind container = GltfContainerKind::Text;
    std::uint32_t glbVersion = 0;
    std::uint64_t declaredLengthBytes = 0;
    std::uint64_t jsonBytes = 0;
    std::uint64_t binaryBytes = 0;
    std::size_t meshCount = 0;
    std::size_t nodeCount = 0;
    std::size_t materialCount = 0;
    std::size_t accessorCount = 0;
    std::size_t bufferViewCount = 0;
    std::size_t bufferCount = 0;
    std::size_t imageCount = 0;
    std::size_t animationCount = 0;
    std::size_t skinCount = 0;
};

struct GltfSceneInfoLoadResult final {
    std::vector<std::string> errors;

    [[nodiscard]] bool ok() const {
        return errors.empty();
    }
};

struct GltfMatrix4 final {
    std::array<float, 16> values{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
};

struct GltfNodeTransform final {
    math::Vec3 translation{};
    math::Quat rotation{};
    math::Vec3 scale{1.0F, 1.0F, 1.0F};
    GltfMatrix4 matrix{};
    bool usesMatrix = false;
};

struct GltfNodeData final {
    std::size_t nodeIndex = 0;
    std::string name;
    int parentNodeIndex = -1;
    std::vector<std::size_t> children;
    int meshIndex = -1;
    int skinIndex = -1;
    GltfNodeTransform localTransform{};
};

struct GltfPrimitiveData final {
    std::size_t meshIndex = 0;
    std::size_t primitiveIndex = 0;
    int nodeIndex = -1;
    int skinIndex = -1;
    int materialIndex = -1;
    bool nodeTransformBaked = false;
    std::vector<math::Vec3> positions;
    std::vector<math::Vec3> normals;
    std::vector<math::Vec2> texcoords;
    std::vector<std::array<std::uint16_t, 4>> joints;
    std::vector<std::array<float, 4>> weights;
    std::vector<std::uint32_t> indices;
};

struct GltfNodeMarker final {
    std::string name;
    math::Vec3 worldPosition{};
    math::Vec3 worldForward{0.0F, 0.0F, 1.0F};
    math::Vec3 worldUp{0.0F, 1.0F, 0.0F};
    int meshIndex = -1;
};

struct GltfMaterialData final {
    std::size_t materialIndex = 0;
    std::string name;
    std::array<float, 4> baseColorFactor{1.0F, 1.0F, 1.0F, 1.0F};
    math::Vec3 emissiveFactor{};
    float metallicFactor = 1.0F;
    float roughnessFactor = 1.0F;
    int baseColorTextureIndex = -1;
    int metallicRoughnessTextureIndex = -1;
    int normalTextureIndex = -1;
    int emissiveTextureIndex = -1;
    bool doubleSided = false;
};

struct GltfAnimationData;

struct GltfMeshData final {
    std::filesystem::path path;
    GltfSceneInfo sceneInfo;
    std::vector<GltfNodeData> nodes;
    std::vector<GltfNodeMarker> nodeMarkers;
    std::vector<GltfMaterialData> materials;
    std::vector<GltfPrimitiveData> primitives;
    std::shared_ptr<const GltfAnimationData> animationData;

    [[nodiscard]] std::size_t primitiveCount() const {
        return primitives.size();
    }

    [[nodiscard]] std::size_t vertexCount() const;
    [[nodiscard]] std::size_t indexCount() const;
};

struct GltfJointData final {
    std::size_t nodeIndex = 0;
    std::string name;
    int parentJointIndex = -1;
    GltfNodeTransform localTransform{};
    GltfMatrix4 inverseBindMatrix{};
};

struct GltfSkinData final {
    std::size_t skinIndex = 0;
    std::string name;
    int skeletonRootNodeIndex = -1;
    std::vector<GltfJointData> joints;
};

enum class GltfAnimationInterpolation {
    Linear,
    Step,
    CubicSpline
};

enum class GltfAnimationTargetPath {
    Translation,
    Rotation,
    Scale
};

struct GltfVec3Keyframe final {
    float timeSeconds = 0.0F;
    math::Vec3 value{};
    math::Vec3 inTangent{};
    math::Vec3 outTangent{};
};

struct GltfQuatKeyframe final {
    float timeSeconds = 0.0F;
    math::Quat value{};
    math::Quat inTangent{0.0F, 0.0F, 0.0F, 0.0F};
    math::Quat outTangent{0.0F, 0.0F, 0.0F, 0.0F};
};

struct GltfAnimationChannelData final {
    std::size_t samplerIndex = 0;
    std::size_t targetNodeIndex = 0;
    GltfAnimationTargetPath targetPath = GltfAnimationTargetPath::Translation;
    GltfAnimationInterpolation interpolation = GltfAnimationInterpolation::Linear;
    std::vector<GltfVec3Keyframe> vectorKeyframes;
    std::vector<GltfQuatKeyframe> rotationKeyframes;
};

struct GltfAnimationClipData final {
    std::size_t animationIndex = 0;
    std::string name;
    float durationSeconds = 0.0F;
    std::vector<GltfAnimationChannelData> channels;
};

struct GltfAnimationData final {
    std::filesystem::path path;
    GltfSceneInfo sceneInfo;
    std::vector<GltfNodeData> nodes;
    std::vector<GltfSkinData> skins;
    std::vector<GltfAnimationClipData> animations;
};

struct GltfAnimationDataLoadResult final {
    std::vector<std::string> errors;

    [[nodiscard]] bool ok() const {
        return errors.empty();
    }
};

struct GltfMeshDataLoadResult final {
    std::vector<std::string> errors;

    [[nodiscard]] bool ok() const {
        return errors.empty();
    }
};

[[nodiscard]] std::string_view gltfContainerKindName(GltfContainerKind kind);

[[nodiscard]] GltfSceneInfoLoadResult parseGltfSceneInfoFromJson(
    std::string_view json,
    GltfSceneInfo& outInfo);

[[nodiscard]] GltfSceneInfoLoadResult loadGltfSceneInfo(
    const std::filesystem::path& path,
    GltfSceneInfo& outInfo);

[[nodiscard]] GltfMeshDataLoadResult loadGltfMeshData(
    const std::filesystem::path& path,
    GltfMeshData& outMeshData);

[[nodiscard]] GltfAnimationDataLoadResult loadGltfAnimationData(
    const std::filesystem::path& path,
    GltfAnimationData& outAnimationData);

} // namespace novacore::assets
