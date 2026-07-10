#include "novacore/animation/AnimationTypes.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace novacore::animation {

namespace {

constexpr float kQuaternionEpsilon = 1.0e-12F;

[[nodiscard]] float dot(math::Quat lhs, math::Quat rhs) {
    return (lhs.x * rhs.x) + (lhs.y * rhs.y) + (lhs.z * rhs.z) + (lhs.w * rhs.w);
}

} // namespace

bool ValidationResult::valid() const { return errorCount() == 0U; }

std::size_t ValidationResult::errorCount() const {
    return static_cast<std::size_t>(std::ranges::count_if(
        issues, [](const ValidationIssue& issue) { return issue.severity == ValidationSeverity::Error; }));
}

void ValidationResult::addError(std::string path, std::string message) {
    issues.push_back({ValidationSeverity::Error, std::move(path), std::move(message)});
}

void ValidationResult::addWarning(std::string path, std::string message) {
    issues.push_back({ValidationSeverity::Warning, std::move(path), std::move(message)});
}

void ValidationResult::append(const ValidationResult& other, std::string_view pathPrefix) {
    for (const auto& issue : other.issues) {
        auto path = std::string(pathPrefix);
        if (!path.empty() && !issue.path.empty()) {
            path += '.';
        }
        path += issue.path;
        issues.push_back({issue.severity, std::move(path), issue.message});
    }
}

