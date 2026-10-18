#include "novacore/physics/RigidBodyWorld.hpp"
#include "novacore/physics/CharacterController.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <tuple>
#include <utility>

#if NOVACORE_HAS_JOLT
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Geometry/AABox.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
static_assert(JPH_VERSION_MAJOR == 5 && JPH_VERSION_MINOR == 6 && JPH_VERSION_PATCH == 0,
    "NovaCore's adoption gate requires the audited Jolt 5.6.0");
#endif

namespace novacore::physics {
namespace {
[[nodiscard]] bool finite(math::Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
[[nodiscard]] bool validRotation(math::Quat q) {
    const float n = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    return std::isfinite(n) && std::abs(n - 1.0F) < 0.002F;
}
[[nodiscard]] bool bounded(math::Vec3 v, float maximum) {
    return finite(v) && std::abs(v.x) <= maximum && std::abs(v.y) <= maximum && std::abs(v.z) <= maximum;
}
[[nodiscard]] bool speedBounded(math::Vec3 v, float maximum) {
    return bounded(v,maximum) && v.lengthSquared() <= maximum*maximum;
}
#if NOVACORE_HAS_JOLT
[[nodiscard]] bool equal(math::Vec3 a, math::Vec3 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
#endif
[[nodiscard]] bool validate(const RigidBodyDesc& d, std::string& error) {
    if (d.isSensor && d.motion != RigidBodyMotion::Kinematic) {
        error = "Sensors require active kinematic motion";
        return false;
    }
    if (d.id.empty() || d.id.size() > 128 || !bounded(d.position, 100000.0F) ||
        !validRotation(d.rotation) || !speedBounded(d.linearVelocity, 500.0F) ||
        !speedBounded(d.angularVelocity, 100.0F) || d.filter.layer >= 8 ||
        !std::isfinite(d.massKg) || d.massKg < 0.001F || d.massKg > 100000.0F ||
        !std::isfinite(d.friction) || d.friction < 0.0F || d.friction > 2.0F ||
        !std::isfinite(d.restitution) || d.restitution < 0.0F || d.restitution > 1.0F ||
        !std::isfinite(d.linearDamping) || d.linearDamping < 0.0F || d.linearDamping > 10.0F ||
        !std::isfinite(d.angularDamping) || d.angularDamping < 0.0F || d.angularDamping > 10.0F) {
        error = "Invalid rigid-body identity, transform, velocity, filter or material";
        return false;
    }
    switch (d.shape) {
    case RigidBodyShape::Box:
        if (!bounded(d.halfExtents, 10000.0F) || d.halfExtents.x < 0.001F ||
            d.halfExtents.y < 0.001F || d.halfExtents.z < 0.001F) break;
        return true;
    case RigidBodyShape::Sphere:
        if (!std::isfinite(d.radius) || d.radius < 0.001F || d.radius > 10000.0F) break;
        return true;
    case RigidBodyShape::Capsule:
        if (!std::isfinite(d.radius) || d.radius < 0.001F || d.radius > 10000.0F ||
            !std::isfinite(d.capsuleHalfHeight) || d.capsuleHalfHeight <= 0.0F ||
            d.capsuleHalfHeight > 10000.0F) break;
        return true;
    }
    error = "Invalid rigid-body shape dimensions";
    return false;
}
RigidBodyDesc queryDescription(const RigidBodyQueryShape& shape) {
    RigidBodyDesc desc;
    desc.id = "query";
    desc.shape = shape.shape; desc.position = shape.position; desc.rotation = shape.rotation;
    desc.halfExtents = shape.halfExtents; desc.radius = shape.radius; desc.capsuleHalfHeight = shape.capsuleHalfHeight;
    return desc;
}
} // namespace

#if NOVACORE_HAS_JOLT
namespace {
std::atomic<std::uint64_t> nextWorld{1};
struct RuntimeState final { std::mutex mutex; unsigned users = 0; };
RuntimeState& runtimeState() { static RuntimeState value; return value; }
class RuntimeLease final {
public:
    bool acquire(std::string& error) {
        auto& state = runtimeState();
        const std::lock_guard lock(state.mutex);
        if (state.users == 0) {
            if (!JPH::VerifyJoltVersionID()) {
                error = "Jolt header/library ABI mismatch";
                return false;
            }
            if (JPH::Factory::sInstance) {
                error = "Jolt was initialized outside NovaCore; competing global ownership is unsupported";
                return false;
            }
            JPH::RegisterDefaultAllocator();
            JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();
        }
        ++state.users;
        active_ = true;
        return true;
    }
    ~RuntimeLease() {
        if (!active_) return;
        auto& state = runtimeState();
        const std::lock_guard lock(state.mutex);
        if (--state.users == 0) {
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }
    }
private:
    bool active_ = false;
};

JPH::Vec3 vec(math::Vec3 v) { return {v.x, v.y, v.z}; }
math::Vec3 vec(JPH::Vec3Arg v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
JPH::Quat quat(math::Quat q) { return JPH::Quat(q.x,q.y,q.z,q.w).Normalized(); }
math::Quat quat(JPH::QuatArg q) { return {q.GetX(),q.GetY(),q.GetZ(),q.GetW()}; }
unsigned category(JPH::ObjectLayer layer) { return (layer >> 1U) & 7U; }
unsigned mask(JPH::ObjectLayer layer) { return (layer >> 4U) & 255U; }
JPH::ObjectLayer layerFor(const RigidBodyDesc& d) {
    return static_cast<JPH::ObjectLayer>((d.motion != RigidBodyMotion::Static ? 1U : 0U) |
        (static_cast<unsigned>(d.filter.layer) << 1U) | (static_cast<unsigned>(d.filter.mask) << 4U));
}
class BroadLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return JPH::BroadPhaseLayer(static_cast<JPH::uint8>(layer & 1U));
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return static_cast<JPH::BroadPhaseLayer::Type>(layer) == 0 ? "Static" : "Moving";
    }
#endif
};
class BroadFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::BroadPhaseLayer b) const override {
        return (a & 1U) != 0 || static_cast<JPH::BroadPhaseLayer::Type>(b) != 0;
    }
};
class PairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return ((a|b) & 1U) != 0 && (mask(a) & (1U << category(b))) != 0 &&
            (mask(b) & (1U << category(a))) != 0;
    }
};
class RayFilter final : public JPH::ObjectLayerFilter {
public:
    explicit RayFilter(std::uint8_t value) : mask_(value) {}
    bool ShouldCollide(JPH::ObjectLayer layer) const override { return (mask_ & (1U << category(layer))) != 0; }
private:
    unsigned mask_;
};
class RayBodyFilter final : public JPH::BodyFilter {
public:
    explicit RayBodyFilter(bool includeSensors) : includeSensors_(includeSensors) {}
    bool ShouldCollideLocked(const JPH::Body& body) const override { return includeSensors_ || !body.IsSensor(); }
private:
    bool includeSensors_;
};

