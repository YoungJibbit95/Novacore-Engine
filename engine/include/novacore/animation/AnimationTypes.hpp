#pragma once

#include "novacore/math/Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace novacore::animation {

using JointIndex = std::uint32_t;
inline constexpr JointIndex invalidJointIndex = std::numeric_limits<JointIndex>::max();

struct Transform final {
    math::Vec3 translation{};
    math::Quat rotation{};
    math::Vec3 scale{1.0F, 1.0F, 1.0F};
};

struct Mat4 final {
    std::array<float, 16> values{
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
    };

    [[nodiscard]] constexpr float at(std::size_t row, std::size_t column) const { return values[(row * 4U) + column]; }

    constexpr float& at(std::size_t row, std::size_t column) { return values[(row * 4U) + column]; }
};

struct LocalPose final {
    std::vector<Transform> joints;
};

struct GlobalPose final {
    std::vector<Mat4> joints;
};

enum class ValidationSeverity {
    Warning,
    Error,
};

struct ValidationIssue final {
    ValidationSeverity severity = ValidationSeverity::Error;
    std::string path;
    std::string message;
};

struct ValidationResult final {
    std::vector<ValidationIssue> issues;

    [[nodiscard]] bool valid() const;
    [[nodiscard]] std::size_t errorCount() const;
    void addError(std::string path, std::string message);
    void addWarning(std::string path, std::string message);
    void append(const ValidationResult& other, std::string_view pathPrefix = {});
};

[[nodiscard]] bool isFinite(math::Vec3 value);
[[nodiscard]] bool isFinite(math::Quat value);
[[nodiscard]] bool isFinite(const Transform& value);
[[nodiscard]] float quaternionLengthSquared(math::Quat value);
[[nodiscard]] math::Quat normalize(math::Quat value);
[[nodiscard]] math::Quat conjugate(math::Quat value);
[[nodiscard]] math::Quat multiply(math::Quat lhs, math::Quat rhs);
[[nodiscard]] math::Vec3 rotate(math::Quat rotation, math::Vec3 value);
[[nodiscard]] math::Vec3 lerp(math::Vec3 from, math::Vec3 to, float weight);
[[nodiscard]] math::Quat slerp(math::Quat from, math::Quat to, float weight);
[[nodiscard]] Transform blend(const Transform& from, const Transform& to, float weight);
[[nodiscard]] Mat4 matrixFromTransform(const Transform& transform);
[[nodiscard]] Mat4 multiply(const Mat4& lhs, const Mat4& rhs);
[[nodiscard]] math::Vec3 transformPoint(const Mat4& transform, math::Vec3 point);
[[nodiscard]] math::Vec3 transformDirection(const Mat4& transform, math::Vec3 direction);
[[nodiscard]] math::Vec3 matrixTranslation(const Mat4& transform);

} // namespace novacore::animation
