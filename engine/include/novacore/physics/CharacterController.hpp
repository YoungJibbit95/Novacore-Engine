#pragma once

#include "novacore/math/Types.hpp"

#include <cstddef>
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

struct StaticCollider final {
    std::string id;
    SurfaceKind kind = SurfaceKind::Wall;
    math::Vec3 center{};
    math::Vec3 halfExtents{0.5F, 0.5F, 0.5F};
    bool blocksMovement = true;
    RampDirection rampDirection = RampDirection::None;
    float stepOverrideHeight = 0.0F;
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
};

struct CharacterResolveResult final {
    math::Vec3 position{};
    math::Vec3 correction{};
    math::Vec3 groundNormal{0.0F, 1.0F, 0.0F};
    math::Vec3 wallNormal{};
    math::Vec3 wallTangent{};
    float groundHeight = 0.0F;
    float wallDistance = 0.0F;
    std::size_t hitCount = 0;
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
    [[nodiscard]] const std::vector<StaticCollider>& staticColliders() const;
    [[nodiscard]] const StaticCollider* findStaticCollider(std::string_view id) const;
    [[nodiscard]] std::size_t colliderCount() const;

    [[nodiscard]] CharacterResolveResult resolveCharacter(CharacterQuery query) const;
    [[nodiscard]] WallProbeResult probeWall(WallProbe probe) const;
    [[nodiscard]] MantleProbeResult probeMantle(MantleProbe probe) const;

private:
    math::Vec3 boundsHalfExtents_{100.0F, 100.0F, 100.0F};
    std::vector<StaticCollider> staticColliders_;
};

[[nodiscard]] const char* surfaceKindName(SurfaceKind kind);

} // namespace novacore::physics
