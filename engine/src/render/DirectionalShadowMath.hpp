#pragma once

#include "CameraMath.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace novacore::render::detail {

[[nodiscard]] inline float shadowDot(math::Vec3 a, math::Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

[[nodiscard]] inline math::Vec3 shadowCross(math::Vec3 a, math::Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] inline bool finiteShadowPoint(math::Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] inline bool validShadowReceiverCamera(const RenderCamera3D& camera) {
    return finiteShadowPoint(camera.position) && std::isfinite(camera.yawDegrees) &&
           std::isfinite(camera.pitchDegrees) && std::isfinite(camera.rollDegrees) &&
           std::isfinite(camera.verticalFovDegrees) && std::isfinite(camera.nearPlane) &&
           std::isfinite(camera.farPlane);
}

[[nodiscard]] inline bool validDirectionalShadowSettings(std::uint32_t resolution, float distance, float depthBias,
                                                         float normalBias) {
    return (resolution == 256 || resolution == 512 || resolution == 1024 || resolution == 2048) &&
           std::isfinite(distance) && distance >= 1.0F && distance <= 150.0F && std::isfinite(depthBias) &&
           depthBias >= 0.0F && depthBias <= 0.05F && std::isfinite(normalBias) && normalBias >= 0.0F &&
           normalBias <= 0.05F;
}

struct DirectionalShadowCamera final {
    Mat4 viewProjection{};
    math::Vec3 towardSun{};
    math::Vec3 right{};
    math::Vec3 up{};
    math::Vec3 forward{};
    math::Vec3 center{};
    float radius = 0;
    float texelWorldSize = 0;
    float receiverDistance = 0;
};

// Covers a camera-centred receiver sphere of radius distance. Casters may lie
// outside that sphere toward the sun, but must intersect the bounded light
// frustum: XY +/-distance and depth 0..4*distance. No cascade/world-size claim.
[[nodiscard]] inline std::optional<DirectionalShadowCamera>
directionalShadowCamera(math::Vec3 cameraPosition, math::Vec3 towardSun, float distance, std::uint32_t resolution) {
    if (!finiteShadowPoint(cameraPosition) || !finiteShadowPoint(towardSun) ||
        !validDirectionalShadowSettings(resolution, distance, 0, 0)) {
        return std::nullopt;
    }
    const float magnitude = std::max({std::abs(towardSun.x), std::abs(towardSun.y), std::abs(towardSun.z)});
    if (magnitude == 0) {
        return std::nullopt;
    }
    towardSun = {towardSun.x / magnitude, towardSun.y / magnitude, towardSun.z / magnitude};
    towardSun = towardSun * (1.0F / std::sqrt(towardSun.lengthSquared()));
    const auto forward = towardSun * -1.0F;
    const math::Vec3 referenceUp = std::abs(forward.y) > 0.98F ? math::Vec3{0, 0, 1} : math::Vec3{0, 1, 0};
    auto right = shadowCross(referenceUp, forward);
    right = right * (1.0F / std::sqrt(right.lengthSquared()));
    const auto up = shadowCross(forward, right);
    // One texel of border contains the whole receiver sphere even when its
    // light-space centre moves by half a texel during snapping.
    const float texel = 2.0F * distance / static_cast<float>(resolution - 2U);
    const float extent = distance + texel;
    const float centerX = std::round(shadowDot(cameraPosition, right) / texel) * texel;
    const float centerY = std::round(shadowDot(cameraPosition, up) / texel) * texel;
    const float centerZ = shadowDot(cameraPosition, forward);
    const auto center = right * centerX + up * centerY + forward * centerZ;
    if (!finiteShadowPoint(center)) {
        return std::nullopt;
    }
    const auto eye = center - forward * (2.0F * extent);
    Mat4 view = identity();
    const std::array<math::Vec3, 3> axes{right, up, forward};
    for (std::size_t row = 0; row < axes.size(); ++row) {
        view.at(row, 0) = axes[row].x;
        view.at(row, 1) = axes[row].y;
        view.at(row, 2) = axes[row].z;
        view.at(row, 3) = -shadowDot(axes[row], eye);
    }
    Mat4 projection = identity();
    projection.at(0, 0) = 1.0F / extent;
    projection.at(1, 1) = -1.0F / extent; // same Vulkan Y convention as world geometry
    projection.at(2, 2) = 1.0F / (4.0F * extent);
    return DirectionalShadowCamera{
        multiply(projection, view), towardSun, right, up, forward, center, extent, texel, distance};
}

[[nodiscard]] inline math::Vec3 shadowProjectPoint(const Mat4& matrix, math::Vec3 point) {
    return {
        matrix.at(0, 0) * point.x + matrix.at(0, 1) * point.y + matrix.at(0, 2) * point.z + matrix.at(0, 3),
        matrix.at(1, 0) * point.x + matrix.at(1, 1) * point.y + matrix.at(1, 2) * point.z + matrix.at(1, 3),
        matrix.at(2, 0) * point.x + matrix.at(2, 1) * point.y + matrix.at(2, 2) * point.z + matrix.at(2, 3),
    };
}

[[nodiscard]] inline bool shadowCasterSphereVisible(const DirectionalShadowCamera& light, math::Vec3 center,
                                                    float radius) {
    if (!finiteShadowPoint(center) || !std::isfinite(radius) || radius < 0) {
        return false;
    }
    const auto p = shadowProjectPoint(light.viewProjection, center);
    const float extent = radius / light.radius;
    return p.x + extent >= -1 && p.x - extent <= 1 && p.y + extent >= -1 && p.y - extent <= 1 &&
           p.z + extent * 0.25F >= 0 && p.z - extent * 0.25F <= 1;
}

struct ShadowMeshBounds final {
    math::Vec3 minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max()};
    math::Vec3 maximum{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                       -std::numeric_limits<float>::max()};
    bool valid = false;

    void include(math::Vec3 point) {
        if (!finiteShadowPoint(point)) {
            return;
        }
        minimum = {std::min(minimum.x, point.x), std::min(minimum.y, point.y), std::min(minimum.z, point.z)};
        maximum = {std::max(maximum.x, point.x), std::max(maximum.y, point.y), std::max(maximum.z, point.z)};
        valid = true;
    }
};