// Jolt invokes OnBody/OnBodyEnd around every target shape. Keep one deepest
// native manifold per body and reserve the entire result bound before traversal.
// No world mutation, locking interface call, or application callback occurs here.
class BoundedOverlapCollector final : public JPH::CollideShapeCollector {
public:
    explicit BoundedOverlapCollector(std::uint32_t maximum) : maximum_(maximum) { hits.reserve(maximum); }
    void OnBody(const JPH::Body&) override { current_.reset(); }
    void AddHit(const JPH::CollideShapeResult& hit) override {
        if (!current_ || hit.mPenetrationDepth > current_->mPenetrationDepth) current_ = hit;
    }
    void OnBodyEnd() override {
        if (!current_) return;
        if (hits.size() == maximum_) { overflow = true; ForceEarlyOut(); return; }
        hits.push_back(*current_);
    }
    std::vector<JPH::CollideShapeResult> hits;
    bool overflow = false;
private:
    std::uint32_t maximum_;
    std::optional<JPH::CollideShapeResult> current_;
};

bool endpointLess(const RigidBodyContactEndpoint& a, const RigidBodyContactEndpoint& b) {
    return std::tie(a.id,a.handle.world,a.handle.index,a.handle.generation) <
        std::tie(b.id,b.handle.world,b.handle.index,b.handle.generation);
}
template<class Pair> bool pairLess(const Pair& a, const Pair& b) {
    if (endpointLess(a.first,b.first)) return true;
    if (endpointLess(b.first,a.first)) return false;
    return endpointLess(a.second,b.second);
}
template<class Pair> bool samePair(const Pair& a, const Pair& b) {
    return a.first.handle == b.first.handle && a.second.handle == b.second.handle;
}

JPH::Shape::ShapeResult makeShape(const RigidBodyDesc& desc) {
    switch (desc.shape) {
    case RigidBodyShape::Box:
        return JPH::BoxShapeSettings(vec(desc.halfExtents), 0.0F).Create();
    case RigidBodyShape::Sphere:
        return JPH::SphereShapeSettings(desc.radius).Create();
    case RigidBodyShape::Capsule:
        return JPH::CapsuleShapeSettings(desc.capsuleHalfHeight, desc.radius).Create();
    }
    return {};
}
JPH::Shape::ShapeResult makeRamp(const StaticCollider& collider) {
    const auto h = collider.halfExtents;
    JPH::Array<JPH::Vec3> points;
    // Same linear top surface as CharacterController::rampHeightAt, over a
    // solid wedge. Convex radius zero keeps authored planes at exact positions.
    for (int x : {-1,1}) for (int z : {-1,1}) {
        points.emplace_back(x*h.x, -h.y, z*h.z);
        bool high = false;
        switch (collider.rampDirection) {
        case RampDirection::PositiveX: high = x > 0; break;
        case RampDirection::NegativeX: high = x < 0; break;
        case RampDirection::PositiveZ: high = z > 0; break;
        case RampDirection::NegativeZ: high = z < 0; break;
        case RampDirection::None: break;
        }
        if (high) points.emplace_back(x*h.x, h.y, z*h.z);
    }
    return JPH::ConvexHullShapeSettings(points, 0.0F).Create();
}
} // namespace

