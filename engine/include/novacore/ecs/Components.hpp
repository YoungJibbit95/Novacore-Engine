#pragma once

#include "novacore/math/Types.hpp"

#include <string>

namespace novacore::ecs {

struct TransformComponent final {
    math::Vec3 position{};
    math::Quat rotation{};
    math::Vec3 scale{1.0F, 1.0F, 1.0F};
};

struct NameComponent final {
    std::string name;
};

struct CameraComponent final {
    float verticalFovDegrees = 74.0F;
    float nearPlane = 0.03F;
    float farPlane = 1000.0F;
};

} // namespace novacore::ecs