bool isFinite(math::Vec3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

bool isFinite(math::Quat value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
}

bool isFinite(const Transform& value) {
    return isFinite(value.translation) && isFinite(value.rotation) && isFinite(value.scale);
}

float quaternionLengthSquared(math::Quat value) {
    return (value.x * value.x) + (value.y * value.y) + (value.z * value.z) + (value.w * value.w);
}

math::Quat normalize(math::Quat value) {
    const float lengthSquared = quaternionLengthSquared(value);
    if (!std::isfinite(lengthSquared) || lengthSquared <= kQuaternionEpsilon) {
        return {};
    }

    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    return {
        value.x * inverseLength,
        value.y * inverseLength,
        value.z * inverseLength,
        value.w * inverseLength,
    };
}

math::Quat conjugate(math::Quat value) { return {-value.x, -value.y, -value.z, value.w}; }

math::Quat multiply(math::Quat lhs, math::Quat rhs) {
    return normalize({
        (lhs.w * rhs.x) + (lhs.x * rhs.w) + (lhs.y * rhs.z) - (lhs.z * rhs.y),
        (lhs.w * rhs.y) - (lhs.x * rhs.z) + (lhs.y * rhs.w) + (lhs.z * rhs.x),
        (lhs.w * rhs.z) + (lhs.x * rhs.y) - (lhs.y * rhs.x) + (lhs.z * rhs.w),
        (lhs.w * rhs.w) - (lhs.x * rhs.x) - (lhs.y * rhs.y) - (lhs.z * rhs.z),
    });
}

math::Vec3 rotate(math::Quat rotation, math::Vec3 value) {
    const auto q = normalize(rotation);
    const math::Vec3 u{q.x, q.y, q.z};
    const float uv = (u.x * value.x) + (u.y * value.y) + (u.z * value.z);
    const float uu = u.lengthSquared();
    const math::Vec3 cross{
        (u.y * value.z) - (u.z * value.y),
        (u.z * value.x) - (u.x * value.z),
        (u.x * value.y) - (u.y * value.x),
    };
    return (u * (2.0F * uv)) + (value * ((q.w * q.w) - uu)) + (cross * (2.0F * q.w));
}

math::Vec3 lerp(math::Vec3 from, math::Vec3 to, float weight) {
    const float t = std::clamp(weight, 0.0F, 1.0F);
    return from + ((to - from) * t);
}

math::Quat slerp(math::Quat from, math::Quat to, float weight) {
    const float t = std::clamp(weight, 0.0F, 1.0F);
    from = normalize(from);
    to = normalize(to);

    float cosine = dot(from, to);
    if (cosine < 0.0F) {
        to = {-to.x, -to.y, -to.z, -to.w};
        cosine = -cosine;
    }

    if (cosine > 0.9995F) {
        return normalize({
            from.x + ((to.x - from.x) * t),
            from.y + ((to.y - from.y) * t),
            from.z + ((to.z - from.z) * t),
            from.w + ((to.w - from.w) * t),
        });
    }

    cosine = std::clamp(cosine, -1.0F, 1.0F);
    const float angle = std::acos(cosine);
    const float sine = std::sin(angle);
    if (std::abs(sine) <= 1.0e-6F) {
        return from;
    }

    const float fromWeight = std::sin((1.0F - t) * angle) / sine;
    const float toWeight = std::sin(t * angle) / sine;
    return normalize({
        (from.x * fromWeight) + (to.x * toWeight),
        (from.y * fromWeight) + (to.y * toWeight),
        (from.z * fromWeight) + (to.z * toWeight),
        (from.w * fromWeight) + (to.w * toWeight),
    });
}

Transform blend(const Transform& from, const Transform& to, float weight) {
    return {
        lerp(from.translation, to.translation, weight),
        slerp(from.rotation, to.rotation, weight),
        lerp(from.scale, to.scale, weight),
    };
}

Mat4 matrixFromTransform(const Transform& transform) {
    const auto q = normalize(transform.rotation);
    const float xx = q.x * q.x;
    const float yy = q.y * q.y;
    const float zz = q.z * q.z;
    const float xy = q.x * q.y;
    const float xz = q.x * q.z;
    const float yz = q.y * q.z;
    const float wx = q.w * q.x;
    const float wy = q.w * q.y;
    const float wz = q.w * q.z;

    Mat4 matrix{};
    matrix.at(0, 0) = (1.0F - (2.0F * (yy + zz))) * transform.scale.x;
    matrix.at(1, 0) = (2.0F * (xy + wz)) * transform.scale.x;
    matrix.at(2, 0) = (2.0F * (xz - wy)) * transform.scale.x;
    matrix.at(0, 1) = (2.0F * (xy - wz)) * transform.scale.y;
    matrix.at(1, 1) = (1.0F - (2.0F * (xx + zz))) * transform.scale.y;
    matrix.at(2, 1) = (2.0F * (yz + wx)) * transform.scale.y;
    matrix.at(0, 2) = (2.0F * (xz + wy)) * transform.scale.z;
    matrix.at(1, 2) = (2.0F * (yz - wx)) * transform.scale.z;
    matrix.at(2, 2) = (1.0F - (2.0F * (xx + yy))) * transform.scale.z;
    matrix.at(0, 3) = transform.translation.x;
    matrix.at(1, 3) = transform.translation.y;
    matrix.at(2, 3) = transform.translation.z;
    return matrix;
}

Mat4 multiply(const Mat4& lhs, const Mat4& rhs) {
    Mat4 result{};
    for (std::size_t row = 0; row < 4U; ++row) {
        for (std::size_t column = 0; column < 4U; ++column) {
            result.at(row, column) = 0.0F;
            for (std::size_t element = 0; element < 4U; ++element) {
                result.at(row, column) += lhs.at(row, element) * rhs.at(element, column);
            }
        }
    }
    return result;
}

math::Vec3 transformPoint(const Mat4& transform, math::Vec3 point) {
    return {
        (transform.at(0, 0) * point.x) + (transform.at(0, 1) * point.y) + (transform.at(0, 2) * point.z) +
            transform.at(0, 3),
        (transform.at(1, 0) * point.x) + (transform.at(1, 1) * point.y) + (transform.at(1, 2) * point.z) +
            transform.at(1, 3),
        (transform.at(2, 0) * point.x) + (transform.at(2, 1) * point.y) + (transform.at(2, 2) * point.z) +
            transform.at(2, 3),
    };
}

math::Vec3 transformDirection(const Mat4& transform, math::Vec3 direction) {
    return {
        (transform.at(0, 0) * direction.x) + (transform.at(0, 1) * direction.y) + (transform.at(0, 2) * direction.z),
        (transform.at(1, 0) * direction.x) + (transform.at(1, 1) * direction.y) + (transform.at(1, 2) * direction.z),
        (transform.at(2, 0) * direction.x) + (transform.at(2, 1) * direction.y) + (transform.at(2, 2) * direction.z),
    };
}

math::Vec3 matrixTranslation(const Mat4& transform) {
    return {transform.at(0, 3), transform.at(1, 3), transform.at(2, 3)};
}

} // namespace novacore::animation