class RigidBodyWorld::Impl final : public JPH::ContactListener {
public:
    struct Slot final {
        std::uint32_t generation = 1;
        JPH::BodyID native{};
        RigidBodyDesc desc{};
        bool canonical = false;
        bool kinematicTargetPending = false;
        StaticCollider source{};
    };
    RuntimeLease runtime; // Last destroyed: all bodies/shapes must go first.
    BroadLayers broadLayers;
    BroadFilter broadFilter;
    PairFilter pairFilter;
    std::unique_ptr<JPH::PhysicsSystem> system;
    std::unique_ptr<JPH::TempAllocatorImplWithMallocFallback> allocator;
    std::unique_ptr<JPH::JobSystemSingleThreaded> jobs;
    RigidBodyWorldConfig config{};
    std::uint64_t owner = nextWorld.fetch_add(1);
    std::uint64_t tick = 0;
    std::vector<Slot> slots;
    std::map<std::string,std::size_t,std::less<>> byId;
    struct RawContact final {
        RigidBodyHandle first{}, second{};
        math::Vec3 pointOnFirst{}, pointOnSecond{}, normal{};
        float depth = 0;
    };
    std::vector<RawContact> rawContacts; // Sized once; callbacks never allocate.
    std::size_t rawCount = 0;
    bool captureOverflow = false;
    std::vector<RigidBodyContact> sensorPairs;
    std::vector<RigidBodySensorEvent> pendingExits;

