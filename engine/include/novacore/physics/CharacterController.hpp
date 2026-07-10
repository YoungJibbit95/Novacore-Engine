#pragma once

#include "novacore/math/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace novacore::physics {

enum class SurfaceKind {
    Floor,
    Wall,
    Ramp,
    Cover,
    WallRun,
    Slide,
    Ledge,
    Trigger,
};

enum class RampDirection {
    None,
    PositiveX,
    NegativeX,
    PositiveZ,
    NegativeZ,
};

enum class CharacterContactRole {
    Ground,
    Step,
    Wall,
    Ceiling,
    Bounds,
    Sweep,
};

struct StaticCollider final {
    std::string id;
    SurfaceKind kind = SurfaceKind::Wall;
    math::Vec3 center{};
    math::Vec3 halfExtents{0.5F, 0.5F, 0.5F};
    bool blocksMovement = true;
    RampDirection rampDirection = RampDirection::None;
    float stepOverrideHeight = 0.0F;
    math::Vec3 velocity{};
};

struct CharacterQuery final {
    math::Vec3 position{};
    float radius = 0.42F;
    float height = 1.80F;
    float maxStepHeight = 0.42F;
    float snapDownDistance = 0.35F;
    float walkableSlopeCosine = 0.68F;
    float wallProbeDistance = 0.30F;
    bool enableGroundSnap = true;
    bool enableStepUp = true;
    float skinWidth = 0.003F;
    int maxDepenetrationIterations = 6;
};

struct CharacterSweepQuery final {
    math::Vec3 startPosition{};
    math::Vec3 desiredDisplacement{};
    float radius = 0.42F;
    float height = 1.80F;
    float maxStepHeight = 0.42F;
    float snapDownDistance = 0.35F;
    float walkableSlopeCosine = 0.68F;
    float wallProbeDistance = 0.30F;
    int maxIterations = 4;
    bool enableGroundSnap = true;
    bool enableStepUp = true;
    float skinWidth = 0.003F;
    float deltaSeconds = 0.0F;
    int maxDepenetrationIterations = 6;
};

struct CharacterContact final {
    std::string colliderId;
    SurfaceKind surfaceKind = SurfaceKind::Wall;
    CharacterContactRole role = CharacterContactRole::Wall;
    math::Vec3 point{};
    math::Vec3 normal{};
    float distance = 0.0F;
    float fraction = 1.0F;
    float penetrationDepth = 0.0F;
    bool blocking = false;
    bool walkable = false;
    math::Vec3 surfaceVelocity{};
    std::uint32_t iteration = 0;
};

struct CharacterResolveResult final {
    math::Vec3 position{};
    math::Vec3 correction{};
    math::Vec3 groundNormal{0.0F, 1.0F, 0.0F};
    math::Vec3 wallNormal{};
    math::Vec3 wallTangent{};
    float groundHeight = 0.0F;
    float groundSnapDistance = 0.0F;
    float stepHeight = 0.0F;
    float wallDistance = 0.0F;
    std::size_t hitCount = 0;
    std::size_t depenetrationIterations = 0;
    std::size_t blockingContactCount = 0;
    std::size_t walkableContactCount = 0;
    bool grounded = false;
    bool blocked = false;
    bool stepped = false;
    bool onRamp = false;
    bool nearWallRunSurface = false;
    bool nearSlideSurface = false;
    std::string lastColliderId;
    std::string groundColliderId;
    std::string wallColliderId;
    SurfaceKind groundKind = SurfaceKind::Floor;
    SurfaceKind wallKind = SurfaceKind::Wall;
    math::Vec3 groundVelocity{};
    math::Vec3 wallVelocity{};
    std::vector<CharacterContact> contacts;
    std::uint64_t contactHash = 0;
};

struct CharacterSweepResult final {
    CharacterResolveResult resolve{};
    math::Vec3 startPosition{};
    math::Vec3 desiredDisplacement{};
    math::Vec3 appliedDisplacement{};
    math::Vec3 remainingDisplacement{};
    math::Vec3 hitNormal{};
    float firstHitFraction = 1.0F;
    std::size_t iterationCount = 0;
    std::size_t slidePlaneCount = 0;
    bool swept = false;
    bool hit = false;
    bool stepped = false;
    bool startedGrounded = false;
    bool endedGrounded = false;
    bool ceilingHit = false;
    bool stoppedOnCrease = false;
    float stepHeight = 0.0F;
    std::string hitColliderId;
    SurfaceKind hitKind = SurfaceKind::Wall;
    std::vector<CharacterContact> sweepContacts;
    std::uint64_t contactHash = 0;
};

struct WallProbe final {
    math::Vec3 position{};
    float radius = 0.42F;
    float height = 1.80F;
    float maxDistance = 0.55F;
};

struct WallProbeResult final {
    bool hit = false;
    math::Vec3 point{};
    math::Vec3 normal{};
    math::Vec3 tangent{};
    float distance = 0.0F;
    SurfaceKind kind = SurfaceKind::Wall;
    std::string colliderId;
};

struct MantleProbe final {
    math::Vec3 position{};
    math::Vec3 forward{0.0F, 0.0F, 1.0F};
    float radius = 0.42F;
    float height = 1.80F;
    float maxDistance = 1.25F;
    float minHeight = 0.44F;
    float maxHeight = 1.45F;
    float landingInset = 0.18F;
};

struct MantleProbeResult final {
    bool hit = false;
    math::Vec3 obstaclePoint{};
    math::Vec3 targetPosition{};
    math::Vec3 normal{};
    float distance = 0.0F;
    float height = 0.0F;
    SurfaceKind kind = SurfaceKind::Ledge;
    std::string colliderId;
};

class PhysicsWorld final {
public:
    void setBounds(math::Vec3 halfExtents);
    [[nodiscard]] math::Vec3 boundsHalfExtents() const;

    void clearStaticColliders();
    void addStaticCollider(StaticCollider collider);
    [[nodiscard]] bool setColliderKinematics(
        std::string_view id,
        math::Vec3 center,
        math::Vec3 velocity);
    [[nodiscard]] std::size_t advanceKinematicColliders(float deltaSeconds);
    [[nodiscard]] const std::vector<StaticCollider>& staticColliders() const;
    [[nodiscard]] const StaticCollider* findStaticCollider(std::string_view id) const;
    [[nodiscard]] std::size_t colliderCount() const;

    [[nodiscard]] CharacterResolveResult resolveCharacter(CharacterQuery query) const;
    [[nodiscard]] CharacterSweepResult sweepCharacter(CharacterSweepQuery query) const;
    [[nodiscard]] WallProbeResult probeWall(WallProbe probe) const;
    [[nodiscard]] MantleProbeResult probeMantle(MantleProbe probe) const;

private:
    math::Vec3 boundsHalfExtents_{100.0F, 100.0F, 100.0F};
    std::vector<StaticCollider> staticColliders_;
};

[[nodiscard]] const char* surfaceKindName(SurfaceKind kind);
[[nodiscard]] const char* contactRoleName(CharacterContactRole role);
[[nodiscard]] std::uint64_t hashCharacterContacts(std::span<const CharacterContact> contacts);

} // namespace novacore::physics
