#pragma once

#include "novacore/render/Renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace novacore::render::detail {

// RenderMesh3D is T * Ry * Rx * Rz * S. Its inverse-transpose normal
// transform is therefore R * inverse(S); imported/skinned node transforms
// are already represented by the input vertex and are not applied again.
struct MeshNormalTransform final {
    std::array<float, 4> rotation{};
    std::array<float, 3> inverseScale{};
};

[[nodiscard]] inline std::optional<MeshNormalTransform> meshNormalTransform(const RenderMesh3D& mesh) {
    const std::array<float, 3> scale{mesh.scale.x, mesh.scale.y, mesh.scale.z};
    if (!std::isfinite(mesh.yawDegrees) || !std::isfinite(mesh.pitchDegrees) || !std::isfinite(mesh.rollDegrees)) {
        return std::nullopt;
    }
    MeshNormalTransform result{};
    for (std::size_t i = 0; i < scale.size(); ++i) {
        if (!std::isfinite(scale[i]) || scale[i] == 0.0F) {
            return std::nullopt;
        }
        result.inverseScale[i] = 1.0F / scale[i];
        if (!std::isfinite(result.inverseScale[i])) {
            return std::nullopt;
        }
    }
    constexpr float halfRadians = 3.14159265358979323846F / 360.0F;
    const float sy = std::sin(mesh.yawDegrees * halfRadians);
    const float cy = std::cos(mesh.yawDegrees * halfRadians);
    const float sx = std::sin(mesh.pitchDegrees * halfRadians);
    const float cx = std::cos(mesh.pitchDegrees * halfRadians);
    const float sz = std::sin(mesh.rollDegrees * halfRadians);
    const float cz = std::cos(mesh.rollDegrees * halfRadians);
    result.rotation = {
        cy * sx * cz + sy * cx * sz,
        sy * cx * cz - cy * sx * sz,
        cy * cx * sz - sy * sx * cz,
        cy * cx * cz + sy * sx * sz,
    };
    return result;
}

// Six existing [0,1] material/light factors use unorm16 (absolute error
// <= 0.5 / 65535), leaving full precision for WVP, RGBA and normal transforms
// while keeping the complete draw push block within Vulkan's 128-byte minimum.
[[nodiscard]] inline std::uint32_t packUnitPair(float low, float high) {
    const auto encode = [](float value) {
        if (!std::isfinite(value)) {
            value = 0.0F;
        }
        return static_cast<std::uint32_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 65535.0F));
    };
    return encode(low) | (encode(high) << 16U);
}

// Octahedral unit-direction packing. Both hemispheres and zero components
// are explicit; GLSL decodes with unpackUnorm2x16 and normalizes the result.
[[nodiscard]] inline std::uint32_t packLightDirection(math::Vec3 direction) {
    const float lengthSquared = direction.lengthSquared();
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.000001F) {
        direction = {0.0F, 0.0F, 1.0F};
    }
    const float divisor = std::abs(direction.x) + std::abs(direction.y) + std::abs(direction.z);
    float x = direction.x / divisor;
    float y = direction.y / divisor;
    if (direction.z < 0.0F) {
        const float oldX = x;
        x = (1.0F - std::abs(y)) * (oldX < 0.0F ? -1.0F : 1.0F);
        y = (1.0F - std::abs(oldX)) * (y < 0.0F ? -1.0F : 1.0F);
    }
    return packUnitPair(x * 0.5F + 0.5F, y * 0.5F + 0.5F);
}

} // namespace novacore::render::detail