    ~Impl() {
        if (system) system->SetContactListener(nullptr);
        if (system) for (auto& slot : slots) if (!slot.native.IsInvalid()) {
            system->GetBodyInterface().RemoveBody(slot.native);
            system->GetBodyInterface().DestroyBody(slot.native);
        }
    }
    RigidBodyHandle handle(std::size_t index) const {
        return {owner, static_cast<std::uint32_t>(index), slots[index].generation};
    }
    Slot* find(RigidBodyHandle handle) {
        if (handle.world != owner || handle.index >= slots.size()) return nullptr;
        auto& slot = slots[handle.index];
        return slot.generation == handle.generation && !slot.native.IsInvalid() ? &slot : nullptr;
    }
    RigidBodyContactEndpoint endpoint(std::size_t index) const {
        const auto& slot = slots[index];
        return {handle(index),slot.desc.id,slot.desc.materialId,slot.desc.surfaceId,slot.desc.isSensor,slot.canonical};
    }
    std::optional<RigidBodyShapeHit> shapeHit(const JPH::CollideShapeResult& hit, math::Vec3 baseOffset,
                                             float fraction, bool initialOverlap) const {
        const auto index = static_cast<std::size_t>(system->GetBodyInterface().GetUserData(hit.mBodyID2));
        if (index >= slots.size() || slots[index].native != hit.mBodyID2) return {};
        return RigidBodyShapeHit{endpoint(index),baseOffset+vec(hit.mContactPointOn1),baseOffset+vec(hit.mContactPointOn2),
            vec(-hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sZero())),fraction,hit.mPenetrationDepth,initialOverlap};
    }
    void capture(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold) {
        // SingleThreaded jobs only. Bodies are locked: read callback values,
        // never call the locking interface, allocate, or dispatch game code.
        if (rawCount == rawContacts.size()) { captureOverflow = true; return; }
        const auto first = static_cast<std::size_t>(a.GetUserData());
        const auto second = static_cast<std::size_t>(b.GetUserData());
        if (first >= slots.size() || second >= slots.size() || slots[first].native != a.GetID() || slots[second].native != b.GetID()) {
            captureOverflow = true; return;
        }
        auto& record = rawContacts[rawCount++];
        record = {handle(first),handle(second),{}, {},vec(manifold.mWorldSpaceNormal),manifold.mPenetrationDepth};
        if (!manifold.mRelativeContactPointsOn1.empty() && !manifold.mRelativeContactPointsOn2.empty()) {
            record.pointOnFirst = vec(manifold.GetWorldSpaceContactPointOn1(0));
            record.pointOnSecond = vec(manifold.GetWorldSpaceContactPointOn2(0));
        }
    }
    void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& m, JPH::ContactSettings&) override { capture(a,b,m); }
    void OnContactPersisted(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& m, JPH::ContactSettings&) override { capture(a,b,m); }
    void OnContactRemoved(const JPH::SubShapeIDPair&) override {
        // Never dereference possibly destroyed bodies. Active kinematic sensors
        // report their complete current pairs every update; diff that owned set
        // after Update so removal of just one subshape cannot emit a false Exit.
    }
    bool extract(RigidBodyStepResult& result, std::uint64_t nextTick) {
        if (config.maxContactEvents == 0) return true;
        if (captureOverflow) return false;
        std::vector<RigidBodyContact> contacts;
        contacts.reserve(rawCount);
        for (std::size_t i=0; i<rawCount; ++i) {
            const auto& raw = rawContacts[i];
            if (!find(raw.first) || !find(raw.second)) return false;
            RigidBodyContact contact{nextTick,endpoint(raw.first.index),endpoint(raw.second.index),
                raw.pointOnFirst,raw.pointOnSecond,raw.normal,raw.depth};
            if (endpointLess(contact.second,contact.first)) {
                std::swap(contact.first,contact.second);
                std::swap(contact.pointOnFirst,contact.pointOnSecond);
                contact.normal = contact.normal * -1.0F;
            }
            contacts.push_back(std::move(contact));
        }
        std::sort(contacts.begin(),contacts.end(),[](const auto& a,const auto& b) {
            if (pairLess(a,b)) return true;
            if (pairLess(b,a)) return false;
            // Keep deepest manifold, with a stable tie break for CCD repeats.
            return std::tie(a.penetrationDepth,a.pointOnFirst.x,a.pointOnFirst.y,a.pointOnFirst.z,
                a.pointOnSecond.x,a.pointOnSecond.y,a.pointOnSecond.z,a.normal.x,a.normal.y,a.normal.z) >
                std::tie(b.penetrationDepth,b.pointOnFirst.x,b.pointOnFirst.y,b.pointOnFirst.z,
                b.pointOnSecond.x,b.pointOnSecond.y,b.pointOnSecond.z,b.normal.x,b.normal.y,b.normal.z);
        });
        contacts.erase(std::unique(contacts.begin(),contacts.end(),[](const auto& a,const auto& b) { return samePair(a,b); }),contacts.end());
        std::vector<RigidBodyContact> currentSensors;
        for (const auto& contact : contacts) if (contact.first.isSensor || contact.second.isSensor) currentSensors.push_back(contact);
        auto events = pendingExits;
        for (auto& event : events) event.tick = nextTick;
        std::size_t old = 0, current = 0;
        while (old < sensorPairs.size() || current < currentSensors.size()) {
            RigidBodySensorEvent event; event.tick = nextTick;
            if (current == currentSensors.size() || (old < sensorPairs.size() && pairLess(sensorPairs[old],currentSensors[current]))) {
                const auto& previous = sensorPairs[old++];
                event.first = previous.first; event.second = previous.second; event.phase = RigidBodySensorPhase::Exit;
            } else {
                const auto& contact = currentSensors[current++];
                event.first = contact.first; event.second = contact.second;
                if (old < sensorPairs.size() && samePair(sensorPairs[old],contact)) { ++old; event.phase = RigidBodySensorPhase::Stay; }
            }
            if (events.size() == config.maxContactEvents) return false;
            events.push_back(std::move(event));
        }
        std::sort(events.begin(),events.end(),[](const auto& a,const auto& b) { return pairLess(a,b); });
        sensorPairs = std::move(currentSensors);
        pendingExits.clear();
        result.contacts = std::move(contacts);
        result.sensorEvents = std::move(events);
        return true;
    }
    RigidBodyHandle create(const RigidBodyDesc& desc, const JPH::Shape* shape, bool canonical, std::string& error) {
        if (byId.contains(desc.id) || byId.size() >= config.maxBodies) {
            error = "Duplicate rigid-body ID or world capacity exhausted";
            return {};
        }
        if (desc.isSensor && config.maxContactEvents == 0) { error = "Sensors require contact capture capacity"; return {}; }
        JPH::EMotionType motion = JPH::EMotionType::Static;
        if (desc.motion == RigidBodyMotion::Dynamic) motion = JPH::EMotionType::Dynamic;
        if (desc.motion == RigidBodyMotion::Kinematic) motion = JPH::EMotionType::Kinematic;
        JPH::BodyCreationSettings settings(shape, vec(desc.position), quat(desc.rotation), motion, layerFor(desc));
        settings.mLinearVelocity = vec(desc.linearVelocity);
        settings.mAngularVelocity = vec(desc.angularVelocity);
        settings.mFriction = desc.friction;
        settings.mRestitution = desc.restitution;
        settings.mLinearDamping = desc.linearDamping;
        settings.mAngularDamping = desc.angularDamping;
        settings.mMaxAngularVelocity = 100.0F;
        settings.mMotionQuality = desc.continuousCollision && !desc.isSensor ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
        settings.mAllowSleeping = desc.allowSleeping && !desc.isSensor;
        settings.mIsSensor = desc.isSensor;
        settings.mCollideKinematicVsNonDynamic = desc.isSensor;
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = desc.massKg;
        std::size_t index = 0;
        while (index < slots.size() && !slots[index].native.IsInvalid()) ++index;
        settings.mUserData = index;
        const auto native = system->GetBodyInterface().CreateAndAddBody(settings,
            motion == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
        if (native.IsInvalid()) { error = "Jolt body creation failed"; return {}; }
        if (index == slots.size()) slots.emplace_back();
        auto& slot = slots[index];
        slot.native = native;
        slot.desc = desc;
        slot.canonical = canonical;
        slot.kinematicTargetPending = false;
        byId.emplace(desc.id,index);
        return handle(index);
    }
    void remove(std::size_t index, RigidBodySensorExitReason reason = RigidBodySensorExitReason::BodyRemoved) {
        auto& slot = slots[index];
        const auto removedHandle = handle(index);
        for (auto it = sensorPairs.begin(); it != sensorPairs.end();) {
            if (it->first.handle != removedHandle && it->second.handle != removedHandle) { ++it; continue; }
            auto first = it->first, second = it->second;
            if (find(first.handle)) first = endpoint(first.handle.index);
            if (find(second.handle)) second = endpoint(second.handle.index);
            if (pendingExits.size() == config.maxContactEvents) captureOverflow = true;
            else pendingExits.push_back({0,std::move(first),std::move(second),RigidBodySensorPhase::Exit,reason});
            it = sensorPairs.erase(it);
        }
        std::optional<JPH::AABox> bounds;
        {
            const JPH::BodyLockRead lock(system->GetBodyLockInterface(),slot.native);
            if (lock.Succeeded()) bounds = lock.GetBody().GetWorldSpaceBounds();
        }
        if (bounds) {
            bounds->ExpandBy(JPH::Vec3::sReplicate(0.05F));
            system->GetBodyInterface().ActivateBodiesInAABox(*bounds,
                JPH::SpecifiedBroadPhaseLayerFilter(JPH::BroadPhaseLayer(1)),{});
        }
        system->GetBodyInterface().RemoveBody(slot.native);
        system->GetBodyInterface().DestroyBody(slot.native);
        byId.erase(slot.desc.id);
        slot.native = JPH::BodyID();
        // 32-bit engine generation, independent of Jolt's 8-bit sequence field.
        if (++slot.generation == 0) ++slot.generation;
    }
    RigidBodySnapshot snapshot(std::size_t index) const {
        const auto& slot = slots[index];
        const auto& api = system->GetBodyInterface();
        return {handle(index), slot.desc.id, slot.desc.motion,
            vec(api.GetPosition(slot.native)), quat(api.GetRotation(slot.native)),
            vec(api.GetLinearVelocity(slot.native)), vec(api.GetAngularVelocity(slot.native)),
            slot.desc.motion == RigidBodyMotion::Dynamic && !api.IsActive(slot.native), slot.canonical,
            slot.desc.materialId,slot.desc.surfaceId,slot.desc.isSensor};
    }
    void wakeNear(const StaticCollider& collider) {
        const auto padding = math::Vec3{0.05F,0.05F,0.05F};
        const JPH::AABox bounds(vec(collider.center-collider.halfExtents-padding),
            vec(collider.center+collider.halfExtents+padding));
        system->GetBodyInterface().ActivateBodiesInAABox(bounds,
            JPH::SpecifiedBroadPhaseLayerFilter(JPH::BroadPhaseLayer(1)),{});
    }
    bool synchronize(const PhysicsWorld& world, RigidBodyStepResult& result) {
        struct Pending final { StaticCollider source; RigidBodyDesc desc; JPH::RefConst<JPH::Shape> shape; };
        std::map<std::string,Pending,std::less<>> sources;
        std::size_t applicationCount = 0;
        for (const auto& [id,index] : byId) {
            (void)id;
            if (!slots[index].canonical) ++applicationCount;
        }
        for (const auto& source : world.staticColliders()) {
            if (!source.blocksMovement || source.kind == SurfaceKind::Trigger) continue;
            RigidBodyDesc desc;
            desc.id = source.id;
            desc.motion = source.velocity.lengthSquared() > 0.000001F ? RigidBodyMotion::Kinematic : RigidBodyMotion::Static;
            desc.position = source.center;
            desc.halfExtents = source.halfExtents;
            desc.filter.layer = 0;
            if (!validate(desc,result.error) || !speedBounded(source.velocity,500.0F) || sources.contains(source.id) ||
                (byId.contains(source.id) && !slots[byId.at(source.id)].canonical)) {
                if (result.error.empty()) result.error = "Invalid or duplicate canonical collider";
                return false;
            }
            const auto previous = byId.find(source.id);
            if (previous != byId.end()) {
                desc.materialId = slots[previous->second].desc.materialId;
                desc.surfaceId = slots[previous->second].desc.surfaceId;
            }
            if (desc.motion == RigidBodyMotion::Kinematic && previous != byId.end() &&
                !speedBounded(source.center - slots[previous->second].source.center,500.0F*config.fixedDeltaSeconds)) {
                result.error = "Canonical kinematic displacement exceeds fixed-tick speed bound; reset for teleports";
                return false;
            }
            if (source.rampDirection != RampDirection::None && source.rampDirection != RampDirection::PositiveX &&
                source.rampDirection != RampDirection::NegativeX && source.rampDirection != RampDirection::PositiveZ &&
                source.rampDirection != RampDirection::NegativeZ) {
                result.error = "Invalid canonical ramp direction";
                return false;
            }
            sources.emplace(source.id,Pending{source,desc,{}});
        }
        if (sources.size() + applicationCount > config.maxBodies) {
            result.error = "Canonical collider synchronization exceeds body capacity";
            return false;
        }
        // Validate/create changed shapes before mutating the live world.
        for (auto& [id,pending] : sources) {
            const auto existing = byId.find(id);
            const bool changedShape = existing == byId.end() ||
                !equal(slots[existing->second].source.halfExtents,pending.source.halfExtents) ||
                slots[existing->second].source.rampDirection != pending.source.rampDirection ||
                slots[existing->second].desc.motion != pending.desc.motion;
            if (changedShape) {
                const auto shape = pending.source.rampDirection == RampDirection::None ? makeShape(pending.desc) : makeRamp(pending.source);
                if (shape.HasError()) { result.error = shape.GetError().c_str(); return false; }
                pending.shape = shape.Get();
            }
        }
        std::vector<std::size_t> removed;
        for (const auto& [id,index] : byId) if (slots[index].canonical && !sources.contains(id)) removed.push_back(index);
        for (const auto index : removed) { remove(index); ++result.staticBodiesRemoved; }
        for (auto& [id,pending] : sources) {
            auto existing = byId.find(id);
            if (pending.shape) {
                if (existing != byId.end()) { remove(existing->second,RigidBodySensorExitReason::ColliderReplaced); ++result.staticBodiesRemoved; }
                const auto created = create(pending.desc,pending.shape,true,result.error);
                if (!created.valid()) return false;
                slots[created.index].source = pending.source;
                wakeNear(pending.source);
                ++result.staticBodiesCreated;
            } else {
                auto& slot = slots[existing->second];
                if (!equal(slot.source.center,pending.source.center) || !equal(slot.source.velocity,pending.source.velocity)) {
                    wakeNear(slot.source);
                    wakeNear(pending.source);
                    ++result.staticBodiesUpdated;
                }
                if (slot.desc.motion == RigidBodyMotion::Kinematic)
                    system->GetBodyInterface().MoveKinematic(slot.native,vec(pending.source.center),JPH::Quat::sIdentity(),config.fixedDeltaSeconds);
                else if (!equal(slot.source.center,pending.source.center))
                    system->GetBodyInterface().SetPositionAndRotation(slot.native,vec(pending.source.center),JPH::Quat::sIdentity(),JPH::EActivation::DontActivate);
                slot.source = pending.source;
            }
        }
        return true;
    }
};
#else
class RigidBodyWorld::Impl final {};
#endif

