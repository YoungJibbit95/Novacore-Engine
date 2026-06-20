#pragma once

#include "novacore/math/Types.hpp"
#include "novacore/physics/CharacterController.hpp"

#include <cstdint>
#include <vector>

namespace novacore::physics {

struct PhysicsWorldStats final {
    std::uint32_t staticColliderCount = 0;
    std::uint32_t blockingColliderCount = 0;
    std::uint32_t triggerColliderCount = 0;
    std::uint32_t walkableColliderCount = 0;
    std::uint32_t wallRunColliderCount = 0;
    std::uint32_t slideColliderCount = 0;
};

struct PhysicsStepConfig final {
    float fixedDeltaSeconds = 1.0F / 60.0F;
    std::uint32_t maxSubsteps = 4;
};

struct CharacterMotorConfig final {
    float radius = 0.42F;
    float standingHeight = 1.80F;
    float crouchedHeight = 1.20F;
    float maxStepHeight = 0.42F;
    float snapDownDistance = 0.36F;
    float walkableSlopeCosine = 0.68F;
    float wallProbeDistance = 0.45F;
    float groundSpeed = 7.2F;
    float sprintSpeed = 9.4F;
    float crouchSpeed = 3.4F;
    float groundAcceleration = 38.0F;
    float airAcceleration = 10.5F;
    float brakingDeceleration = 28.0F;
    float gravity = 24.0F;
    float jumpSpeed = 7.25F;
    float terminalFallSpeed = 48.0F;
    float groundFriction = 8.5F;
};

struct CharacterMotorInput final {
    math::Vec3 move{};
    math::Vec3 forward{0.0F, 0.0F, 1.0F};
    bool jumpPressed = false;
    bool sprintHeld = false;
    bool crouchHeld = false;
};

struct CharacterMotorState final {
    math::Vec3 position{};
    math::Vec3 velocity{};
    float capsuleHeight = 1.80F;
    bool grounded = false;
    bool crouched = false;
    bool nearWallRunSurface = false;
    std::uint64_t tick = 0;
};

struct CharacterMotorStepResult final {
    CharacterMotorState state{};
    CharacterResolveResult resolve{};
    CharacterSweepResult sweep{};
    math::Vec3 desiredDisplacement{};
    bool jumped = false;
    bool swept = false;
};

[[nodiscard]] PhysicsWorldStats summarizePhysicsWorld(const PhysicsWorld& world);
[[nodiscard]] math::Vec3 normalizeHorizontal(math::Vec3 value);
[[nodiscard]] math::Vec3 projectVelocityOnPlane(math::Vec3 velocity, math::Vec3 normal);
[[nodiscard]] CharacterMotorStepResult stepCharacterMotor(
    const PhysicsWorld& world,
    CharacterMotorState state,
    const CharacterMotorInput& input,
    const CharacterMotorConfig& config,
    float deltaSeconds);
[[nodiscard]] std::vector<StaticCollider> makeDefaultPhysicsSandboxColliders();

} // namespace novacore::physics
