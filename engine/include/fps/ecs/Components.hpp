#pragma once

#include <array>
#include <string>

namespace riftline::ecs {

struct Vec3 final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct Quat final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;
};

struct TransformComponent final {
    Vec3 position{};
    Quat rotation{};
    Vec3 scale{1.0F, 1.0F, 1.0F};
};

struct NameComponent final {
    std::string name;
};

struct CameraComponent final {
    float verticalFovDegrees = 74.0F;
    float nearPlane = 0.03F;
    float farPlane = 1000.0F;
};

} // namespace riftline::ecs

