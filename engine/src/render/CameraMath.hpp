#pragma once

#include "novacore/render/Renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace novacore::render::detail {

struct Mat4 final {
    std::array<float, 16> value{};

    [[nodiscard]] float& at(std::size_t row, std::size_t column) { return value[column * 4U + row]; }
    [[nodiscard]] float at(std::size_t row, std::size_t column) const { return value[column * 4U + row]; }
};

[[nodiscard]] inline Mat4 identity() {
    Mat4 result{};
    for (std::size_t i = 0; i < 4; ++i) {
        result.at(i, i) = 1.0F;
    }
    return result;
}

[[nodiscard]] inline Mat4 multiply(const Mat4& lhs, const Mat4& rhs) {
    Mat4 result{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            for (std::size_t k = 0; k < 4; ++k) {
                result.at(row, column) += lhs.at(row, k) * rhs.at(k, column);
            }
        }
    }
    return result;
}

struct CameraFrame final {
    math::Vec3 right{};
    math::Vec3 up{};
    math::Vec3 forward{};
};

// glTF world coordinates are right handed. Forward is the engine's logical
// look direction; the physical camera's local +Z points backward, so its
// proper rotation has columns (right, up, -forward). Positive pitch looks up.
// This analytic basis remains defined at vertical pitch.
[[nodiscard]] inline CameraFrame cameraFrame(const RenderCamera3D& camera) {
    constexpr float radians = 3.14159265358979323846F / 180.0F;
    const float yaw = camera.yawDegrees * radians;
    const float pitch = camera.pitchDegrees * radians;
    const float roll = camera.rollDegrees * radians;
    const math::Vec3 right{-std::cos(yaw), 0.0F, std::sin(yaw)};
    const math::Vec3 up{-std::sin(yaw) * std::sin(pitch), std::cos(pitch), -std::cos(yaw) * std::sin(pitch)};
    return {
        right * std::cos(roll) + up * std::sin(roll),
        up * std::cos(roll) - right * std::sin(roll),
        {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)},
    };
}

struct CameraProjection final {
    float horizontalRayScale = 1.0F;
    float verticalRayScale = 1.0F;
    float nearPlane = 0.03F;
    float farPlane = 1000.0F;
};

[[nodiscard]] inline CameraProjection cameraProjection(const RenderCamera3D& camera, std::uint32_t width,
                                                       std::uint32_t height) {
    constexpr float radians = 3.14159265358979323846F / 180.0F;
    const float aspect = static_cast<float>(std::max(width, 1U)) / static_cast<float>(std::max(height, 1U));
    const float vertical = std::tan(std::clamp(camera.verticalFovDegrees, 1.0F, 179.0F) * radians * 0.5F);
    const float nearPlane = std::max(0.001F, camera.nearPlane);
    return {vertical * aspect, vertical, nearPlane, std::max(nearPlane + 0.001F, camera.farPlane)};
}

// These vectors are also uploaded to sky.frag. Vulkan framebuffer NDC Y grows
// downward, so screen-up rays negate Y, matching the world projection Y flip.
struct SkyRayBasis final {
    math::Vec3 forward{};
    math::Vec3 right{};
    math::Vec3 down{};
};

[[nodiscard]] inline SkyRayBasis skyRayBasis(const RenderCamera3D& camera, std::uint32_t width, std::uint32_t height) {
    const auto basis = cameraFrame(camera);
    const auto projection = cameraProjection(camera, width, height);
    return {basis.forward, basis.right * projection.horizontalRayScale, basis.up * -projection.verticalRayScale};
}

[[nodiscard]] inline Mat4 cameraViewProjection(const RenderCamera3D& camera, std::uint32_t width,
                                               std::uint32_t height) {
    const auto basis = cameraFrame(camera);
    const auto projection = cameraProjection(camera, width, height);
    Mat4 view = identity();
    const std::array<math::Vec3, 3> axes{basis.right, basis.up, basis.forward * -1.0F};
    for (std::size_t row = 0; row < axes.size(); ++row) {
        view.at(row, 0) = axes[row].x;
        view.at(row, 1) = axes[row].y;
        view.at(row, 2) = axes[row].z;
        view.at(row, 3) =
            -(axes[row].x * camera.position.x + axes[row].y * camera.position.y + axes[row].z * camera.position.z);
    }
    Mat4 perspective{};
    perspective.at(0, 0) = 1.0F / projection.horizontalRayScale;
    perspective.at(1, 1) = -1.0F / projection.verticalRayScale;
    perspective.at(2, 2) = -projection.farPlane / (projection.farPlane - projection.nearPlane);
    perspective.at(2, 3) = -(projection.nearPlane * projection.farPlane) / (projection.farPlane - projection.nearPlane);
    perspective.at(3, 2) = -1.0F;
    return multiply(perspective, view);
}

} // namespace novacore::render::detail
