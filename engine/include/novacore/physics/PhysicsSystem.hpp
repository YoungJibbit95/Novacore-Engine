#pragma once

#include "novacore/math/Types.hpp"
#include "novacore/physics/CharacterController.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace novacore::physics {

struct PhysicsWorldStats final {
    std::uint32_t staticColliderCount = 0;
    std::uint32_t blockingColliderCount = 0;
    std::uint32_t triggerColliderCount = 0;
    std::uint32_t walkableColliderCount = 0;
    std::uint32_t wallRunColliderCount = 0;
    std::uint32_t slideColliderCount = 0;
    std::uint32_t kinematicColliderCount = 0;
};

struct PhysicsStepConfig final {
    float fixedDeltaSeconds = 1.0F / 60.0F;
    std::uint32_t maxSubsteps = 4;
};

struct SurfaceResponse final {
    float speedScale = 1.0F;
    float accelerationScale = 1.0F;
    float frictionScale = 1.0F;
    float traction01 = 1.0F;
    bool walkable = true;
    bool slideAssist = false;
    bool wallRunAssist = false;
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
    float airDrag = 0.10F;
    float capsuleSkinWidth = 0.003F;
    float crouchTransitionSpeed = 7.5F;
    float groundAdhesionSpeed = 2.5F;
    float coyoteTimeSeconds = 0.10F;
    float jumpBufferSeconds = 0.12F;
    float hardLandingSpeed = 10.0F;
    float landingRecoverySeconds = 0.18F;
    std::uint32_t maxDepenetrationIterations = 6;
    std::uint32_t maxSweepIterations = 6;
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
    float crouchFraction = 0.0F;
    math::Vec3 groundNormal{0.0F, 1.0F, 0.0F};
    math::Vec3 supportVelocity{};
    std::string supportColliderId;
    float airborneSeconds = 0.0F;
    float groundedSeconds = 0.0F;
    float timeSinceGrounded = 0.0F;
    float jumpBufferRemaining = 0.0F;
    float landingRecoveryRemaining = 0.0F;
    float lastImpactSpeed = 0.0F;
    std::uint64_t tick = 0;
};

enum class CharacterMotorEvent : std::uint32_t {
    None = 0,
    Jumped = 1U << 0U,
    Landed = 1U << 1U,
    HardLanded = 1U << 2U,
    Stepped = 1U << 3U,
    GroundSnapped = 1U << 4U,
    SupportChanged = 1U << 5U,
    SupportLost = 1U << 6U,
    CeilingHit = 1U << 7U,
    CrouchBlocked = 1U << 8U,
    SweepBlocked = 1U << 9U,
};

[[nodiscard]] constexpr CharacterMotorEvent operator|(CharacterMotorEvent lhs, CharacterMotorEvent rhs) {
    return static_cast<CharacterMotorEvent>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

constexpr CharacterMotorEvent& operator|=(CharacterMotorEvent& lhs, CharacterMotorEvent rhs) {
    lhs = lhs | rhs;
    return lhs;
}

[[nodiscard]] constexpr bool hasCharacterMotorEvent(CharacterMotorEvent events, CharacterMotorEvent event) {
    return (static_cast<std::uint32_t>(events) & static_cast<std::uint32_t>(event)) != 0U;
}

struct CharacterMotorTelemetry final {
    std::uint64_t tick = 0;
    CharacterMotorEvent events = CharacterMotorEvent::None;
    std::uint32_t contactCount = 0;
    std::uint32_t blockingContactCount = 0;
    std::uint32_t walkableContactCount = 0;
    std::uint32_t sweepIterations = 0;
    std::uint32_t depenetrationIterations = 0;
    float groundSlopeDegrees = 0.0F;
    float groundSnapDistance = 0.0F;
    float stepHeight = 0.0F;
    float impactSpeed = 0.0F;
    math::Vec3 supportDisplacement{};
    std::uint64_t contactHash = 0;
    std::uint64_t stateHash = 0;
};

struct CharacterMotorStepResult final {
    CharacterMotorState state{};
    CharacterResolveResult resolve{};
    CharacterSweepResult sweep{};
    math::Vec3 desiredDisplacement{};
    math::Vec3 supportVelocity{};
    math::Vec3 supportDisplacement{};
    SurfaceResponse groundSurface{};
    std::string supportColliderId;
    bool jumped = false;
    bool swept = false;
    bool carriedBySupport = false;
    bool touchedSlideSurface = false;
    bool touchedWallRunSurface = false;
    bool crouchBlocked = false;
    bool landed = false;
    bool hardLanded = false;
    bool supportChanged = false;
    bool supportLost = false;
    float impactSpeed = 0.0F;
    CharacterMotorTelemetry telemetry{};
};

[[nodiscard]] PhysicsWorldStats summarizePhysicsWorld(const PhysicsWorld& world);
[[nodiscard]] SurfaceResponse surfaceResponseFor(SurfaceKind kind);
[[nodiscard]] math::Vec3 normalizeHorizontal(math::Vec3 value);
[[nodiscard]] math::Vec3 projectVelocityOnPlane(math::Vec3 velocity, math::Vec3 normal);
[[nodiscard]] std::uint64_t hashCharacterMotorState(const CharacterMotorState& state);
[[nodiscard]] CharacterMotorStepResult stepCharacterMotor(
    const PhysicsWorld& world,
    CharacterMotorState state,
    const CharacterMotorInput& input,
    const CharacterMotorConfig& config,
    float deltaSeconds);
[[nodiscard]] std::vector<StaticCollider> makeDefaultPhysicsSandboxColliders();

} // namespace novacore::physics