RigidBodyWorld::RigidBodyWorld() = default;
RigidBodyWorld::~RigidBodyWorld() = default;
RigidBodyWorld::RigidBodyWorld(RigidBodyWorld&&) noexcept = default;
RigidBodyWorld& RigidBodyWorld::operator=(RigidBodyWorld&&) noexcept = default;
bool RigidBodyWorld::backendAvailable() { return NOVACORE_HAS_JOLT != 0; }
bool RigidBodyWorld::initialized() const { return impl_ != nullptr; }
void RigidBodyWorld::shutdown() { impl_.reset(); }

bool RigidBodyWorld::initialize(const RigidBodyWorldConfig& config, std::string& error) {
    error.clear();
    if (!std::isfinite(config.fixedDeltaSeconds) || config.fixedDeltaSeconds < 1.0F/240.0F ||
        config.fixedDeltaSeconds > 1.0F/15.0F || !bounded(config.gravity,1000.0F) ||
        config.maxBodies < 8 || config.maxBodies > 8192 ||
        !std::isfinite(config.contactSlopMeters) || config.contactSlopMeters < 0.0001F || config.contactSlopMeters > 0.02F ||
        config.maxContactEvents > 65536) {
        error = "Invalid rigid-body world configuration";
        return false;
    }
#if NOVACORE_HAS_JOLT
    auto candidate = std::make_unique<Impl>();
    if (!candidate->runtime.acquire(error)) return false;
    candidate->config = config;
    candidate->allocator = std::make_unique<JPH::TempAllocatorImplWithMallocFallback>(8U*1024U*1024U);
    candidate->jobs = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
    candidate->system = std::make_unique<JPH::PhysicsSystem>();
    candidate->system->Init(config.maxBodies,0,config.maxBodies*8U,config.maxBodies*8U,
        candidate->broadLayers,candidate->broadFilter,candidate->pairFilter);
    candidate->system->SetGravity(vec(config.gravity));
    auto settings = candidate->system->GetPhysicsSettings();
    settings.mPenetrationSlop = config.contactSlopMeters;
    candidate->system->SetPhysicsSettings(settings);
    if (config.maxContactEvents != 0) {
        candidate->rawContacts.resize(config.maxContactEvents);
        candidate->sensorPairs.reserve(config.maxContactEvents);
        candidate->pendingExits.reserve(config.maxContactEvents);
        candidate->system->SetContactListener(candidate.get());
    }
    impl_ = std::move(candidate);
    return true;
#else
    error = "NovaCore was built without NOVACORE_ENABLE_JOLT";
    return false;
#endif
}

