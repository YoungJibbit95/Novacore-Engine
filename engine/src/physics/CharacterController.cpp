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

[[nodiscard]] float horizontalLength(math::Vec3 value) {
    return std::sqrt((value.x * value.x) + (value.z * value.z));
}

[[nodiscard]] float dotHorizontal(math::Vec3 lhs, math::Vec3 rhs) {
    return (lhs.x * rhs.x) + (lhs.z * rhs.z);
}

[[nodiscard]] bool verticalRangesOverlap(float playerFeetY, float playerHeight, const StaticCollider& collider) {
    constexpr float kVerticalEpsilon = 0.0001F;
    const float playerMinY = playerFeetY;
    const float playerMaxY = playerFeetY + playerHeight;
    return playerMaxY > minY(collider) + kVerticalEpsilon && playerMinY < maxY(collider) - kVerticalEpsilon;
}

[[nodiscard]] bool horizontalPointInsideExpanded(math::Vec3 position, const StaticCollider& collider, float expansion) {
    return position.x >= minX(collider) - expansion &&
        position.x <= maxX(collider) + expansion &&
        position.z >= minZ(collider) - expansion &&
        position.z <= maxZ(collider) + expansion;
}

[[nodiscard]] bool horizontalCircleOverlapsAabb(
    math::Vec3 position,
    const StaticCollider& collider,
    float radius) {
    const float closestX = std::clamp(position.x, minX(collider), maxX(collider));
    const float closestZ = std::clamp(position.z, minZ(collider), maxZ(collider));
    const float dx = position.x - closestX;
    const float dz = position.z - closestZ;
    const float safeRadius = std::max(0.0F, radius);
    return (dx * dx) + (dz * dz) <= (safeRadius * safeRadius) + 0.000001F;
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
    if (!horizontalCircleOverlapsAabb(result.position, collider, query.radius + query.skinWidth)) {
        return false;
    }

    const float verticalDelta = groundHeight - result.position.y;
    const bool rampLike = collider.kind == SurfaceKind::Ramp || collider.kind == SurfaceKind::Slide;
    const float rampRise = rampLike ? std::max(0.0F, maxY(collider) - minY(collider)) : 0.0F;
    float maxStepUp = query.enableStepUp
        ? std::max(query.maxStepHeight, collider.stepOverrideHeight)
        : 0.001F;
    float maxSnapDown = query.enableGroundSnap
        ? query.snapDownDistance
        : 0.001F;
    if (rampLike && query.enableStepUp) {
        maxStepUp = std::max(maxStepUp, rampRise + 0.04F);
    }
    if (rampLike && query.enableGroundSnap) {
        maxSnapDown = std::max(maxSnapDown, query.snapDownDistance + rampRise + 0.04F);
    }
    if (verticalDelta > maxStepUp) {
        return false;
    }
    if (verticalDelta < -maxSnapDown) {
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

void appendContact(CharacterResolveResult& result, CharacterContact contact) {
    if (contact.colliderId.empty()) {
        return;
    }

    contact.normal = normalizeOrZero(contact.normal);
    for (auto& existing : result.contacts) {
        if (existing.colliderId == contact.colliderId && existing.role == contact.role) {
            if (contact.distance <= existing.distance || contact.penetrationDepth >= existing.penetrationDepth) {
                existing = std::move(contact);
            }
            return;
        }
    }

    result.contacts.push_back(std::move(contact));
}

void recordCorrection(CharacterResolveResult& result, math::Vec3 before, const StaticCollider& collider) {
    const auto delta = result.position - before;
    if (delta.lengthSquared() <= 0.000001F) {
        return;
    }

    result.correction = result.correction + delta;
    result.blocked = true;
    ++result.hitCount;
    if (result.lastColliderId.empty()) {
        result.lastColliderId = collider.id;
    }
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
    result.groundVelocity = collider.velocity;
    const float verticalCorrection = groundHeight - before.y;
    if (verticalCorrection < 0.0F) {
        result.groundSnapDistance = std::max(result.groundSnapDistance, -verticalCorrection);
    }
    if (stepped && verticalCorrection > 0.0F) {
        result.stepHeight = std::max(result.stepHeight, verticalCorrection);
    }
    appendContact(
        result,
        CharacterContact{
            collider.id,
            collider.kind,
            stepped ? CharacterContactRole::Step : CharacterContactRole::Ground,
            {result.position.x, groundHeight, result.position.z},
            result.groundNormal,
            std::abs(groundHeight - before.y),
            1.0F,
            0.0F,
            false,
            true,
            collider.velocity,
        });

    const auto delta = result.position - before;
    if (delta.lengthSquared() > 0.000001F) {
        result.correction = result.correction + delta;
        ++result.hitCount;
        if (result.lastColliderId.empty()) {
            result.lastColliderId = collider.id;
        }
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

        const bool higher = height > bestHeight + 0.00001F;
        const bool sameHeight = std::abs(height - bestHeight) <= 0.00001F;
        const bool stableTie = sameHeight && bestCollider != nullptr && collider.id < bestCollider->id;
        if (bestCollider == nullptr || bestCollider == &floor || higher || stableTie) {
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
    const auto delta = result.position - before;
    if (delta.lengthSquared() > 0.000001F) {
        StaticCollider bounds{};
        bounds.id = "world_bounds";
        bounds.kind = SurfaceKind::Wall;
        math::Vec3 normal{};
        if (std::abs(delta.x) >= std::abs(delta.z)) {
            normal = {delta.x < 0.0F ? -1.0F : 1.0F, 0.0F, 0.0F};
        } else {
            normal = {0.0F, 0.0F, delta.z < 0.0F ? -1.0F : 1.0F};
        }
        result.correction = result.correction + delta;
        result.blocked = true;
        ++result.hitCount;
        result.lastColliderId = bounds.id;
        appendContact(
            result,
            CharacterContact{
                bounds.id,
                bounds.kind,
                CharacterContactRole::Bounds,
                result.position,
                normal,
                0.0F,
                1.0F,
                horizontalLength(delta),
                true,
                false,
            });
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
    result.wallVelocity = collider.velocity;
    result.nearWallRunSurface = collider.kind == SurfaceKind::WallRun;
    appendContact(
        result,
        CharacterContact{
            collider.id,
            collider.kind,
            CharacterContactRole::Wall,
            result.position,
            result.wallNormal,
            distance,
            1.0F,
            0.0F,
            true,
            false,
            collider.velocity,
        });
}

struct DepenetrationCandidate final {
    bool hit = false;
    const StaticCollider* collider = nullptr;
    math::Vec3 correction{};
    math::Vec3 normal{};
    float depth = 0.0F;
};

[[nodiscard]] DepenetrationCandidate depenetrationAgainstCollider(
    math::Vec3 position,
    const StaticCollider& collider,
    const CharacterQuery& query) {
    DepenetrationCandidate candidate{};
    if (!collider.blocksMovement || !isBlockingSideSurface(collider.kind) ||
        !verticalRangesOverlap(position.y, query.height, collider)) {
        return candidate;
    }

    const float effectiveRadius = query.radius + query.skinWidth;
    const float closestX = std::clamp(position.x, minX(collider), maxX(collider));
    const float closestZ = std::clamp(position.z, minZ(collider), maxZ(collider));
    const float dx = position.x - closestX;
    const float dz = position.z - closestZ;
    const float distanceSquared = (dx * dx) + (dz * dz);
    if (distanceSquared > (effectiveRadius * effectiveRadius) - 0.000001F) {
        return candidate;
    }

    math::Vec3 normal{};
    float depth = 0.0F;
    if (distanceSquared > 0.000001F) {
        const float distance = std::sqrt(distanceSquared);
        normal = {dx / distance, 0.0F, dz / distance};
        depth = effectiveRadius - distance;
    } else {
        const std::array<std::pair<float, math::Vec3>, 4> faces{{
            {position.x - minX(collider), {-1.0F, 0.0F, 0.0F}},
            {maxX(collider) - position.x, {1.0F, 0.0F, 0.0F}},
            {position.z - minZ(collider), {0.0F, 0.0F, -1.0F}},
            {maxZ(collider) - position.z, {0.0F, 0.0F, 1.0F}},
        }};
        const auto nearest = std::min_element(
            faces.begin(),
            faces.end(),
            [](const auto& lhs, const auto& rhs) {
                return lhs.first < rhs.first;
            });
        normal = nearest->second;
        depth = effectiveRadius + nearest->first;
    }

    if (depth <= 0.000001F) {
        return candidate;
    }
    candidate.hit = true;
    candidate.collider = &collider;
    candidate.normal = normal;
    candidate.depth = depth;
    candidate.correction = normal * depth;
    return candidate;
}

[[nodiscard]] bool preferDepenetration(
    const DepenetrationCandidate& candidate,
    const DepenetrationCandidate& current) {
    if (!candidate.hit) {
        return false;
    }
    if (!current.hit || candidate.depth > current.depth + 0.00001F) {
        return true;
    }
    if (std::abs(candidate.depth - current.depth) <= 0.00001F) {
        return candidate.collider->id < current.collider->id;
    }
    return false;
}

void resolveSidePenetrations(
    CharacterResolveResult& result,
    const std::vector<StaticCollider>& colliders,
    const CharacterQuery& query) {
    for (int iteration = 0; iteration < query.maxDepenetrationIterations; ++iteration) {
        DepenetrationCandidate best{};
        for (const auto& collider : colliders) {
            const auto candidate = depenetrationAgainstCollider(result.position, collider, query);
            if (preferDepenetration(candidate, best)) {
                best = candidate;
            }
        }
        if (!best.hit || best.collider == nullptr) {
            break;
        }

        const auto before = result.position;
        result.position = result.position + best.correction;
        recordWallContact(result, *best.collider, best.normal, best.depth);
        recordCorrection(result, before, *best.collider);
        ++result.depenetrationIterations;
    }
}

void clearGroundState(CharacterResolveResult& result) {
    result.groundNormal = {0.0F, 1.0F, 0.0F};
    result.groundHeight = 0.0F;
    result.grounded = false;
    result.stepped = false;
    result.onRamp = false;
    result.nearSlideSurface = false;
    result.groundColliderId.clear();
    result.groundKind = SurfaceKind::Floor;
    result.groundVelocity = {};
    result.contacts.erase(
        std::remove_if(
            result.contacts.begin(),
            result.contacts.end(),
            [](const CharacterContact& contact) {
                return contact.role == CharacterContactRole::Ground || contact.role == CharacterContactRole::Step;
            }),
        result.contacts.end());
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

[[nodiscard]] CharacterQuery characterQueryFromSweep(CharacterSweepQuery query, math::Vec3 position) {
    CharacterQuery characterQuery{};
    characterQuery.position = position;
    characterQuery.radius = query.radius;
    characterQuery.height = query.height;
    characterQuery.maxStepHeight = query.maxStepHeight;
    characterQuery.snapDownDistance = query.snapDownDistance;
    characterQuery.walkableSlopeCosine = query.walkableSlopeCosine;
    characterQuery.wallProbeDistance = query.wallProbeDistance;
    characterQuery.enableGroundSnap = query.enableGroundSnap;
    characterQuery.enableStepUp = query.enableStepUp;
    characterQuery.skinWidth = query.skinWidth;
    characterQuery.maxDepenetrationIterations = query.maxDepenetrationIterations;
    return characterQuery;
}

[[nodiscard]] bool verticalSweepRangeOverlaps(
    math::Vec3 start,
    math::Vec3 end,
    float height,
    const StaticCollider& collider) {
    const float playerMinY = std::min(start.y, end.y);
    const float playerMaxY = std::max(start.y, end.y) + height;
    return playerMaxY >= minY(collider) && playerMinY <= maxY(collider);
}

[[nodiscard]] bool canStepOntoColliderDuringSweep(
    const StaticCollider& collider,
    const CharacterSweepQuery& query,
    math::Vec3 position) {
    if (!query.enableStepUp || !isStandableTopSurface(collider.kind)) {
        return false;
    }

    const float stepHeight = maxY(collider) - position.y;
    const float allowedHeight = std::max(query.maxStepHeight, collider.stepOverrideHeight);
    return stepHeight > query.skinWidth &&
        stepHeight <= allowedHeight + query.skinWidth &&
        horizontalCircleOverlapsAabb(position, collider, query.radius + query.skinWidth);
}

[[nodiscard]] bool capsuleHasStepClearance(
    const std::vector<StaticCollider>& colliders,
    const StaticCollider& stepCollider,
    math::Vec3 position,
    const CharacterSweepQuery& query) {
    for (const auto& collider : colliders) {
        if (&collider == &stepCollider || !collider.blocksMovement || collider.kind == SurfaceKind::Trigger) {
            continue;
        }
        if (!verticalRangesOverlap(position.y, query.height, collider)) {
            continue;
        }
        if (horizontalCircleOverlapsAabb(position, collider, query.radius + query.skinWidth)) {
            return false;
        }
    }
    return true;
}

struct SweepHitCandidate final {
    bool hit = false;
    float fraction = 1.0F;
    math::Vec3 normal{};
    const StaticCollider* collider = nullptr;
    std::string colliderId;
    SurfaceKind kind = SurfaceKind::Wall;
    math::Vec3 surfaceVelocity{};
    bool walkable = false;
};

[[nodiscard]] bool preferSweepHit(const SweepHitCandidate& candidate, const SweepHitCandidate& current) {
    if (!candidate.hit) {
        return false;
    }
    if (!current.hit || candidate.fraction < current.fraction - 0.00001F) {
        return true;
    }
    if (std::abs(candidate.fraction - current.fraction) <= 0.00001F) {
        return candidate.colliderId < current.colliderId;
    }
    return false;
}

void considerSweepHit(SweepHitCandidate& best, SweepHitCandidate candidate) {
    if (preferSweepHit(candidate, best)) {
        best = std::move(candidate);
    }
}

[[nodiscard]] SweepHitCandidate sweepCircleAgainstAabb2D(
    math::Vec3 position,
    math::Vec3 displacement,
    const StaticCollider& collider,
    float radius) {
    constexpr float kEpsilon = 0.00001F;
    SweepHitCandidate best{};

    const auto makeCandidate = [&](float fraction, math::Vec3 normal) {
        SweepHitCandidate candidate{};
        if (fraction < -kEpsilon || fraction > 1.0F + kEpsilon ||
            dotHorizontal(displacement, normal) >= -kEpsilon) {
            return candidate;
        }
        candidate.hit = true;
        candidate.fraction = std::clamp(fraction, 0.0F, 1.0F);
        candidate.normal = normal;
        candidate.collider = &collider;
        candidate.colliderId = collider.id;
        candidate.kind = collider.kind;
        candidate.surfaceVelocity = collider.velocity;
        return candidate;
    };

    if (displacement.x > kEpsilon) {
        const float t = ((minX(collider) - radius) - position.x) / displacement.x;
        const float z = position.z + (displacement.z * t);
        if (z >= minZ(collider) - kEpsilon && z <= maxZ(collider) + kEpsilon) {
            considerSweepHit(best, makeCandidate(t, {-1.0F, 0.0F, 0.0F}));
        }
    } else if (displacement.x < -kEpsilon) {
        const float t = ((maxX(collider) + radius) - position.x) / displacement.x;
        const float z = position.z + (displacement.z * t);
        if (z >= minZ(collider) - kEpsilon && z <= maxZ(collider) + kEpsilon) {
            considerSweepHit(best, makeCandidate(t, {1.0F, 0.0F, 0.0F}));
        }
    }

    if (displacement.z > kEpsilon) {
        const float t = ((minZ(collider) - radius) - position.z) / displacement.z;
        const float x = position.x + (displacement.x * t);
        if (x >= minX(collider) - kEpsilon && x <= maxX(collider) + kEpsilon) {
            considerSweepHit(best, makeCandidate(t, {0.0F, 0.0F, -1.0F}));
        }
    } else if (displacement.z < -kEpsilon) {
        const float t = ((maxZ(collider) + radius) - position.z) / displacement.z;
        const float x = position.x + (displacement.x * t);
        if (x >= minX(collider) - kEpsilon && x <= maxX(collider) + kEpsilon) {
            considerSweepHit(best, makeCandidate(t, {0.0F, 0.0F, 1.0F}));
        }
    }

    const float a = (displacement.x * displacement.x) + (displacement.z * displacement.z);
    if (a <= kEpsilon) {
        return best;
    }
    const std::array<math::Vec3, 4> corners{{
        {minX(collider), 0.0F, minZ(collider)},
        {minX(collider), 0.0F, maxZ(collider)},
        {maxX(collider), 0.0F, minZ(collider)},
        {maxX(collider), 0.0F, maxZ(collider)},
    }};
    for (const auto corner : corners) {
        const float rx = position.x - corner.x;
        const float rz = position.z - corner.z;
        const float b = 2.0F * ((rx * displacement.x) + (rz * displacement.z));
        const float c = (rx * rx) + (rz * rz) - (radius * radius);
        const float discriminant = (b * b) - (4.0F * a * c);
        if (discriminant < 0.0F) {
            continue;
        }
        const float t = (-b - std::sqrt(discriminant)) / (2.0F * a);
        if (t < -kEpsilon || t > 1.0F + kEpsilon) {
            continue;
        }
        const auto impact = position + (displacement * t);
        const bool xOutside = corner.x == minX(collider)
            ? impact.x <= minX(collider) + kEpsilon
            : impact.x >= maxX(collider) - kEpsilon;
        const bool zOutside = corner.z == minZ(collider)
            ? impact.z <= minZ(collider) + kEpsilon
            : impact.z >= maxZ(collider) - kEpsilon;
        if (!xOutside || !zOutside) {
            continue;
        }
        const auto normal = normalizeOrZero({impact.x - corner.x, 0.0F, impact.z - corner.z});
        considerSweepHit(best, makeCandidate(t, normal));
    }
    return best;
}

[[nodiscard]] SweepHitCandidate findEarliestSweepHit(
    const std::vector<StaticCollider>& colliders,
    math::Vec3 boundsHalfExtents,
    const CharacterSweepQuery& query,
    math::Vec3 position,
    math::Vec3 displacement) {
    SweepHitCandidate best{};
    best.fraction = 1.0F;
    const auto end = position + displacement;
    for (const auto& collider : colliders) {
        if (!collider.blocksMovement) {
            continue;
        }

        if (isBlockingSideSurface(collider.kind) && verticalSweepRangeOverlaps(position, end, query.height, collider)) {
            auto candidate = sweepCircleAgainstAabb2D(position, displacement, collider, query.radius + query.skinWidth);
            if (candidate.hit) {
                const auto impact = position + (displacement * candidate.fraction);
                if (!verticalRangesOverlap(impact.y, query.height, collider)) {
                    candidate.hit = false;
                }
            }
            considerSweepHit(best, std::move(candidate));
        }

        if (isGroundSurface(collider.kind)) {
            const auto normal = (collider.kind == SurfaceKind::Ramp || collider.kind == SurfaceKind::Slide)
                ? rampNormal(collider)
                : math::Vec3{0.0F, 1.0F, 0.0F};
            const float surfaceStart = surfaceHeightAt(collider, position);
            const float surfaceEnd = surfaceHeightAt(collider, end);
            const float relativeStart = position.y - surfaceStart;
            const float relativeDelta = displacement.y - (surfaceEnd - surfaceStart);
            if (normal.y >= query.walkableSlopeCosine && relativeStart > 0.0001F && relativeDelta < -0.00001F) {
                const float t = -relativeStart / relativeDelta;
                const auto impact = position + (displacement * t);
                if (t >= 0.0F && t <= 1.0F &&
                    horizontalCircleOverlapsAabb(impact, collider, query.radius + query.skinWidth)) {
                    SweepHitCandidate ground{};
                    ground.hit = true;
                    ground.fraction = t;
                    ground.normal = normal;
                    ground.collider = &collider;
                    ground.colliderId = collider.id;
                    ground.kind = collider.kind;
                    ground.surfaceVelocity = collider.velocity;
                    ground.walkable = true;
                    considerSweepHit(best, std::move(ground));
                }
            }
        }

        if (displacement.y > 0.00001F) {
            const float headStart = position.y + query.height;
            const float ceiling = minY(collider);
            const float t = (ceiling - headStart) / displacement.y;
            const auto impact = position + (displacement * t);
            if (t >= 0.0F && t <= 1.0F && headStart <= ceiling + 0.0001F &&
                horizontalCircleOverlapsAabb(impact, collider, query.radius + query.skinWidth)) {
                SweepHitCandidate ceilingHit{};
                ceilingHit.hit = true;
                ceilingHit.fraction = t;
                ceilingHit.normal = {0.0F, -1.0F, 0.0F};
                ceilingHit.collider = &collider;
                ceilingHit.colliderId = collider.id;
                ceilingHit.kind = collider.kind;
                ceilingHit.surfaceVelocity = collider.velocity;
                considerSweepHit(best, std::move(ceilingHit));
            }
        }
    }

    if (displacement.y < -0.00001F && position.y > 0.0001F) {
        const float t = -position.y / displacement.y;
        const auto impact = position + (displacement * t);
        if (t >= 0.0F && t <= 1.0F &&
            std::abs(impact.x) <= boundsHalfExtents.x - query.radius &&
            std::abs(impact.z) <= boundsHalfExtents.z - query.radius) {
            SweepHitCandidate floor{};
            floor.hit = true;
            floor.fraction = t;
            floor.normal = {0.0F, 1.0F, 0.0F};
            floor.colliderId = "floor_main";
            floor.kind = SurfaceKind::Floor;
            floor.walkable = true;
            considerSweepHit(best, std::move(floor));
        }
    }

    const float minAllowedX = -boundsHalfExtents.x + query.radius + query.skinWidth;
    const float maxAllowedX = boundsHalfExtents.x - query.radius - query.skinWidth;
    const float minAllowedZ = -boundsHalfExtents.z + query.radius + query.skinWidth;
    const float maxAllowedZ = boundsHalfExtents.z - query.radius - query.skinWidth;
    const auto considerBound = [&](float t, math::Vec3 normal) {
        SweepHitCandidate bounds{};
        if (t < 0.0F || t > 1.0F) {
            return;
        }
        bounds.hit = true;
        bounds.fraction = t;
        bounds.normal = normal;
        bounds.colliderId = "world_bounds";
        bounds.kind = SurfaceKind::Wall;
        considerSweepHit(best, std::move(bounds));
    };
    if (displacement.x > 0.00001F && end.x > maxAllowedX) {
        considerBound((maxAllowedX - position.x) / displacement.x, {-1.0F, 0.0F, 0.0F});
    } else if (displacement.x < -0.00001F && end.x < minAllowedX) {
        considerBound((minAllowedX - position.x) / displacement.x, {1.0F, 0.0F, 0.0F});
    }
    if (displacement.z > 0.00001F && end.z > maxAllowedZ) {
        considerBound((maxAllowedZ - position.z) / displacement.z, {0.0F, 0.0F, -1.0F});
    } else if (displacement.z < -0.00001F && end.z < minAllowedZ) {
        considerBound((minAllowedZ - position.z) / displacement.z, {0.0F, 0.0F, 1.0F});
    }
    return best;
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
    query.wallProbeDistance = std::max(0.0F, query.wallProbeDistance);
    query.skinWidth = std::clamp(query.skinWidth, 0.0001F, std::max(0.0001F, query.radius * 0.25F));
    query.maxDepenetrationIterations = std::clamp(query.maxDepenetrationIterations, 1, 16);

    CharacterResolveResult result{};
    result.position = query.position;

    if (result.position.y <= 0.0F) {
        const auto before = result.position;
        result.position.y = 0.0F;
        result.groundHeight = 0.0F;
        result.groundNormal = {0.0F, 1.0F, 0.0F};
        result.grounded = true;
        result.groundColliderId = "floor_main";
        result.groundKind = SurfaceKind::Floor;
        result.correction = result.correction + (result.position - before);
    }

    resolveBounds(result, boundsHalfExtents_, query.radius);
    resolveGroundSurfaces(result, boundsHalfExtents_, staticColliders_, query);
    resolveSidePenetrations(result, staticColliders_, query);
    clearGroundState(result);
    resolveGroundSurfaces(result, boundsHalfExtents_, staticColliders_, query);

    const auto wall = probeWall(WallProbe{result.position, query.radius, query.height, query.wallProbeDistance});
    if (wall.hit) {
        if (const auto* collider = findStaticCollider(wall.colliderId)) {
            recordWallContact(result, *collider, wall.normal, wall.distance);
        }
    }

    std::stable_sort(
        result.contacts.begin(),
        result.contacts.end(),
        [](const CharacterContact& lhs, const CharacterContact& rhs) {
            if (lhs.role != rhs.role) {
                return lhs.role < rhs.role;
            }
            return lhs.colliderId < rhs.colliderId;
        });

    return result;
}

CharacterSweepResult PhysicsWorld::sweepCharacter(CharacterSweepQuery query) const {
    query.radius = std::max(0.01F, query.radius);
    query.height = std::max(0.01F, query.height);
    query.maxStepHeight = std::max(0.0F, query.maxStepHeight);
    query.snapDownDistance = std::max(0.0F, query.snapDownDistance);
    query.walkableSlopeCosine = std::clamp(query.walkableSlopeCosine, 0.0F, 1.0F);
    query.wallProbeDistance = std::max(0.0F, query.wallProbeDistance);
    query.maxIterations = std::clamp(query.maxIterations, 1, 8);
    query.skinWidth = std::clamp(query.skinWidth, 0.0001F, std::max(0.0001F, query.radius * 0.25F));
    query.maxDepenetrationIterations = std::clamp(query.maxDepenetrationIterations, 1, 16);

    CharacterSweepResult result{};
    result.startPosition = query.startPosition;
    result.desiredDisplacement = query.desiredDisplacement;

    auto startResolveQuery = characterQueryFromSweep(query, query.startPosition);
    startResolveQuery.enableGroundSnap = false;
    auto start = resolveCharacter(startResolveQuery);
    math::Vec3 position = start.position;
    math::Vec3 remaining = query.desiredDisplacement;
    math::Vec3 rejectedDisplacement{};
    const bool startedGrounded = start.grounded;
    result.startPosition = position;

    for (int iteration = 0; iteration < query.maxIterations; ++iteration) {
        if (remaining.lengthSquared() <= 0.00000001F) {
            position = position + remaining;
            remaining = {};
            break;
        }

        auto hit = findEarliestSweepHit(staticColliders_, boundsHalfExtents_, query, position, remaining);
        if (!hit.hit) {
            position = position + remaining;
            remaining = {};
            result.iterationCount = static_cast<std::size_t>(iteration + 1);
            break;
        }

        const auto contactPosition = position + (remaining * hit.fraction);
        if (startedGrounded && hit.collider != nullptr && std::abs(hit.normal.y) <= 0.0001F &&
            canStepOntoColliderDuringSweep(*hit.collider, query, contactPosition)) {
            const float stepHeight = maxY(*hit.collider) - position.y;
            auto raisedPosition = contactPosition;
            raisedPosition.y = maxY(*hit.collider) + query.skinWidth;
            if (capsuleHasStepClearance(staticColliders_, *hit.collider, raisedPosition, query)) {
                position = raisedPosition;
                remaining = remaining * (1.0F - hit.fraction);
                remaining.y = std::max(0.0F, remaining.y);
                result.stepped = true;
                result.stepHeight = std::max(result.stepHeight, stepHeight);
                result.iterationCount = static_cast<std::size_t>(iteration + 1);
                continue;
            }
        }

        const float travelLength = std::max(0.001F, std::sqrt(remaining.lengthSquared()));
        const float safetyFraction = std::min(0.05F, 0.003F / travelLength);
        const float safeFraction = std::max(0.0F, hit.fraction - safetyFraction);
        position = position + (remaining * safeFraction);
        result.swept = true;
        result.hit = true;
        if (result.hitColliderId.empty()) {
            result.firstHitFraction = hit.fraction;
            result.hitNormal = hit.normal;
            result.hitColliderId = hit.colliderId;
            result.hitKind = hit.kind;
        }
        result.sweepContacts.push_back(CharacterContact{
            hit.colliderId,
            hit.kind,
            CharacterContactRole::Sweep,
            position,
            hit.normal,
            0.0F,
            hit.fraction,
            0.0F,
            true,
            hit.walkable,
            hit.surfaceVelocity,
        });

        remaining = remaining * (1.0F - safeFraction);
        const float intoSurface = (remaining.x * hit.normal.x) +
            (remaining.y * hit.normal.y) +
            (remaining.z * hit.normal.z);
        if (intoSurface < 0.0F) {
            const auto rejected = hit.normal * intoSurface;
            rejectedDisplacement = rejectedDisplacement + rejected;
            remaining = remaining - rejected;
        }
        result.iterationCount = static_cast<std::size_t>(iteration + 1);
    }

    result.remainingDisplacement = remaining + rejectedDisplacement;
    auto resolveQuery = characterQueryFromSweep(query, position);
    result.resolve = resolveCharacter(resolveQuery);
    result.resolve.stepped = result.resolve.stepped || result.stepped;
    result.resolve.stepHeight = std::max(result.resolve.stepHeight, result.stepHeight);
    result.appliedDisplacement = result.resolve.position - result.startPosition;
    for (const auto& contact : result.sweepContacts) {
        appendContact(result.resolve, contact);
    }

    if (result.hit) {
        const auto requestedEnd = result.startPosition + query.desiredDisplacement;
        const auto correction = result.resolve.position - requestedEnd;
        if (correction.lengthSquared() > 0.000001F) {
            result.resolve.correction = result.resolve.correction + correction;
            result.resolve.blocked = true;
            ++result.resolve.hitCount;
            if (result.resolve.lastColliderId.empty()) {
                result.resolve.lastColliderId = result.hitColliderId;
            }
        }
        if (result.resolve.wallColliderId.empty() &&
            !result.hitColliderId.empty() &&
            std::abs(result.hitNormal.y) <= 0.25F) {
            result.resolve.wallColliderId = result.hitColliderId;
            result.resolve.wallNormal = result.hitNormal;
            result.resolve.wallTangent = tangentForWallNormal(result.hitNormal);
            result.resolve.wallKind = result.hitKind;
            result.resolve.nearWallRunSurface = result.hitKind == SurfaceKind::WallRun;
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
        if (!best.hit || candidate.distance < best.distance - 0.00001F ||
            (std::abs(candidate.distance - best.distance) <= 0.00001F && candidate.colliderId < best.colliderId)) {
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
        if (!best.hit || candidate.distance < best.distance - 0.00001F ||
            (std::abs(candidate.distance - best.distance) <= 0.00001F && candidate.colliderId < best.colliderId)) {
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

const char* contactRoleName(CharacterContactRole role) {
    switch (role) {
    case CharacterContactRole::Ground:
        return "ground";
    case CharacterContactRole::Step:
        return "step";
    case CharacterContactRole::Wall:
        return "wall";
    case CharacterContactRole::Bounds:
        return "bounds";
    case CharacterContactRole::Sweep:
        return "sweep";
    }
    return "unknown";
}

} // namespace novacore::physics
