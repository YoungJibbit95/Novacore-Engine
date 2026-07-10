#include "novacore/physics/PhysicsSystem.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>

namespace novacore::physics {

namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void hashByte(std::uint64_t& hash, std::uint8_t value) {
    hash ^= value;
    hash *= kFnvPrime;
}

void hashU64(std::uint64_t& hash, std::uint64_t value) {
    for (std::uint32_t shift = 0; shift < 64U; shift += 8U) {
        hashByte(hash, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void hashFloat(std::uint64_t& hash, float value) {
    constexpr double kPrecision = 10000.0;
    const auto quantized = std::isfinite(value)
        ? static_cast<std::int64_t>(std::llround(static_cast<double>(value) * kPrecision))
        : std::numeric_limits<std::int64_t>::min();
    hashU64(hash, static_cast<std::uint64_t>(quantized));
}

void hashVec3(std::uint64_t& hash, math::Vec3 value) {
    hashFloat(hash, value.x);
    hashFloat(hash, value.y);
    hashFloat(hash, value.z);
}

void hashString(std::uint64_t& hash, std::string_view value) {
    hashU64(hash, value.size());
    for (const char character : value) {
        hashByte(hash, static_cast<std::uint8_t>(character));
    }
}

[[nodiscard]] float clamp01(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] float lengthHorizontal(math::Vec3 value) {
    return std::sqrt((value.x * value.x) + (value.z * value.z));
}

[[nodiscard]] float dot(math::Vec3 lhs, math::Vec3 rhs) {
    return (lhs.x * rhs.x) + (lhs.y * rhs.y) + (lhs.z * rhs.z);
}

[[nodiscard]] float dotHorizontal(math::Vec3 lhs, math::Vec3 rhs) {
    return (lhs.x * rhs.x) + (lhs.z * rhs.z);
}

[[nodiscard]] bool hasKinematicVelocity(math::Vec3 velocity) {
    return velocity.lengthSquared() > 0.000001F;
}

[[nodiscard]] math::Vec3 horizontalRightFromForward(math::Vec3 forward) {
    const auto normalized = normalizeHorizontal(forward);
    if (normalized.lengthSquared() <= 0.000001F) {
        return {1.0F, 0.0F, 0.0F};
    }
    return {normalized.z, 0.0F, -normalized.x};
}

[[nodiscard]] math::Vec3 approachVec3(math::Vec3 current, math::Vec3 target, float maxDelta) {
    const auto delta = target - current;
    const float length = std::sqrt(delta.lengthSquared());
    if (length <= maxDelta || length <= 0.000001F) {
        return target;
    }
    return current + (delta * (maxDelta / length));
}

[[nodiscard]] float approachFloat(float current, float target, float maxDelta) {
    if (current < target) {
        return std::min(target, current + std::max(0.0F, maxDelta));
    }
    return std::max(target, current - std::max(0.0F, maxDelta));
}

[[nodiscard]] math::Vec3 applyFriction(math::Vec3 velocity, float friction, float deltaSeconds) {
    const float horizontalSpeed = lengthHorizontal(velocity);
    if (horizontalSpeed <= 0.0001F) {
        velocity.x = 0.0F;
        velocity.z = 0.0F;
        return velocity;
    }

    const float nextSpeed = std::max(0.0F, horizontalSpeed - (std::max(0.0F, friction) * deltaSeconds));
    const float scale = nextSpeed / horizontalSpeed;
    velocity.x *= scale;
    velocity.z *= scale;
    return velocity;
}

[[nodiscard]] math::Vec3 desiredMoveWorld(const CharacterMotorInput& input) {
    const auto forward = normalizeHorizontal(input.forward);
    const auto right = horizontalRightFromForward(input.forward);
    const auto raw = (right * input.move.x) + (forward * input.move.z);
    return normalizeHorizontal(raw);
}

[[nodiscard]] CharacterQuery makeResolveQuery(
    const CharacterMotorState& state,
    const CharacterMotorConfig& config,
    bool enableSnap,
    bool enableStep) {
    CharacterQuery query{};
    query.position = state.position;
    query.radius = config.radius;
    query.height = state.capsuleHeight;
    query.maxStepHeight = config.maxStepHeight;
    query.snapDownDistance = config.snapDownDistance;
    query.walkableSlopeCosine = config.walkableSlopeCosine;
    query.wallProbeDistance = config.wallProbeDistance;
    query.enableGroundSnap = enableSnap;
    query.enableStepUp = enableStep;
    query.skinWidth = config.capsuleSkinWidth;
    query.maxDepenetrationIterations = static_cast<int>(config.maxDepenetrationIterations);
    return query;
}

[[nodiscard]] CharacterSweepQuery makeSweepQuery(
    const CharacterMotorState& state,
    const CharacterMotorConfig& config,
    math::Vec3 displacement,
    float deltaSeconds) {
    CharacterSweepQuery query{};
    query.startPosition = state.position;
    query.desiredDisplacement = displacement;
    query.radius = config.radius;
    query.height = state.capsuleHeight;
    query.maxStepHeight = config.maxStepHeight;
    query.snapDownDistance = config.snapDownDistance;
    query.walkableSlopeCosine = config.walkableSlopeCosine;
    query.wallProbeDistance = config.wallProbeDistance;
    query.maxIterations = static_cast<int>(config.maxSweepIterations);
    query.enableGroundSnap = displacement.y <= 0.02F;
    query.enableStepUp = state.grounded && displacement.y <= 0.02F;
    query.skinWidth = config.capsuleSkinWidth;
    query.deltaSeconds = deltaSeconds;
    query.maxDepenetrationIterations = static_cast<int>(config.maxDepenetrationIterations);
    return query;
}

[[nodiscard]] float slopeDegrees(math::Vec3 normal) {
    const float cosine = std::clamp(normal.y, -1.0F, 1.0F);
    return std::acos(cosine) * (180.0F / 3.14159265358979323846F);
}

void addEvent(CharacterMotorTelemetry& telemetry, CharacterMotorEvent event, bool condition) {
    if (condition) {
        telemetry.events |= event;
    }
}

} // namespace

PhysicsWorldStats summarizePhysicsWorld(const PhysicsWorld& world) {
    PhysicsWorldStats stats{};
    stats.staticColliderCount = static_cast<std::uint32_t>(world.colliderCount());
    for (const auto& collider : world.staticColliders()) {
        if (collider.blocksMovement) {
            ++stats.blockingColliderCount;
        } else {
            ++stats.triggerColliderCount;
        }
        if (collider.kind == SurfaceKind::Floor ||
            collider.kind == SurfaceKind::Ramp ||
            collider.kind == SurfaceKind::Slide ||
            collider.kind == SurfaceKind::Cover ||
            collider.kind == SurfaceKind::Ledge) {
            ++stats.walkableColliderCount;
        }
        if (collider.kind == SurfaceKind::WallRun) {
            ++stats.wallRunColliderCount;
        }
        if (collider.kind == SurfaceKind::Slide) {
            ++stats.slideColliderCount;
        }
        if (hasKinematicVelocity(collider.velocity)) {
            ++stats.kinematicColliderCount;
        }
    }
    return stats;
}

SurfaceResponse surfaceResponseFor(SurfaceKind kind) {
    switch (kind) {
    case SurfaceKind::Floor:
        return {};
    case SurfaceKind::Ramp:
        return {0.96F, 0.92F, 1.08F, 0.92F, true, false, false};
    case SurfaceKind::Cover:
    case SurfaceKind::Ledge:
        return {0.90F, 0.86F, 1.20F, 0.86F, true, false, false};
    case SurfaceKind::Slide:
        return {1.18F, 0.72F, 0.34F, 0.38F, true, true, false};
    case SurfaceKind::WallRun:
        return {1.05F, 0.88F, 0.80F, 0.64F, false, false, true};
    case SurfaceKind::Wall:
        return {0.0F, 0.0F, 1.0F, 0.0F, false, false, false};
    case SurfaceKind::Trigger:
        return {1.0F, 1.0F, 1.0F, 1.0F, false, false, false};
    }
    return {};
}

math::Vec3 normalizeHorizontal(math::Vec3 value) {
    value.y = 0.0F;
    const float length = lengthHorizontal(value);
    if (length <= 0.000001F) {
        return {};
    }
    return {value.x / length, 0.0F, value.z / length};
}

math::Vec3 projectVelocityOnPlane(math::Vec3 velocity, math::Vec3 normal) {
    const float normalLength = std::sqrt(normal.lengthSquared());
    if (normalLength <= 0.000001F) {
        return velocity;
    }
    const auto unitNormal = normal * (1.0F / normalLength);
    return velocity - (unitNormal * dot(velocity, unitNormal));
}

std::uint64_t hashCharacterMotorState(const CharacterMotorState& state) {
    std::uint64_t hash = kFnvOffset;
    hashVec3(hash, state.position);
    hashVec3(hash, state.velocity);
    hashFloat(hash, state.capsuleHeight);
    hashU64(hash, state.grounded ? 1U : 0U);
    hashU64(hash, state.crouched ? 1U : 0U);
    hashU64(hash, state.nearWallRunSurface ? 1U : 0U);
    hashFloat(hash, state.crouchFraction);
    hashVec3(hash, state.groundNormal);
    hashVec3(hash, state.supportVelocity);
    hashString(hash, state.supportColliderId);
    hashFloat(hash, state.airborneSeconds);
    hashFloat(hash, state.groundedSeconds);
    hashFloat(hash, state.timeSinceGrounded);
    hashFloat(hash, state.jumpBufferRemaining);
    hashFloat(hash, state.landingRecoveryRemaining);
    hashFloat(hash, state.lastImpactSpeed);
    hashU64(hash, state.tick);
    return hash;
}

CharacterMotorStepResult stepCharacterMotor(
    const PhysicsWorld& world,
    CharacterMotorState state,
    const CharacterMotorInput& input,
    const CharacterMotorConfig& config,
    float deltaSeconds) {
    const float dt = std::clamp(deltaSeconds, 0.0F, 0.10F);
    CharacterMotorStepResult result{};
    const bool wasGrounded = state.grounded;
    const std::string previousSupportId = state.supportColliderId;
    const float incomingVerticalSpeed = state.velocity.y;
    state.jumpBufferRemaining = input.jumpPressed
        ? std::max(0.0F, config.jumpBufferSeconds)
        : std::max(0.0F, state.jumpBufferRemaining - dt);
    state.landingRecoveryRemaining = std::max(0.0F, state.landingRecoveryRemaining - dt);

    const float standingHeight = std::max(config.radius * 2.0F, config.standingHeight);
    const float crouchedHeight = std::clamp(config.crouchedHeight, config.radius * 2.0F, standingHeight);
    const float crouchTarget = input.crouchHeld ? 1.0F : 0.0F;
    state.crouchFraction = approachFloat(
        clamp01(state.crouchFraction),
        crouchTarget,
        std::max(0.01F, config.crouchTransitionSpeed) * dt);
    state.crouched = state.crouchFraction > 0.001F;
    state.capsuleHeight = standingHeight + ((crouchedHeight - standingHeight) * state.crouchFraction);

    const bool allowInitialSnap = state.grounded || state.velocity.y <= 0.0F;
    const bool allowInitialStep = state.grounded || (state.position.y <= config.snapDownDistance && state.velocity.y <= 0.0F);
    const auto preResolve = world.resolveCharacter(makeResolveQuery(state, config, allowInitialSnap, allowInitialStep));
    state.position = preResolve.position;
    state.grounded = preResolve.grounded;
    state.nearWallRunSurface = preResolve.nearWallRunSurface;
    state.groundNormal = preResolve.groundNormal;
    state.supportColliderId = preResolve.groundColliderId;
    state.supportVelocity = preResolve.groundVelocity;
    result.groundSurface = state.grounded ? surfaceResponseFor(preResolve.groundKind) : SurfaceResponse{};
    result.supportVelocity = state.grounded ? preResolve.groundVelocity : math::Vec3{};
    result.supportColliderId = state.grounded ? preResolve.groundColliderId : std::string{};
    result.carriedBySupport = hasKinematicVelocity(result.supportVelocity);
    result.touchedSlideSurface = preResolve.nearSlideSurface || result.groundSurface.slideAssist;
    result.touchedWallRunSurface = preResolve.nearWallRunSurface || result.groundSurface.wallRunAssist;

    if (state.grounded) {
        if (state.velocity.y < 0.0F) {
            state.velocity.y = 0.0F;
        }
        state.velocity = projectVelocityOnPlane(state.velocity, preResolve.groundNormal);
    }

    const bool hasGroundHistory = wasGrounded || state.grounded || state.groundedSeconds > 0.0F;
    const bool withinCoyoteWindow = hasGroundHistory &&
        state.timeSinceGrounded <= std::max(0.0F, config.coyoteTimeSeconds);
    if (state.jumpBufferRemaining > 0.0F && (state.grounded || withinCoyoteWindow)) {
        if (result.carriedBySupport) {
            state.velocity.x += result.supportVelocity.x;
            state.velocity.z += result.supportVelocity.z;
        }
        state.velocity.y = std::max(0.0F, config.jumpSpeed) +
            (result.carriedBySupport ? std::max(0.0F, result.supportVelocity.y) : 0.0F);
        state.grounded = false;
        state.supportColliderId.clear();
        state.supportVelocity = {};
        state.jumpBufferRemaining = 0.0F;
        state.timeSinceGrounded = std::max(0.0F, config.coyoteTimeSeconds) + dt;
        result.jumped = true;
    }

    const auto move = desiredMoveWorld(input);
    const float inputStrength = std::min(1.0F, std::sqrt(dotHorizontal(input.move, input.move)));
    float targetSpeed = input.crouchHeld
        ? config.crouchSpeed
        : input.sprintHeld ? config.sprintSpeed : config.groundSpeed;
    if (state.landingRecoveryRemaining > 0.0F && config.landingRecoverySeconds > 0.0F) {
        const float recovery = clamp01(state.landingRecoveryRemaining / config.landingRecoverySeconds);
        targetSpeed *= 1.0F - (0.25F * recovery);
    }
    const float surfaceSpeedScale = state.grounded ? result.groundSurface.speedScale : 1.0F;
    const auto desiredHorizontal = move * (targetSpeed * surfaceSpeedScale * inputStrength);
    const auto currentHorizontal = math::Vec3{state.velocity.x, 0.0F, state.velocity.z};
    float acceleration = state.grounded
        ? config.groundAcceleration * result.groundSurface.accelerationScale
        : config.airAcceleration;
    if (state.grounded && state.landingRecoveryRemaining > 0.0F && config.landingRecoverySeconds > 0.0F) {
        const float recovery = clamp01(state.landingRecoveryRemaining / config.landingRecoverySeconds);
        acceleration *= 1.0F - (0.40F * recovery);
    }
    const float braking = state.grounded
        ? config.brakingDeceleration * result.groundSurface.frictionScale
        : config.airDrag;
    if (state.grounded && !result.jumped) {
        const auto currentTangent = projectVelocityOnPlane(state.velocity, preResolve.groundNormal);
        const auto desiredTangent = projectVelocityOnPlane(desiredHorizontal, preResolve.groundNormal);
        state.velocity = inputStrength > 0.001F
            ? approachVec3(currentTangent, desiredTangent, acceleration * dt)
            : projectVelocityOnPlane(applyFriction(currentTangent, braking, dt), preResolve.groundNormal);
    } else {
        const auto nextHorizontal = inputStrength > 0.001F
            ? approachVec3(currentHorizontal, desiredHorizontal, acceleration * dt)
            : applyFriction(currentHorizontal, braking, dt);
        state.velocity.x = nextHorizontal.x;
        state.velocity.z = nextHorizontal.z;
    }

    if (state.grounded && result.groundSurface.slideAssist && preResolve.groundNormal.y < 0.99F) {
        const auto downhill = normalizeHorizontal(projectVelocityOnPlane({0.0F, -1.0F, 0.0F}, preResolve.groundNormal));
        state.velocity = state.velocity + (downhill * (config.gravity * dt * (1.0F - result.groundSurface.traction01)));
    }

    if (!state.grounded) {
        state.velocity.y = std::max(
            -std::max(1.0F, config.terminalFallSpeed),
            state.velocity.y - (std::max(0.0F, config.gravity) * dt));
    }

    const float collisionVerticalSpeed = state.velocity.y;

    result.supportDisplacement = (!result.jumped && state.grounded)
        ? result.supportVelocity * dt
        : math::Vec3{};
    const auto adhesionDisplacement = (!result.jumped && state.grounded)
        ? preResolve.groundNormal * (-std::max(0.0F, config.groundAdhesionSpeed) * dt)
        : math::Vec3{};
    result.desiredDisplacement = (state.velocity * dt) + result.supportDisplacement + adhesionDisplacement;
    if (result.desiredDisplacement.lengthSquared() > 0.0000001F) {
        result.swept = true;
        result.sweep = world.sweepCharacter(makeSweepQuery(state, config, result.desiredDisplacement, dt));
        state.position = result.sweep.resolve.position;
        if (result.sweep.hit) {
            for (const auto& contact : result.sweep.sweepContacts) {
                const float intoSurface = dot(state.velocity, contact.normal);
                if (intoSurface < 0.0F) {
                    state.velocity = projectVelocityOnPlane(state.velocity, contact.normal);
                }
            }
        }
    }

    const bool allowFinalGroundSnap = !result.jumped && state.velocity.y <= 0.02F;
    const bool allowFinalStep = state.grounded && !result.jumped && state.velocity.y <= 0.02F;
    result.resolve = world.resolveCharacter(makeResolveQuery(
        state,
        config,
        allowFinalGroundSnap,
        allowFinalStep));
    state.position = result.resolve.position;
    state.grounded = result.resolve.grounded;
    state.nearWallRunSurface = result.resolve.nearWallRunSurface;
    state.groundNormal = result.resolve.groundNormal;
    state.supportColliderId = result.resolve.groundColliderId;
    state.supportVelocity = result.resolve.groundVelocity;
    result.groundSurface = state.grounded ? surfaceResponseFor(result.resolve.groundKind) : result.groundSurface;
    if (!result.jumped && state.grounded) {
        result.supportVelocity = result.resolve.groundVelocity;
        result.supportColliderId = result.resolve.groundColliderId;
        result.carriedBySupport = hasKinematicVelocity(result.supportVelocity);
    }
    result.touchedSlideSurface = result.touchedSlideSurface || result.resolve.nearSlideSurface || result.groundSurface.slideAssist;
    result.touchedWallRunSurface = result.touchedWallRunSurface || result.resolve.nearWallRunSurface || result.groundSurface.wallRunAssist;
    if (state.grounded && !result.jumped) {
        state.velocity = projectVelocityOnPlane(state.velocity, result.resolve.groundNormal);
    }

    const bool hadAirborneMotion = state.airborneSeconds > dt ||
        incomingVerticalSpeed < -0.01F || collisionVerticalSpeed < -0.01F;
    result.landed = !wasGrounded && state.grounded && !result.jumped && hadAirborneMotion;
    if (result.landed) {
        const float relativeVerticalSpeed = collisionVerticalSpeed - result.resolve.groundVelocity.y;
        result.impactSpeed = std::max(0.0F, -relativeVerticalSpeed);
        if (result.impactSpeed <= 0.01F) {
            result.impactSpeed = std::max(0.0F, -incomingVerticalSpeed);
        }
        result.hardLanded = result.impactSpeed >= std::max(0.0F, config.hardLandingSpeed);
        state.lastImpactSpeed = result.impactSpeed;
        if (result.hardLanded) {
            state.landingRecoveryRemaining = std::max(0.0F, config.landingRecoverySeconds);
        }
    }

    if (state.grounded && !result.jumped) {
        state.groundedSeconds = wasGrounded ? state.groundedSeconds + dt : dt;
        state.airborneSeconds = 0.0F;
        state.timeSinceGrounded = 0.0F;
    } else {
        state.airborneSeconds = wasGrounded ? dt : state.airborneSeconds + dt;
        state.groundedSeconds = 0.0F;
        state.timeSinceGrounded = wasGrounded ? dt : state.timeSinceGrounded + dt;
    }

    const std::string currentSupportId = state.grounded ? state.supportColliderId : std::string{};
    result.supportChanged = !previousSupportId.empty() && !currentSupportId.empty() &&
        previousSupportId != currentSupportId;
    result.supportLost = !previousSupportId.empty() && currentSupportId.empty() && !result.jumped;
    if (!state.grounded) {
        state.supportColliderId.clear();
        state.supportVelocity = {};
    }

    ++state.tick;
    result.state = state;

    auto& telemetry = result.telemetry;
    telemetry.tick = state.tick;
    telemetry.contactCount = static_cast<std::uint32_t>(result.resolve.contacts.size());
    telemetry.blockingContactCount = static_cast<std::uint32_t>(result.resolve.blockingContactCount);
    telemetry.walkableContactCount = static_cast<std::uint32_t>(result.resolve.walkableContactCount);
    telemetry.sweepIterations = static_cast<std::uint32_t>(result.sweep.iterationCount);
    telemetry.depenetrationIterations = static_cast<std::uint32_t>(
        preResolve.depenetrationIterations + result.resolve.depenetrationIterations);
    telemetry.groundSlopeDegrees = state.grounded ? slopeDegrees(state.groundNormal) : 0.0F;
    telemetry.groundSnapDistance = std::max(preResolve.groundSnapDistance, result.resolve.groundSnapDistance);
    telemetry.stepHeight = std::max({preResolve.stepHeight, result.resolve.stepHeight, result.sweep.stepHeight});
    telemetry.impactSpeed = result.impactSpeed;
    telemetry.supportDisplacement = result.supportDisplacement;
    telemetry.contactHash = result.resolve.contactHash ^ result.sweep.contactHash;
    telemetry.stateHash = hashCharacterMotorState(state);
    addEvent(telemetry, CharacterMotorEvent::Jumped, result.jumped);
    addEvent(telemetry, CharacterMotorEvent::Landed, result.landed);
    addEvent(telemetry, CharacterMotorEvent::HardLanded, result.hardLanded);
    addEvent(telemetry, CharacterMotorEvent::Stepped, result.resolve.stepped || result.sweep.stepped);
    addEvent(telemetry, CharacterMotorEvent::GroundSnapped, telemetry.groundSnapDistance > 0.0001F);
    addEvent(telemetry, CharacterMotorEvent::SupportChanged, result.supportChanged);
    addEvent(telemetry, CharacterMotorEvent::SupportLost, result.supportLost);
    addEvent(telemetry, CharacterMotorEvent::CeilingHit, result.sweep.ceilingHit);
    addEvent(telemetry, CharacterMotorEvent::CrouchBlocked, result.crouchBlocked);
    addEvent(telemetry, CharacterMotorEvent::SweepBlocked, result.sweep.hit);

    return result;
}

std::vector<StaticCollider> makeDefaultPhysicsSandboxColliders() {
    return {
        StaticCollider{
            "sandbox_low_step",
            SurfaceKind::Cover,
            {0.0F, 0.18F, 2.25F},
            {1.10F, 0.18F, 0.85F},
            true,
            RampDirection::None,
            0.36F,
        },
        StaticCollider{
            "sandbox_slide_ramp",
            SurfaceKind::Slide,
            {3.0F, 0.45F, 1.0F},
            {1.2F, 0.45F, 2.0F},
            true,
            RampDirection::PositiveZ,
        },
        StaticCollider{
            "sandbox_wallrun_panel",
            SurfaceKind::WallRun,
            {-4.0F, 1.25F, 0.6F},
            {0.15F, 1.25F, 2.80F},
            true,
        },
        StaticCollider{
            "sandbox_mantle_ledge",
            SurfaceKind::Ledge,
            {6.0F, 0.70F, 0.0F},
            {1.15F, 0.70F, 1.10F},
            true,
        },
        StaticCollider{
            "sandbox_moving_platform",
            SurfaceKind::Cover,
            {-2.25F, 0.16F, 4.60F},
            {0.90F, 0.16F, 0.75F},
            true,
            RampDirection::None,
            0.32F,
            {0.85F, 0.0F, 0.0F},
        },
        StaticCollider{
            "sandbox_trigger_zone",
            SurfaceKind::Trigger,
            {0.0F, 0.8F, 6.0F},
            {1.2F, 0.8F, 0.8F},
            false,
        },
    };
}

} // namespace novacore::physics
