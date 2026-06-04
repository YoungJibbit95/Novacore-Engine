#pragma once

#include <cmath>

namespace novacore::math {

struct Vec2 final {
    float x = 0.0F;
    float y = 0.0F;

    [[nodiscard]] constexpr float lengthSquared() const {
        return (x * x) + (y * y);
    }
};

struct Vec3 final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    [[nodiscard]] constexpr float lengthSquared() const {
        return (x * x) + (y * y) + (z * z);
    }
};

struct Quat final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;
};

[[nodiscard]] constexpr Vec3 operator+(Vec3 lhs, Vec3 rhs) {
    return Vec3{lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

[[nodiscard]] constexpr Vec3 operator-(Vec3 lhs, Vec3 rhs) {
    return Vec3{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

[[nodiscard]] constexpr Vec3 operator*(Vec3 value, float scalar) {
    return Vec3{value.x * scalar, value.y * scalar, value.z * scalar};
}

[[nodiscard]] inline float length(Vec3 value) {
    return std::sqrt(value.lengthSquared());
}

} // namespace novacore::math

