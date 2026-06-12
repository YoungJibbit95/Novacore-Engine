#include "novacore/physics/CharacterController.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace novacore::physics {

namespace {

[[nodiscard]] float clamp01(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] float minX(const StaticCollider& collider) {
    return collider.center.x - collider.halfExtents.x;
}

[[nodiscard]] float maxX(const StaticCollider& collider) {
    return collider.center.x + collider.halfExtents.x;
}

[[nodiscard]] float minY(const StaticCollider& collider) {
    return collider.center.y - collider.halfExtents.y;
}

[[nodiscard]] float maxY(const StaticCollider& collider) {
    return collider.center.y + collider.halfExtents.y;
}

[[nodiscard]] float minZ(const StaticCollider& collider) {
    return collider.center.z - collider.halfExtents.z;
}

[[nodiscard]] float maxZ(const StaticCollider& collider) {
    return collider.center.z + collider.halfExtents.z;
}

[[nodiscard]] math::Vec3 normalizeOrZero(math::Vec3 value) {
    const float lengthSquared = value.lengthSquared();
    if (lengthSquared <= 0.000001F) {
        return {};
    }

    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    return value * inverseLength;
}

[[nodiscard]] math::Vec3 normalizeOrUp(math::Vec3 value) {
    const auto normalized = normalizeOrZero(value);
    return normalized.lengthSquared() <= 0.000001F ? math::Vec3{0.0F, 1.0F, 0.0F} : normalized;
}

[[nodiscard]] math::Vec3 tangentForWallNormal(math::Vec3 normal) {
    const math::Vec3 tangent{-normal.z, 0.0F, normal.x};
    return normalizeOrZero(tangent);
}

[[nodiscard]] bool verticalRangesOverlap(float playerFeetY, float playerHeight, const StaticCollider& collider) {
    const float playerMinY = playerFeetY;
    const float playerMaxY = playerFeetY + playerHeight;
    return playerMaxY >= minY(collider) && playerMinY <= maxY(collider);
}

[[nodiscard]] bool horizontalPointInsideExpanded(math::Vec3 position, const StaticCollider& collider, float expansion) {
    return position.x >= minX(collider) - expansion &&
        position.x <= maxX(collider) + expansion &&
        position.z >= minZ(collider) - expansion &&
        position.z <= maxZ(collider) + expansion;
}

[[nodiscard]] float rampT(const StaticCollider& collider, math::Vec3 position) {
    switch (collider.rampDirection) {
    case RampDirection::PositiveZ:
        return clamp01((position.z - minZ(collider)) / std::max(0.001F, collider.halfExtents.z * 2.0F));
    case RampDirection::NegativeZ:
        return 1.0F - clamp01((position.z - minZ(collider)) / std::max(0.001F, collider.halfExtents.z * 2.0F));
    case RampDirection::PositiveX:
        return clamp01((position.x - minX(collider)) / std::max(0.001F, collider.halfExtents.x * 2.0F));
    case RampDirection::NegativeX:
        return 1.0F - clamp01((position.x - minX(collider)) / std::max(0.001F, collider.halfExtents.x * 2.0F));
    case RampDirection::None:
        break;
    }
    return 0.0F;
}

[[nodiscard]] float surfaceHeightAt(const StaticCollider& collider, math::Vec3 position) {
    if (collider.kind != SurfaceKind::Ramp && collider.kind != SurfaceKind::Slide) {
        return maxY(collider);
    }
    if (collider.rampDirection == RampDirection::None) {
        return maxY(collider);
    }

    const float low = minY(collider);
    const float high = maxY(collider);
    return low + ((high - low) * rampT(collider, position));
}

[[nodiscard]] math::Vec3 rampNormal(const StaticCollider& collider) {
    if (collider.rampDirection == RampDirection::None) {
        return {0.0F, 1.0F, 0.0F};
    }

    const float rise = collider.halfExtents.y * 2.0F;
    switch (collider.rampDirection) {
    case RampDirection::PositiveZ: {
        const float run = std::max(0.001F, collider.halfExtents.z * 2.0F);
        return normalizeOrUp({0.0F, 1.0F, -(rise / run)});
    }
    case RampDirection::NegativeZ: {
        const float run = std::max(0.001F, collider.halfExtents.z * 2.0F);
        return normalizeOrUp({0.0F, 1.0F, rise / run});
    }
    case RampDirection::PositiveX: {
        const float run = std::max(0.001F, collider.halfExtents.x * 2.0F);
        return normalizeOrUp({-(rise / run), 1.0F, 0.0F});
    }
    case RampDirection::NegativeX: {
        const float run = std::max(0.001F, collider.halfExtents.x * 2.0F);
        return normalizeOrUp({rise / run, 1.0F, 0.0F});
    }
    case RampDirection::None:
        break;
    }
    return {0.0F, 1.0F, 0.0F};
}

[[nodiscard]] bool canSnapToSurface(
    const CharacterResolveResult& result,
    const StaticCollider& collider,
    const CharacterQuery& query,
    float groundHeight,
    math::Vec3 normal) {
    if (!horizontalPointInsideExpanded(result.position, collider, query.radius)) {
        return false;
    }

    const float verticalDelta = groundHeight - result.position.y;
    if (verticalDelta > std::max(query.maxStepHeight, collider.stepOverrideHeight)) {
        return false;
    }
    if (verticalDelta < -query.snapDownDistance) {
        return false;
    }
    return normalizeOrUp(normal).y >= query.walkableSlopeCosine;
}

[[nodiscard]] bool isGroundSurface(SurfaceKind kind) {
    return kind == SurfaceKind::Floor ||
        kind == SurfaceKind::Ramp ||
        kind == SurfaceKind::Cover ||
        kind == SurfaceKind::Slide ||
        kind == SurfaceKind::Ledge;
}

[[nodiscard]] bool isBlockingSideSurface(SurfaceKind kind) {
    return kind == SurfaceKind::Wall ||
        kind == SurfaceKind::Cover ||
        kind == SurfaceKind::WallRun ||
        kind == SurfaceKind::Ledge;
}

[[nodiscard]] bool isStandableTopSurface(SurfaceKind kind) {
    return kind == SurfaceKind::Cover || kind == SurfaceKind::Ledge;
}

[[nodiscard]] bool isMantleSurface(SurfaceKind kind) {
    return kind == SurfaceKind::Cover || kind == SurfaceKind::Ledge;
}

void recordCorrection(CharacterResolveResult& result, math::Vec3 before, const StaticCollider& collider) {
    const auto delta = result.position - before;
    if (delta.lengthSquared() <= 0.000001F) {
        return;
    }

    result.correction = result.correction + delta;
    result.blocked = true;
    ++result.hitCount;
    result.lastColliderId = collider.id;
}

void recordGround(
    CharacterResolveResult& result,
    const StaticCollider& collider,
    float groundHeight,
    math::Vec3 normal,
    bool stepped) {
    const auto before = result.position;
    result.position.y = groundHeight;
    result.groundHeight = groundHeight;
    result.groundNormal = normalizeOrUp(normal);
    result.grounded = true;
    result.stepped = result.stepped || stepped;
    result.onRamp = result.onRamp || collider.kind == SurfaceKind::Ramp || collider.kind == SurfaceKind::Slide;
    result.nearSlideSurface = result.nearSlideSurface || collider.kind == SurfaceKind::Slide;
    result.groundColliderId = collider.id;
    result.groundKind = collider.kind;

    const auto delta = result.position - before;
    if (delta.lengthSquared() > 0.000001F) {
        result.correction = result.correction + delta;
        ++result.hitCount;
        result.lastColliderId = collider.id;
    }
}

[[nodiscard]] StaticCollider floorColliderForBounds(math::Vec3 boundsHalfExtents) {
    return StaticCollider{
        "floor_main",
        SurfaceKind::Floor,
        {0.0F, -0.05F, 0.0F},
        {boundsHalfExtents.x, 0.05F, boundsHalfExtents.z},
        true,
        RampDirection::None,
        0.0F,
    };
}

void resolveGroundSurfaces(
    CharacterResolveResult& result,
    math::Vec3 boundsHalfExtents,
    const std::vector<StaticCollider>& colliders,
    const CharacterQuery& query) {
    const auto floor = floorColliderForBounds(boundsHalfExtents);
    float bestHeight = 0.0F;
    math::Vec3 bestNormal{0.0F, 1.0F, 0.0F};
    const StaticCollider* bestCollider = nullptr;
    bool bestStepped = false;

    if (canSnapToSurface(result, floor, query, 0.0F, {0.0F, 1.0F, 0.0F})) {
        bestCollider = &floor;
    }

    for (const auto& collider : colliders) {
        if (!collider.blocksMovement || !isGroundSurface(collider.kind)) {
            continue;
        }

        const auto normal = (collider.kind == SurfaceKind::Ramp || collider.kind == SurfaceKind::Slide)
            ? rampNormal(collider)
            : math::Vec3{0.0F, 1.0F, 0.0F};
        const float height = surfaceHeightAt(collider, result.position);
        if (!canSnapToSurface(result, collider, query, height, normal)) {
            continue;
        }

        if (bestCollider == nullptr || bestCollider == &floor || height > bestHeight) {
            bestHeight = height;
            bestNormal = normal;
            bestCollider = &collider;
            bestStepped = collider.kind == SurfaceKind::Cover && height > 0.001F;
        }
    }

    if (bestCollider != nullptr) {
        recordGround(result, *bestCollider, bestHeight, bestNormal, bestStepped);
    }
}

void resolveBounds(CharacterResolveResult& result, math::Vec3 boundsHalfExtents, float radius) {
    const auto before = result.position;
    result.position.x = std::clamp(result.position.x, -boundsHalfExtents.x + radius, boundsHalfExtents.x - radius);
    result.position.z = std::clamp(result.position.z, -boundsHalfExtents.z + radius, boundsHalfExtents.z - radius);
    if ((result.position - before).lengthSquared() > 0.000001F) {
        StaticCollider bounds{};
        bounds.id = "world_bounds";
        bounds.kind = SurfaceKind::Wall;
        result.correction = result.correction + (result.position - before);
        result.blocked = true;
        ++result.hitCount;
        result.lastColliderId = bounds.id;
    }
}

void recordWallContact(
    CharacterResolveResult& result,
    const StaticCollider& collider,
    math::Vec3 normal,
    float distance) {
    if (!result.wallColliderId.empty() && distance >= result.wallDistance) {
        return;
    }

    result.wallNormal = normalizeOrZero(normal);
    result.wallTangent = tangentForWallNormal(result.wallNormal);
    result.wallDistance = distance;
    result.wallColliderId = collider.id;
    result.wallKind = collider.kind;
    result.nearWallRunSurface = collider.kind == SurfaceKind::WallRun;
}

void resolveAgainstExpandedAabb(
    CharacterResolveResult& result,
    const StaticCollider& collider,
    const CharacterQuery& query) {
    if (!collider.blocksMovement || !isBlockingSideSurface(collider.kind)) {
        return;
    }
    if (!verticalRangesOverlap(result.position.y, query.height, collider)) {
        return;
    }

    const float topY = maxY(collider);
    if (isStandableTopSurface(collider.kind) &&
        topY <= result.position.y + std::max(query.maxStepHeight, collider.stepOverrideHeight) &&
        topY >= result.position.y - query.snapDownDistance &&
        horizontalPointInsideExpanded(result.position, collider, query.radius)) {
        recordGround(result, collider, topY, {0.0F, 1.0F, 0.0F}, collider.kind == SurfaceKind::Cover);
        return;
    }

    const float expandedMinX = minX(collider) - query.radius;
    const float expandedMaxX = maxX(collider) + query.radius;
    const float expandedMinZ = minZ(collider) - query.radius;
    const float expandedMaxZ = maxZ(collider) + query.radius;

    if (result.position.x < expandedMinX || result.position.x > expandedMaxX ||
        result.position.z < expandedMinZ || result.position.z > expandedMaxZ) {
        return;
    }

    const auto before = result.position;
    const std::array<float, 4> pushes{
        expandedMinX - result.position.x,
        expandedMaxX - result.position.x,
        expandedMinZ - result.position.z,
        expandedMaxZ - result.position.z,
    };

    std::size_t best = 0;
    float bestMagnitude = std::abs(pushes[0]);
    for (std::size_t index = 1; index < pushes.size(); ++index) {
        const float magnitude = std::abs(pushes[index]);
        if (magnitude < bestMagnitude) {
            best = index;
            bestMagnitude = magnitude;
        }
    }

    math::Vec3 normal{};
    if (best == 0) {
        result.position.x += pushes[best];
        normal = {-1.0F, 0.0F, 0.0F};
    } else if (best == 1) {
        result.position.x += pushes[best];
        normal = {1.0F, 0.0F, 0.0F};
    } else if (best == 2) {
        result.position.z += pushes[best];
        normal = {0.0F, 0.0F, -1.0F};
    } else {
        result.position.z += pushes[best];
        normal = {0.0F, 0.0F, 1.0F};
    }

    recordWallContact(result, collider, normal, bestMagnitude);
    recordCorrection(result, before, collider);
}

[[nodiscard]] float distanceOutsideAabb1D(float value, float minValue, float maxValue) {
    if (value < minValue) {
        return minValue - value;
    }
    if (value > maxValue) {
        return value - maxValue;
    }
    return 0.0F;
}

[[nodiscard]] WallProbeResult probeCollider(const StaticCollider& collider, WallProbe probe) {
    WallProbeResult result{};
    if (!collider.blocksMovement || !isBlockingSideSurface(collider.kind)) {
        return result;
    }
    if (!verticalRangesOverlap(probe.position.y, probe.height, collider)) {
        return result;
    }

    const float dx = distanceOutsideAabb1D(probe.position.x, minX(collider), maxX(collider));
    const float dz = distanceOutsideAabb1D(probe.position.z, minZ(collider), maxZ(collider));
    const float distance = std::sqrt((dx * dx) + (dz * dz));
    const float allowed = std::max(0.0F, probe.radius + probe.maxDistance);
    if (distance > allowed) {
        return result;
    }

    const float clampedX = std::clamp(probe.position.x, minX(collider), maxX(collider));
    const float clampedZ = std::clamp(probe.position.z, minZ(collider), maxZ(collider));
    math::Vec3 normal = normalizeOrZero({probe.position.x - clampedX, 0.0F, probe.position.z - clampedZ});
    if (normal.lengthSquared() <= 0.000001F) {
        const std::array<std::pair<float, math::Vec3>, 4> faceDistances{{
            {std::abs(probe.position.x - minX(collider)), {-1.0F, 0.0F, 0.0F}},
            {std::abs(maxX(collider) - probe.position.x), {1.0F, 0.0F, 0.0F}},
            {std::abs(probe.position.z - minZ(collider)), {0.0F, 0.0F, -1.0F}},
            {std::abs(maxZ(collider) - probe.position.z), {0.0F, 0.0F, 1.0F}},
        }};
        const auto nearest = std::min_element(
            faceDistances.begin(),
            faceDistances.end(),
            [](const auto& lhs, const auto& rhs) {
                return lhs.first < rhs.first;
            });
        normal = nearest->second;
    }

    result.hit = true;
    result.point = {clampedX, probe.position.y, clampedZ};
    result.normal = normal;
    result.tangent = tangentForWallNormal(normal);
    result.distance = std::max(0.0F, distance - probe.radius);
    result.kind = collider.kind;
    result.colliderId = collider.id;
    return result;
}

[[nodiscard]] bool rayIntersectsExpandedAabb2D(
    math::Vec3 origin,
    math::Vec3 direction,
    const StaticCollider& collider,
    float expansion,
    float maxDistance,
    float& outDistance,
    math::Vec3& outNormal) {
    constexpr float kEpsilon = 0.00001F;
    float tMin = 0.0F;
    float tMax = std::max(0.0F, maxDistance);
    math::Vec3 normal{};

    const auto testAxis = [&](float originValue, float directionValue, float minValue, float maxValue, math::Vec3 minNormal, math::Vec3 maxNormal) {
        if (std::abs(directionValue) <= kEpsilon) {
            return originValue >= minValue && originValue <= maxValue;
        }

        float nearT = (minValue - originValue) / directionValue;
        float farT = (maxValue - originValue) / directionValue;
        math::Vec3 nearNormal = minNormal;
        if (nearT > farT) {
            std::swap(nearT, farT);
            nearNormal = maxNormal;
        }

        if (nearT > tMin) {
            tMin = nearT;
            normal = nearNormal;
        }
        tMax = std::min(tMax, farT);
        return tMin <= tMax;
    };

    if (!testAxis(
            origin.x,
            direction.x,
            minX(collider) - expansion,
            maxX(collider) + expansion,
            {-1.0F, 0.0F, 0.0F},
            {1.0F, 0.0F, 0.0F})) {
        return false;
    }
    if (!testAxis(
            origin.z,
            direction.z,
            minZ(collider) - expansion,
            maxZ(collider) + expansion,
            {0.0F, 0.0F, -1.0F},
            {0.0F, 0.0F, 1.0F})) {
        return false;
    }

    outDistance = std::clamp(tMin, 0.0F, maxDistance);
    if (normal.lengthSquared() <= 0.000001F) {
        normal = normalizeOrZero({-direction.x, 0.0F, -direction.z});
    }
    outNormal = normal;
    return tMax >= 0.0F && tMin <= maxDistance;
}

[[nodiscard]] float clampToInset(float value, float minValue, float maxValue, float inset) {
    const float low = minValue + inset;
    const float high = maxValue - inset;
    if (low > high) {
        return (minValue + maxValue) * 0.5F;
    }
    return std::clamp(value, low, high);
}

[[nodiscard]] bool mantleTargetHasClearance(
    const std::vector<StaticCollider>& colliders,
    const StaticCollider& mantleCollider,
    math::Vec3 target,
    float radius,
    float height) {
    for (const auto& collider : colliders) {
        if (&collider == &mantleCollider || !collider.blocksMovement || collider.kind == SurfaceKind::Trigger) {
            continue;
        }
        if (!verticalRangesOverlap(target.y + 0.04F, height - 0.08F, collider)) {
            continue;
        }
        if (horizontalPointInsideExpanded(target, collider, radius * 0.85F)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] MantleProbeResult probeMantleCollider(
    const StaticCollider& collider,
    const std::vector<StaticCollider>& colliders,
    MantleProbe probe,
    math::Vec3 forward) {
    MantleProbeResult result{};
    if (!collider.blocksMovement || !isMantleSurface(collider.kind)) {
        return result;
    }

    const float topY = maxY(collider);
    const float mantleHeight = topY - probe.position.y;
    if (mantleHeight < probe.minHeight || mantleHeight > probe.maxHeight) {
        return result;
    }

    float distance = 0.0F;
    math::Vec3 normal{};
    if (!rayIntersectsExpandedAabb2D(
            probe.position,
            forward,
            collider,
            probe.radius,
            probe.maxDistance + probe.radius,
            distance,
            normal)) {
        return result;
    }

    const auto hit = probe.position + (forward * distance);
    const float inset = std::max(0.02F, probe.radius + probe.landingInset);
    const auto desiredTarget = probe.position + (forward * (distance + inset));
    const math::Vec3 target{
        clampToInset(desiredTarget.x, minX(collider), maxX(collider), probe.radius),
        topY,
        clampToInset(desiredTarget.z, minZ(collider), maxZ(collider), probe.radius),
    };

    if (!mantleTargetHasClearance(colliders, collider, target, probe.radius, probe.height)) {
        return result;
    }

    result.hit = true;
    result.obstaclePoint = {
        std::clamp(hit.x, minX(collider), maxX(collider)),
        topY,
        std::clamp(hit.z, minZ(collider), maxZ(collider)),
    };
    result.targetPosition = target;
    result.normal = normal;
    result.distance = std::max(0.0F, distance - probe.radius);
    result.height = mantleHeight;
    result.kind = collider.kind;
    result.colliderId = collider.id;
    return result;
}

} // namespace

void PhysicsWorld::setBounds(math::Vec3 halfExtents) {
    boundsHalfExtents_.x = std::max(0.01F, halfExtents.x);
    boundsHalfExtents_.y = std::max(0.01F, halfExtents.y);
    boundsHalfExtents_.z = std::max(0.01F, halfExtents.z);
}

math::Vec3 PhysicsWorld::boundsHalfExtents() const {
    return boundsHalfExtents_;
}

void PhysicsWorld::clearStaticColliders() {
    staticColliders_.clear();
}

void PhysicsWorld::addStaticCollider(StaticCollider collider) {
    collider.halfExtents.x = std::max(0.001F, collider.halfExtents.x);
    collider.halfExtents.y = std::max(0.001F, collider.halfExtents.y);
    collider.halfExtents.z = std::max(0.001F, collider.halfExtents.z);
    staticColliders_.push_back(std::move(collider));
}

const std::vector<StaticCollider>& PhysicsWorld::staticColliders() const {
    return staticColliders_;
}

const StaticCollider* PhysicsWorld::findStaticCollider(std::string_view id) const {
    const auto it = std::find_if(
        staticColliders_.begin(),
        staticColliders_.end(),
        [id](const StaticCollider& collider) {
            return collider.id == id;
        });
    return it == staticColliders_.end() ? nullptr : &(*it);
}

std::size_t PhysicsWorld::colliderCount() const {
    return staticColliders_.size();
}

CharacterResolveResult PhysicsWorld::resolveCharacter(CharacterQuery query) const {
    query.radius = std::max(0.01F, query.radius);
    query.height = std::max(0.01F, query.height);
    query.maxStepHeight = std::max(0.0F, query.maxStepHeight);
    query.snapDownDistance = std::max(0.0F, query.snapDownDistance);
    query.walkableSlopeCosine = std::clamp(query.walkableSlopeCosine, 0.0F, 1.0F);

    CharacterResolveResult result{};
    result.position = query.position;

    if (result.position.y <= 0.0F) {
        result.position.y = 0.0F;
        result.groundHeight = 0.0F;
        result.groundNormal = {0.0F, 1.0F, 0.0F};
        result.grounded = true;
        result.groundColliderId = "floor_main";
        result.groundKind = SurfaceKind::Floor;
    }

    resolveGroundSurfaces(result, boundsHalfExtents_, staticColliders_, query);
    resolveBounds(result, boundsHalfExtents_, query.radius);

    for (const auto& collider : staticColliders_) {
        resolveAgainstExpandedAabb(result, collider, query);
    }

    const auto wall = probeWall(WallProbe{result.position, query.radius, query.height, 0.18F});
    if (wall.hit) {
        if (const auto* collider = findStaticCollider(wall.colliderId)) {
            recordWallContact(result, *collider, wall.normal, wall.distance);
        }
    }

    return result;
}

WallProbeResult PhysicsWorld::probeWall(WallProbe probe) const {
    probe.radius = std::max(0.01F, probe.radius);
    probe.height = std::max(0.01F, probe.height);
    probe.maxDistance = std::max(0.0F, probe.maxDistance);

    WallProbeResult best{};
    best.distance = std::numeric_limits<float>::max();
    for (const auto& collider : staticColliders_) {
        auto candidate = probeCollider(collider, probe);
        if (!candidate.hit) {
            continue;
        }
        if (!best.hit || candidate.distance < best.distance) {
            best = std::move(candidate);
        }
    }

    if (!best.hit) {
        best.distance = 0.0F;
    }
    return best;
}

MantleProbeResult PhysicsWorld::probeMantle(MantleProbe probe) const {
    probe.radius = std::max(0.01F, probe.radius);
    probe.height = std::max(0.20F, probe.height);
    probe.maxDistance = std::max(0.0F, probe.maxDistance);
    probe.minHeight = std::max(0.0F, probe.minHeight);
    probe.maxHeight = std::max(probe.minHeight, probe.maxHeight);
    probe.landingInset = std::max(0.0F, probe.landingInset);

    math::Vec3 forward{probe.forward.x, 0.0F, probe.forward.z};
    forward = normalizeOrZero(forward);
    if (forward.lengthSquared() <= 0.000001F) {
        return {};
    }

    MantleProbeResult best{};
    best.distance = std::numeric_limits<float>::max();
    for (const auto& collider : staticColliders_) {
        auto candidate = probeMantleCollider(collider, staticColliders_, probe, forward);
        if (!candidate.hit) {
            continue;
        }
        if (!best.hit || candidate.distance < best.distance) {
            best = std::move(candidate);
        }
    }

    if (!best.hit) {
        best.distance = 0.0F;
    }
    return best;
}

const char* surfaceKindName(SurfaceKind kind) {
    switch (kind) {
    case SurfaceKind::Floor:
        return "floor";
    case SurfaceKind::Wall:
        return "wall";
    case SurfaceKind::Ramp:
        return "ramp";
    case SurfaceKind::Cover:
        return "cover";
    case SurfaceKind::WallRun:
        return "wall_run";
    case SurfaceKind::Slide:
        return "slide";
    case SurfaceKind::Ledge:
        return "ledge";
    case SurfaceKind::Trigger:
        return "trigger";
    }
    return "unknown";
}

} // namespace novacore::physics
