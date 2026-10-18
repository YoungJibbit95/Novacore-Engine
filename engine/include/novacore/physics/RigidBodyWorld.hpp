#pragma once

#include "novacore/math/Types.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace novacore::physics {

class PhysicsWorld;

enum class RigidBodyShape { Box, Sphere, Capsule };
enum class RigidBodyMotion { Static, Kinematic, Dynamic };

// Opaque world ownership plus engine generation; never serialize a solver ID.
struct RigidBodyHandle final {
    std::uint64_t world = 0;
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
    [[nodiscard]] bool valid() const { return world != 0 && generation != 0; }
    bool operator==(const RigidBodyHandle&) const = default;
};

struct RigidBodyFilter final {
    std::uint8_t layer = 1; // 0..7; canonical static colliders use layer 0.
    std::uint8_t mask = 0xff; // Pair must pass BOTH masks.
};

struct RigidBodyDesc final {
    std::string id; // Stable application ID, unique within this world.
    RigidBodyShape shape = RigidBodyShape::Box;
    RigidBodyMotion motion = RigidBodyMotion::Dynamic;
    math::Vec3 position{}; // Shape origin, metres; +Y up, no render transform.
    math::Quat rotation{}; // Unit quaternion; oriented boxes need no axis repair.
    math::Vec3 halfExtents{0.25F, 0.25F, 0.25F};
    float radius = 0.25F;
    float capsuleHalfHeight = 0.50F; // Half of cylindrical section, excluding caps.
    float massKg = 5.0F;
    math::Vec3 linearVelocity{};
    math::Vec3 angularVelocity{}; // Radians/second, world space.
    float friction = 0.6F;
    float restitution = 0.1F;
    float linearDamping = 0.05F;
    float angularDamping = 0.05F;
    bool continuousCollision = true;
    bool allowSleeping = true;
    RigidBodyFilter filter{};
};

struct RigidBodyWorldConfig final {
    float fixedDeltaSeconds = 1.0F / 60.0F;
    math::Vec3 gravity{0.0F, -9.81F, 0.0F};
    std::uint32_t maxBodies = 512; // Includes canonical static colliders.
    float contactSlopMeters = 0.005F; // Explicit metre-scale prop contact tolerance.
};

struct RigidBodySnapshot final {
    RigidBodyHandle handle{};
    std::string id;
    RigidBodyMotion motion = RigidBodyMotion::Static;
    math::Vec3 position{};
    math::Quat rotation{};
    math::Vec3 linearVelocity{};
    math::Vec3 angularVelocity{};
    bool sleeping = false;
    bool canonicalCollider = false;
};

struct RigidBodyRayHit final {
    RigidBodyHandle handle{};
    std::string id;
    math::Vec3 position{};
    math::Vec3 normal{};
    float fraction = 1.0F;
    bool canonicalCollider = false;
};

struct RigidBodyStepResult final {
    bool stepped = false;
    std::string error;
    std::uint64_t tick = 0;
    std::uint32_t bodyCount = 0;
    std::uint32_t activeDynamicCount = 0;
    std::uint32_t staticBodiesCreated = 0;
    std::uint32_t staticBodiesRemoved = 0;
    std::uint32_t staticBodiesUpdated = 0;
};

// Optional CPU solver. Single simulation-thread ownership; call after KCC input
// resolution on the authoritative fixed tick. Existing PhysicsWorld/KCC remains
// the character authority. A kinematic capsule may push props; props never feed
// velocity/impulses back into the competitive motor in this slice.
class RigidBodyWorld final {
public:
    RigidBodyWorld();
    ~RigidBodyWorld();
    RigidBodyWorld(RigidBodyWorld&&) noexcept;
    RigidBodyWorld& operator=(RigidBodyWorld&&) noexcept;
    RigidBodyWorld(const RigidBodyWorld&) = delete;
    RigidBodyWorld& operator=(const RigidBodyWorld&) = delete;

    [[nodiscard]] static bool backendAvailable();
    [[nodiscard]] bool initialize(const RigidBodyWorldConfig& config, std::string& error);
    void shutdown(); // Invalidates all handles, including across initialize/reset.
    [[nodiscard]] bool initialized() const;
    [[nodiscard]] RigidBodyHandle createBody(const RigidBodyDesc& desc, std::string& error);
    [[nodiscard]] bool removeBody(RigidBodyHandle handle);
    [[nodiscard]] std::optional<RigidBodySnapshot> body(RigidBodyHandle handle) const;
    [[nodiscard]] std::vector<RigidBodySnapshot> bodies() const; // Application ID order.
    [[nodiscard]] bool applyImpulse(RigidBodyHandle handle, math::Vec3 impulse, math::Vec3 worldPoint);
    [[nodiscard]] bool moveKinematic(RigidBodyHandle handle, math::Vec3 target, math::Quat rotation);

    // Tick must be previous successful tick+1, starting at 1. Derive blocking
    // boxes/ramps from ONE authoritative StaticCollider source; update only changed
    // colliders, never rebuild an unchanged world. No implicit KCC bounds copied:
    // put actual floor/walls in the source. Nonblocking triggers are deferred.
    [[nodiscard]] RigidBodyStepResult step(const PhysicsWorld& canonicalStatics, std::uint64_t tick);
    // displacement encodes both direction AND finite ray length, not a unit ray.
    [[nodiscard]] std::optional<RigidBodyRayHit> raycast(
        math::Vec3 origin, math::Vec3 displacement, std::uint8_t layerMask = 0xff) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace novacore::physics
