#include "novacore/physics/PhysicsSystem.hpp"

#include <algorithm>
#include <cmath>

namespace novacore::physics {

namespace {

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
    return query;
}

[[nodiscard]] CharacterSweepQuery makeSweepQuery(
    const CharacterMotorState& state,
    const CharacterMotorConfig& config,
    math::Vec3 displacement) {
    CharacterSweepQuery query{};
    query.startPosition = state.position;
    query.desiredDisplacement = displacement;
    query.radius = config.radius;
    query.height = state.capsuleHeight;
    query.maxStepHeight = config.maxStepHeight;
    query.snapDownDistance = config.snapDownDistance;
    query.walkableSlopeCosine = config.walkableSlopeCosine;
    query.wallProbeDistance = config.wallProbeDistance;
    query.maxIterations = 5;
    query.enableGroundSnap = displacement.y <= 0.02F;
    query.enableStepUp = true;
    return query;
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
    }
    return stats;
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

CharacterMotorStepResult stepCharacterMotor(
    const PhysicsWorld& world,
    CharacterMotorState state,
    const CharacterMotorInput& input,
    const CharacterMotorConfig& config,
    float deltaSeconds) {
    const float dt = std::clamp(deltaSeconds, 0.0F, 0.10F);
    CharacterMotorStepResult result{};
    state.crouched = input.crouchHeld;
    state.capsuleHeight = input.crouchHeld
        ? std::clamp(config.crouchedHeight, config.radius * 2.0F, config.standingHeight)
        : std::max(config.radius * 2.0F, config.standingHeight);

    const auto preResolve = world.resolveCharacter(makeResolveQuery(state, config, true, true));
    state.position = preResolve.position;
    state.grounded = preResolve.grounded;
    state.nearWallRunSurface = preResolve.nearWallRunSurface;

    if (state.grounded && state.velocity.y < 0.0F) {
        state.velocity.y = 0.0F;
        state.velocity = projectVelocityOnPlane(state.velocity, preResolve.groundNormal);
    }

    if (input.jumpPressed && state.grounded) {
        state.velocity.y = std::max(0.0F, config.jumpSpeed);
        state.grounded = false;
        result.jumped = true;
    }

    const auto move = desiredMoveWorld(input);
    const float inputStrength = std::min(1.0F, std::sqrt(dotHorizontal(input.move, input.move)));
    const float targetSpeed = input.crouchHeld
        ? config.crouchSpeed
        : input.sprintHeld ? config.sprintSpeed : config.groundSpeed;
    const auto desiredHorizontal = move * (targetSpeed * inputStrength);
    const auto currentHorizontal = math::Vec3{state.velocity.x, 0.0F, state.velocity.z};
    const float acceleration = state.grounded ? config.groundAcceleration : config.airAcceleration;
    const auto nextHorizontal = inputStrength > 0.001F
        ? approachVec3(currentHorizontal, desiredHorizontal, acceleration * dt)
        : applyFriction(currentHorizontal, state.grounded ? config.brakingDeceleration : config.groundFriction, dt);
    state.velocity.x = nextHorizontal.x;
    state.velocity.z = nextHorizontal.z;

    if (!state.grounded) {
        state.velocity.y = std::max(
            -std::max(1.0F, config.terminalFallSpeed),
            state.velocity.y - (std::max(0.0F, config.gravity) * dt));
    }

    result.desiredDisplacement = state.velocity * dt;
    if (result.desiredDisplacement.lengthSquared() > 0.0000001F) {
        result.swept = true;
        result.sweep = world.sweepCharacter(makeSweepQuery(state, config, result.desiredDisplacement));
        state.position = result.sweep.resolve.position;
        if (result.sweep.hit) {
            state.velocity = projectVelocityOnPlane(state.velocity, result.sweep.hitNormal);
            if (result.sweep.hitNormal.y > config.walkableSlopeCosine && state.velocity.y < 0.0F) {
                state.velocity.y = 0.0F;
            }
        }
    }

    result.resolve = world.resolveCharacter(makeResolveQuery(state, config, !result.jumped, true));
    state.position = result.resolve.position;
    state.grounded = result.resolve.grounded;
    state.nearWallRunSurface = result.resolve.nearWallRunSurface;
    if (state.grounded && state.velocity.y < 0.0F) {
        state.velocity.y = 0.0F;
    }
    ++state.tick;
    result.state = state;
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
            "sandbox_trigger_zone",
            SurfaceKind::Trigger,
            {0.0F, 0.8F, 6.0F},
            {1.2F, 0.8F, 0.8F},
            false,
        },
    };
}

} // namespace novacore::physics