struct ShadowSphere final {
    math::Vec3 center{};
    float radius = 0;
};

// Transform the cooked/current skinned AABB once by the render instance. Eight
// corners retain a conservative bound for rotation and signed/nonuniform scale.
[[nodiscard]] inline std::optional<ShadowSphere> transformedShadowBounds(const ShadowMeshBounds& bounds,
                                                                         const Mat4& model) {
    if (!bounds.valid) {
        return std::nullopt;
    }
    const auto localCenter = bounds.minimum * 0.5F + bounds.maximum * 0.5F;
    const auto center = shadowProjectPoint(model, localCenter);
    float radius = 0;
    for (unsigned mask = 0; mask < 8; ++mask) {
        const math::Vec3 corner{(mask & 1U) ? bounds.maximum.x : bounds.minimum.x,
                                (mask & 2U) ? bounds.maximum.y : bounds.minimum.y,
                                (mask & 4U) ? bounds.maximum.z : bounds.minimum.z};
        const auto delta = shadowProjectPoint(model, corner) - center;
        const auto length = std::hypot(delta.x, delta.y, delta.z);
        if (!finiteShadowPoint(delta) || !std::isfinite(length)) {
            return std::nullopt;
        }
        radius = std::max(radius, length);
    }
    return ShadowSphere{center, radius};
}

// std140 layout, shared with shadow_data.glsl. Camera rays reconstruct world
// position from clip.x/y/w without an inverse-depth divide or another model
// transform, preserving the existing 128-byte instance push block.
struct DirectionalShadowUniforms final {
    std::array<float, 16> lightViewProjection{};
    std::array<float, 4> cameraRight{};
    std::array<float, 4> cameraDown{};
    std::array<float, 4> cameraForward{};
    std::array<float, 4> cameraPositionDistance{};
    std::array<float, 4> parameters{}; // receiver depth bias, normal bias, texel size, enabled
    std::array<float, 4> towardSun{};
};
static_assert(sizeof(DirectionalShadowUniforms) == 160);
static_assert(offsetof(DirectionalShadowUniforms, cameraRight) == 64);
static_assert(offsetof(DirectionalShadowUniforms, cameraDown) == 80);
static_assert(offsetof(DirectionalShadowUniforms, cameraForward) == 96);
static_assert(offsetof(DirectionalShadowUniforms, cameraPositionDistance) == 112);
static_assert(offsetof(DirectionalShadowUniforms, parameters) == 128);
static_assert(offsetof(DirectionalShadowUniforms, towardSun) == 144);

[[nodiscard]] inline DirectionalShadowUniforms
directionalShadowUniforms(const DirectionalShadowCamera& light, const RenderCamera3D& camera, std::uint32_t width,
                          std::uint32_t height, float depthBias, float normalBias, bool enabled) {
    const auto rays = skyRayBasis(camera, width, height);
    return {
        light.viewProjection.value,
        {rays.right.x, rays.right.y, rays.right.z, 0},
        {rays.down.x, rays.down.y, rays.down.z, 0},
        {rays.forward.x, rays.forward.y, rays.forward.z, 0},
        {camera.position.x, camera.position.y, camera.position.z, light.receiverDistance},
        {depthBias, normalBias, light.texelWorldSize / (2.0F * light.radius), enabled ? 1.0F : 0.0F},
        {light.towardSun.x, light.towardSun.y, light.towardSun.z, 0},
    };
}

} // namespace novacore::render::detail