RigidBodyHandle RigidBodyWorld::createBody(const RigidBodyDesc& desc, std::string& error) {
    error.clear();
    if (!validate(desc,error)) return {};
    if (desc.motion != RigidBodyMotion::Static && desc.motion != RigidBodyMotion::Kinematic && desc.motion != RigidBodyMotion::Dynamic) {
        error = "Invalid rigid-body motion mode"; return {};
    }
#if NOVACORE_HAS_JOLT
    if (impl_) {
        const auto shape = makeShape(desc);
        if (shape.HasError()) { error = shape.GetError().c_str(); return {}; }
        return impl_->create(desc,shape.Get(),false,error);
    }
#endif
    error = "Rigid-body world is not initialized";
    return {};
}
bool RigidBodyWorld::removeBody(RigidBodyHandle handle) {
#if NOVACORE_HAS_JOLT
    if (impl_) if (const auto* slot = impl_->find(handle); slot && !slot->canonical) {
        impl_->remove(handle.index); return true;
    }
#else
    (void)handle;
#endif
    return false;
}
std::optional<RigidBodySnapshot> RigidBodyWorld::body(RigidBodyHandle handle) const {
#if NOVACORE_HAS_JOLT
    if (impl_ && impl_->find(handle)) return impl_->snapshot(handle.index);
#else
    (void)handle;
#endif
    return {};
}
std::vector<RigidBodySnapshot> RigidBodyWorld::bodies() const {
    std::vector<RigidBodySnapshot> result;
#if NOVACORE_HAS_JOLT
    if (impl_) for (const auto& [id,index] : impl_->byId) { (void)id; result.push_back(impl_->snapshot(index)); }
#endif
    return result;
}
bool RigidBodyWorld::applyImpulse(RigidBodyHandle handle, math::Vec3 impulse, math::Vec3 worldPoint) {
    if (!bounded(impulse,100000.0F) || !bounded(worldPoint,100000.0F)) return false;
#if NOVACORE_HAS_JOLT
    if (impl_) if (const auto* slot = impl_->find(handle); slot && slot->desc.motion == RigidBodyMotion::Dynamic) {
        impl_->system->GetBodyInterface().AddImpulse(slot->native,vec(impulse),vec(worldPoint)); return true;
    }
#else
    (void)handle;
#endif
    return false;
}
bool RigidBodyWorld::moveKinematic(RigidBodyHandle handle, math::Vec3 target, math::Quat rotation) {
    if (!bounded(target,100000.0F) || !validRotation(rotation)) return false;
#if NOVACORE_HAS_JOLT
    if (impl_) if (auto* slot = impl_->find(handle); slot && !slot->canonical && slot->desc.motion == RigidBodyMotion::Kinematic) {
        const auto delta = target - vec(impl_->system->GetBodyInterface().GetPosition(slot->native));
        if (!speedBounded(delta,500.0F*impl_->config.fixedDeltaSeconds)) return false;
        const auto current = impl_->system->GetBodyInterface().GetRotation(slot->native);
        const auto requested = quat(rotation);
        const float angle = 2.0F*std::acos(std::clamp(std::abs(current.Dot(requested)),0.0F,1.0F));
        if (angle > 100.0F*impl_->config.fixedDeltaSeconds) return false;
        impl_->system->GetBodyInterface().MoveKinematic(slot->native,vec(target),quat(rotation),impl_->config.fixedDeltaSeconds);
        slot->kinematicTargetPending = true;
        return true;
    }
#else
    (void)handle;
#endif
    return false;
}
bool RigidBodyWorld::setBodyTags(RigidBodyHandle handle, std::uint32_t materialId, std::uint32_t surfaceId) {
#if NOVACORE_HAS_JOLT
    if (impl_) if (auto* slot = impl_->find(handle)) {
        slot->desc.materialId = materialId;
        slot->desc.surfaceId = surfaceId;
        return true;
    }
#else
    (void)handle; (void)materialId; (void)surfaceId;
#endif
    return false;
}
RigidBodyStepResult RigidBodyWorld::step(const PhysicsWorld& canonicalStatics, std::uint64_t tick) {
    RigidBodyStepResult result;
#if NOVACORE_HAS_JOLT
    if (impl_) {
        result.tick = impl_->tick;
        if (impl_->tick == std::numeric_limits<std::uint64_t>::max() || tick != impl_->tick + 1U) {
            result.error = "Rigid-body tick must be the next consecutive authoritative tick"; return result;
        }
        if (impl_->captureOverflow) {
            result.error = "Rigid-body contact capture capacity exceeded; world requires reset";
            shutdown(); return result;
        }
        if (!impl_->synchronize(canonicalStatics,result)) return result;
        for (auto& slot : impl_->slots) if (!slot.native.IsInvalid() && !slot.canonical && slot.desc.motion == RigidBodyMotion::Kinematic) {
            if (!slot.kinematicTargetPending) impl_->system->GetBodyInterface().SetLinearAndAngularVelocity(slot.native,JPH::Vec3::sZero(),JPH::Vec3::sZero());
            slot.kinematicTargetPending = false;
        }
        impl_->rawCount = 0;
        const auto updateError = impl_->system->Update(impl_->config.fixedDeltaSeconds,1,impl_->allocator.get(),impl_->jobs.get());
        if (updateError != JPH::EPhysicsUpdateError::None) {
            result.error = "Jolt update capacity exceeded; world requires reset";
            // Update may have partially advanced. Invalidate the world/handles;
            // retrying the same tick must not simulate it twice.
            shutdown();
            return result;
        }
        if (!impl_->extract(result,tick)) {
            result.error = "Rigid-body contact capture capacity exceeded; world requires reset";
            // No successful/partial event packet escapes a partly advanced tick.
            shutdown(); return result;
        }
        result.stepped = true;
        result.tick = ++impl_->tick;
        result.bodyCount = static_cast<std::uint32_t>(impl_->byId.size());
        for (const auto& [id,index] : impl_->byId) {
            (void)id;
            const auto& slot = impl_->slots[index];
            if (slot.desc.motion == RigidBodyMotion::Dynamic && impl_->system->GetBodyInterface().IsActive(slot.native)) ++result.activeDynamicCount;
        }
        return result;
    }
#else
    (void)canonicalStatics; (void)tick;
#endif
    result.error = "Rigid-body world is not initialized";
    return result;
}
std::optional<RigidBodyRayHit> RigidBodyWorld::raycast(math::Vec3 origin, math::Vec3 displacement, std::uint8_t layerMask, bool includeSensors) const {
    if (!bounded(origin,100000.0F) || !bounded(displacement,100000.0F) || displacement.lengthSquared() <= 1.0e-12F) return {};
#if NOVACORE_HAS_JOLT
    if (impl_) {
        JPH::RayCastResult hit;
        const JPH::RRayCast ray(vec(origin),vec(displacement));
        if (!impl_->system->GetNarrowPhaseQuery().CastRay(ray,hit,{},RayFilter(layerMask),RayBodyFilter(includeSensors))) return {};
        const auto index = static_cast<std::size_t>(impl_->system->GetBodyInterface().GetUserData(hit.mBodyID));
        if (index >= impl_->slots.size() || impl_->slots[index].native != hit.mBodyID) return {};
        const auto point = ray.GetPointOnRay(hit.mFraction);
        const JPH::BodyLockRead lock(impl_->system->GetBodyLockInterface(),hit.mBodyID);
        if (!lock.Succeeded()) return {};
        const auto& slot = impl_->slots[index];
        return RigidBodyRayHit{impl_->handle(index),slot.desc.id,vec(point),
            vec(lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2,point)),hit.mFraction,slot.canonical,
            slot.desc.materialId,slot.desc.surfaceId,slot.desc.isSensor};
    }
