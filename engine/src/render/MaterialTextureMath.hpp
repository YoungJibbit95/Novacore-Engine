#pragma once

#include "DirectionalShadowMath.hpp"
#include <span>

namespace novacore::render::detail {

struct MaterialImageBudget final {
    std::uint64_t limit = 0;
    std::uint64_t used = 0;
    std::uint64_t rejected = 0;

    bool reserve(std::uint64_t bytes) {
        if (bytes == 0 || used > limit || bytes > limit - used) {
            ++rejected;
            return false;
        }
        used += bytes;
        return true;
    }
    bool release(std::uint64_t bytes) {
        if (bytes > used) {
            return false;
        }
        used -= bytes;
        return true;
    }
};

struct MaterialTextureUniforms final {
    std::array<float, 4> mapEnabled{}; // base color, metallic/roughness, normal, emissive
    std::array<float, 4> emissiveNormalScale{};
    std::array<std::uint32_t, 4> uvSets{};
};
static_assert(sizeof(MaterialTextureUniforms) == 48);
static_assert(offsetof(MaterialTextureUniforms, emissiveNormalScale) == 16);
static_assert(offsetof(MaterialTextureUniforms, uvSets) == 32);

[[nodiscard]] inline MaterialTextureUniforms materialFactorFallbackUniforms(const assets::GltfMaterialData& material) {
    MaterialTextureUniforms result{};
    result.emissiveNormalScale[3] = 1;
    const auto emissive = material.emissiveFactor * material.emissiveStrength;
    if (std::isfinite(material.emissiveStrength) && material.emissiveStrength >= 0 && finiteShadowPoint(emissive) &&
        emissive.x >= 0 && emissive.y >= 0 && emissive.z >= 0) {
        result.emissiveNormalScale = {emissive.x, emissive.y, emissive.z, 1};
    }
    return result;
}

[[nodiscard]] inline std::optional<MaterialTextureUniforms>
materialTextureUniforms(const assets::GltfMaterialData& material) {
    const std::array coordinates{material.baseColorTexCoord, material.metallicRoughnessTexCoord,
                                 material.normalTexCoord, material.emissiveTexCoord};
    const std::array indices{material.baseColorTextureIndex, material.metallicRoughnessTextureIndex,
                             material.normalTextureIndex, material.emissiveTextureIndex};
    if (material.alphaMode != "OPAQUE" || !std::isfinite(material.normalScale) ||
        !std::isfinite(material.emissiveStrength) || material.emissiveStrength < 0 ||
        !std::isfinite(material.metallicFactor) || material.metallicFactor < 0 || material.metallicFactor > 1 ||
        !std::isfinite(material.roughnessFactor) || material.roughnessFactor < 0 || material.roughnessFactor > 1) {
        return std::nullopt;
    }
    for (std::size_t slot = 0; slot < coordinates.size(); ++slot) {
        if (indices[slot] >= 0 && (coordinates[slot] > 1 || material.textureTransforms[slot].enabled)) {
            return std::nullopt;
        }
    }
    for (const auto value : material.baseColorFactor) {
        if (!std::isfinite(value) || value < 0 || value > 1) {
            return std::nullopt;
        }
    }
    const auto emissive = material.emissiveFactor * material.emissiveStrength;
    if (!finiteShadowPoint(emissive) || emissive.x < 0 || emissive.y < 0 || emissive.z < 0) {
        return std::nullopt;
    }
    return MaterialTextureUniforms{{}, {emissive.x, emissive.y, emissive.z, material.normalScale}, coordinates};
}

struct MaterialFrameUniforms final {
    std::array<float, 4> right{};
    std::array<float, 4> down{};
    std::array<float, 4> forward{};
    std::array<float, 4> eye{};
    std::array<float, 4> parameters{}; // manual sRGB encoding for a UNORM presentation attachment
};
static_assert(sizeof(MaterialFrameUniforms) == 80);
static_assert(offsetof(MaterialFrameUniforms, parameters) == 64);

[[nodiscard]] inline MaterialFrameUniforms materialFrameUniforms(const RenderCamera3D& camera, std::uint32_t width,
                                                                 std::uint32_t height, bool encodeSrgb) {
    const auto rays = skyRayBasis(camera, width, height);
    return {{rays.right.x, rays.right.y, rays.right.z, 0},
            {rays.down.x, rays.down.y, rays.down.z, 0},
            {rays.forward.x, rays.forward.y, rays.forward.z, 0},
            {camera.position.x, camera.position.y, camera.position.z, 0},
            {encodeSrgb ? 1.0F : 0.0F, 0, 0, 0}};
}

[[nodiscard]] inline std::uint64_t imageContentHash(std::span<const std::uint8_t> bytes) {
    std::uint64_t value = 14695981039346656037ULL;
    for (const auto byte : bytes) {
        value = (value ^ byte) * 1099511628211ULL;
    }
    return value;
}

[[nodiscard]] inline bool sameImageContent(const std::shared_ptr<const std::vector<std::uint8_t>>& a,
                                           const std::shared_ptr<const std::vector<std::uint8_t>>& b) {
    return a && b && (a == b || *a == *b);
}

} // namespace novacore::render::detail