#else
    (void)layerMask; (void)includeSensors;
#endif
    return {};
}
RigidBodyShapeCastResult RigidBodyWorld::castShape(const RigidBodyQueryShape& shape, math::Vec3 displacement,
                                                 std::uint8_t layerMask, bool includeSensors) const {
    RigidBodyShapeCastResult result;
    const auto desc = queryDescription(shape);
    if (!validate(desc,result.error)) return result;
    if (!bounded(displacement,100000.0F) || displacement.lengthSquared() <= 1.0e-12F ||
        !bounded(shape.position+displacement,100000.0F)) {
        result.error = "Invalid rigid-body shape cast displacement or endpoint";
        return result;
    }
#if NOVACORE_HAS_JOLT
    if (impl_) {
        result.world = impl_->owner; result.tick = impl_->tick;
        const auto native = makeShape(desc);
        if (native.HasError()) { result.error = native.GetError().c_str(); return result; }
        const auto pose = JPH::RMat44::sRotationTranslation(quat(shape.rotation),vec(shape.position));
        const auto cast = JPH::RShapeCast::sFromWorldTransform(native.Get(),JPH::Vec3::sReplicate(1),pose,vec(displacement));
        JPH::ShapeCastSettings settings;
        settings.SetBackFaceMode(JPH::EBackFaceMode::CollideWithBackFaces);
        settings.mReturnDeepestPoint = true;
        JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
        impl_->system->GetNarrowPhaseQuery().CastShape(cast,settings,vec(shape.position),collector,{},
            RayFilter(layerMask),RayBodyFilter(includeSensors));
        if (collector.HadHit()) {
            const auto& hit = collector.mHit;
            result.hit = impl_->shapeHit(hit,shape.position,hit.mFraction,hit.mFraction == 0.0F);
            if (!result.hit) { result.error = "Rigid-body shape cast returned a stale body identity"; return result; }
        }
        result.queried = true;
        return result;
    }
#else
    (void)layerMask; (void)includeSensors;
#endif
    result.error = "Rigid-body world is not initialized";
    return result;
}
RigidBodyOverlapResult RigidBodyWorld::overlapShape(const RigidBodyQueryShape& shape, std::uint32_t maxHits,
                                                   std::uint8_t layerMask, bool includeSensors) const {
    RigidBodyOverlapResult result;
    const auto desc = queryDescription(shape);
    if (!validate(desc,result.error)) return result;
    if (maxHits == 0 || maxHits > 8192) { result.error = "Rigid-body overlap maxHits must be 1..8192"; return result; }
#if NOVACORE_HAS_JOLT
    if (impl_) {
        result.world = impl_->owner; result.tick = impl_->tick;
        const auto native = makeShape(desc);
        if (native.HasError()) { result.error = native.GetError().c_str(); return result; }
        const auto pose = JPH::RMat44::sRotationTranslation(quat(shape.rotation),vec(shape.position))
            .PreTranslated(native.Get()->GetCenterOfMass());
        JPH::CollideShapeSettings settings;
        settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
        BoundedOverlapCollector collector(maxHits);
        impl_->system->GetNarrowPhaseQuery().CollideShape(native.Get(),JPH::Vec3::sReplicate(1),pose,settings,
            vec(shape.position),collector,{},RayFilter(layerMask),RayBodyFilter(includeSensors));
        if (collector.overflow) {
            result.overflow = true; result.error = "Rigid-body overlap maxHits exceeded; no partial result returned";
            return result;
        }
        result.hits.reserve(collector.hits.size());
        for (const auto& hit : collector.hits) {
            auto owned = impl_->shapeHit(hit,shape.position,0,true);
            if (!owned) { result.hits.clear(); result.error = "Rigid-body overlap returned a stale body identity"; return result; }
            result.hits.push_back(std::move(*owned));
        }
        std::sort(result.hits.begin(),result.hits.end(),[](const auto& a,const auto& b) { return endpointLess(a.body,b.body); });
        result.queried = true;
        return result;
    }
#else
    (void)layerMask; (void)includeSensors;
#endif
    result.error = "Rigid-body world is not initialized";
    return result;
}
} // namespace novacore::physics
