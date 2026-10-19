#include "novacore/physics/RigidBodyWorld.hpp"
#include "novacore/physics/PhysicsSystem.hpp"
#include "novacore/physics/MovementSimulation.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>
#include <utility>

using namespace novacore;
using namespace novacore::physics;
namespace {
int failures = 0;
void expect(bool condition, std::string_view message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
bool near(float a, float b, float tolerance = 0.025F) { return std::abs(a-b) <= tolerance; }
PhysicsWorld floorWorld() {
    PhysicsWorld world;
    world.addStaticCollider({"floor",SurfaceKind::Floor,{0,-0.5F,0},{50,0.5F,50},true});
    return world;
}
bool init(RigidBodyWorld& world, RigidBodyWorldConfig config = {}) {
    std::string error;
    const bool ok = world.initialize(config,error);
    expect(ok,error);
    return ok;
}
RigidBodyHandle add(RigidBodyWorld& world, RigidBodyDesc desc) {
    std::string error;
    const auto handle = world.createBody(desc,error);
    expect(handle.valid(),error);
    return handle;
}
void steps(RigidBodyWorld& world, const PhysicsWorld& source, std::uint64_t first, std::uint64_t last) {
    for (auto tick = first; tick <= last; ++tick) expect(world.step(source,tick).stepped,"fixed step succeeds");
}
void testUnavailableAndInvalidWorld() {
    RigidBodyWorld world;
    expect(!world.initialized() && world.bodies().empty(),"default world owns no solver state");
    expect(!world.step({},1).stepped,"uninitialized world cannot tick");
    expect(!world.raycast({},{0,-1,0}),"uninitialized world cannot raycast");
    std::string error;
    RigidBodyWorldConfig invalid;
    invalid.fixedDeltaSeconds = std::numeric_limits<float>::quiet_NaN();
    expect(!world.initialize(invalid,error) && !error.empty(),"NaN fixed delta is rejected before solver");
    if (!RigidBodyWorld::backendAvailable())
        expect(!world.initialize({},error) && error.find("NOVACORE_ENABLE_JOLT") != std::string::npos,
            "dependency-light build explicitly reports disabled backend");
}
void testShapesStackSleepAndImpulse() {
    RigidBodyWorld world;
    if (!init(world)) return;
    const auto source = floorWorld();
    RigidBodyDesc desc;
    desc.id = "box_bottom"; desc.position = {0,2,0}; desc.restitution = 0;
    const auto bottom = add(world,desc);
    desc.id = "box_top"; desc.position = {0,2.55F,0};
    const auto top = add(world,desc);
    desc.id = "sphere"; desc.shape = RigidBodyShape::Sphere; desc.position = {3,3,0};
    const auto sphere = add(world,desc);
    desc.id = "capsule"; desc.shape = RigidBodyShape::Capsule; desc.position = {-3,3,0};
    const auto capsule = add(world,desc);
    steps(world,source,1,600);
    const auto b = world.body(bottom), t = world.body(top), s = world.body(sphere), c = world.body(capsule);
    expect(b && t && s && c,"all supported shapes retain live handles");
    if (!b || !t || !s || !c) return;
    std::cout << "Settled stack centre_y bottom=" << b->position.y << " top=" << t->position.y << '\n';
    expect(near(b->position.y,0.25F) && near(t->position.y,0.75F),"two dynamic boxes stack on actual floor");
    expect(near(s->position.y,0.25F),"dynamic sphere rests at radius above floor");
    expect(c->position.y > 0.23F && c->position.y < 0.78F,"capsule contacts floor without penetrating it");
    expect(b->sleeping && t->sleeping && s->sleeping && c->sleeping,"settled bodies sleep");
    expect(world.applyImpulse(bottom,{4,2,0},b->position + math::Vec3{0,0,0.2F}),"impulse applies to sleeping dynamic body");
    const auto awake = world.body(bottom);
    expect(awake && !awake->sleeping && std::abs(awake->angularVelocity.y) > 0.01F,"off-centre impulse wakes and rotates body");
    const auto floor = world.raycast({8,2,0},{0,-4,0});
    expect(floor && !world.applyImpulse(floor->handle,{1,0,0},{}),"canonical static floor cannot receive impulses");
}
void testOwnershipLifetimeAndReset() {
    RigidBodyWorld world, other;
    if (!init(world) || !init(other)) return;
    RigidBodyDesc desc; desc.id = "reused";
    auto first = add(world,desc);
    expect(!other.body(first) && !other.removeBody(first),"handle cannot access a different world");
    expect(world.removeBody(first),"live application body can be removed");
    for (int i = 0; i < 300; ++i) {
        const auto next = add(world,desc);
        expect(!world.body(first) && !world.removeBody(first),"stale handle survives more than solver sequence wrap");
        expect(world.removeBody(next),"reused slot remains removable");
    }
    auto last = add(world,desc);
    expect(last.index == first.index && last.generation != first.generation,"slot reuse increments engine generation");
    RigidBodyWorld moved = std::move(world);
    expect(!world.initialized() && moved.body(last).has_value(),"moving a world transfers sole ownership and preserves live handle");
    world = std::move(moved);
    expect(!moved.initialized() && world.body(last).has_value(),"move assignment transfers implementation ownership");
    std::string error;
    expect(!world.createBody(desc,error).valid(),"duplicate stable application ID rejected");
    expect(init(world),"world can be reset by successful initialize");
    const auto afterReset = add(world,desc);
    expect(afterReset.world != last.world && !world.body(last),"reset invalidates every previous world handle");
    world.shutdown();
    expect(!world.initialized() && !world.body(afterReset),"shutdown invalidates body snapshots");
}
void testCanonicalChangesAndRamps() {
    RigidBodyWorld world;
    if (!init(world)) return;
    auto source = floorWorld();
    source.addStaticCollider({"wall",SurfaceKind::Wall,{3,1,0},{0.2F,1,2},true});
    source.addStaticCollider({"trigger",SurfaceKind::Trigger,{1.5F,1,0},{0.2F,1,2},true});
    auto first = world.step(source,1);
    expect(first.stepped && first.staticBodiesCreated == 2,"canonical source creates only required fixtures");
    const auto hit = world.raycast({0,1,0},{6,0,0});
    expect(hit && hit->id == "wall" && near(hit->position.x,2.8F,0.001F) && near(hit->normal.x,-1,0.001F),
        "closest ray returns exact collider ID, point and world normal");
    expect(hit && !world.removeBody(hit->handle),"derived collider cannot be edited outside canonical source");
    const auto second = world.step(source,2);
    expect(second.staticBodiesCreated == 0 && second.staticBodiesUpdated == 0 && second.staticBodiesRemoved == 0,
        "unchanged canonical world is never rebuilt");
    expect(source.setColliderKinematics("wall",{5,1,0},{}),"canonical wall transform update accepted");
    const auto moved = world.step(source,3);
    expect(moved.staticBodiesUpdated == 1 && moved.staticBodiesCreated == 0,"only changed canonical transform is updated");
    const auto newHit = world.raycast({0,1,0},{6,0,0});
    expect(hit && newHit && hit->handle == newHit->handle && near(newHit->position.x,4.8F,0.001F),
        "static update retains handle and query follows actual new position");
    source.clearStaticColliders();
    const auto removed = world.step(source,4);
    expect(removed.staticBodiesRemoved == 2 && hit && !world.body(hit->handle),"canonical removal invalidates handles");
    const RampDirection directions[] = {RampDirection::PositiveX,RampDirection::NegativeX,RampDirection::PositiveZ,RampDirection::NegativeZ};
    for (int i=0; i<4; ++i) source.addStaticCollider({"ramp"+std::to_string(i),SurfaceKind::Ramp,
        {static_cast<float>(i*5),1,0},{2,1,2},true,directions[i]});
    expect(world.step(source,5).stepped,"all four canonical ramp orientations synchronize");
    for (int i=0; i<4; ++i) {
        const math::Vec3 offset = i<2 ? math::Vec3{1,0,0} : math::Vec3{0,0,1};
        const auto ramp = world.raycast(math::Vec3{static_cast<float>(i*5),4,0}+offset,{0,-5,0});
        const float height = (i == 0 || i == 2) ? 1.5F : 0.5F;
        expect(ramp && ramp->id == "ramp"+std::to_string(i) && near(ramp->position.y,height,0.002F),
            "ramp hull agrees with original KCC linear top surface");
    }
}
void testFiltersCcdAndValidation() {
    RigidBodyWorld world;
    RigidBodyWorldConfig config; config.gravity = {};
    if (!init(world,config)) return;
    PhysicsWorld source;
    source.addStaticCollider({"thin_wall",SurfaceKind::Wall,{0,1,0},{0.025F,2,2},true});
    RigidBodyDesc desc; desc.id = "fast"; desc.shape = RigidBodyShape::Sphere;
    desc.radius = 0.1F; desc.position = {-4,1,0}; desc.linearVelocity = {500,0,0}; desc.restitution = 0;
    const auto fast = add(world,desc);
    expect(world.step(source,1).stepped,"CCD fixed tick succeeds");
    const auto state = world.body(fast);
    expect(state && state->position.x < -0.07F && state->position.x > -0.3F,"500m/s sphere cannot tunnel through 5cm wall");
    expect(!world.raycast({-4,1,0},{8,0,0},1U<<7U),"query layer mask excludes all other categories");
    const auto staticHit = world.raycast({-4,1,0},{8,0,0},1U<<0U);
    expect(staticHit && staticHit->id == "thin_wall","query can select static layer independently of nearer dynamic body");
    const auto before = world.body(fast);
    expect(!world.step(source,1).stepped && !world.step(source,3).stepped,"duplicate/skipped ticks rejected");
    expect(before && world.body(fast) && before->position.x == world.body(fast)->position.x,"bad ticks do not advance body");
    std::string error;
    desc.id = "invalid"; desc.linearVelocity = {}; desc.position.x = std::numeric_limits<float>::infinity();
    expect(!world.createBody(desc,error).valid(),"infinite transform rejected");
    desc.position = {}; desc.rotation.w = 0;
    expect(!world.createBody(desc,error).valid(),"zero quaternion rejected");
    desc.rotation.w = 1; desc.massKg = 0;
    expect(!world.createBody(desc,error).valid(),"zero dynamic mass rejected");
    desc.massKg = 1; desc.friction = std::numeric_limits<float>::quiet_NaN();
    expect(!world.createBody(desc,error).valid(),"NaN material rejected");
    expect(!world.applyImpulse(fast,{std::numeric_limits<float>::infinity(),0,0},{}) &&
        !world.raycast({},{std::numeric_limits<float>::quiet_NaN(),0,1}),"nonfinite impulse and query rejected");
    source.addStaticCollider(source.staticColliders().front());
    expect(!world.step(source,2).stepped && world.body(fast).has_value(),"duplicate canonical IDs rejected before mutation");

    RigidBodyWorld filtered;
    if (!init(filtered)) return;
    desc = {}; desc.id = "no_floor"; desc.position = {0,2,0}; desc.filter.mask = 0;
    const auto falls = add(filtered,desc);
    steps(filtered,floorWorld(),1,120);
    expect(filtered.body(falls) && filtered.body(falls)->position.y < -10,"pair filtering requires both bodies' masks");
}
void testKinematicProxyAndMovingSupport() {
    RigidBodyWorld world;
    if (!init(world)) return;
    const auto source = floorWorld();
    RigidBodyDesc prop; prop.id = "prop"; prop.position = {0,0.25F,0};
    const auto box = add(world,prop);
    RigidBodyDesc proxy; proxy.id = "player_proxy"; proxy.motion = RigidBodyMotion::Kinematic;
    proxy.shape = RigidBodyShape::Capsule; proxy.radius = 0.3F; proxy.capsuleHalfHeight = 0.6F;
    proxy.position = {-2,0.9F,0}; proxy.filter = {2,1U<<1U};
    const auto player = add(world,proxy);
    for (std::uint64_t tick=1; tick<=60; ++tick) {
        expect(world.moveKinematic(player,{-2+static_cast<float>(tick)/15,0.9F,0},{}),"kinematic target accepts resolved character position");
        expect(world.step(source,tick).stepped,"one-way character proxy contacts simulate");
    }
    expect(world.body(box) && world.body(box)->position.x > 1.0F,"kinematic character capsule physically pushes dynamic prop");
    expect(world.body(player) && near(world.body(player)->position.x,2,0.001F),"dynamic prop never displaces authoritative proxy target");
    steps(world,source,61,65);
    expect(world.body(player) && near(world.body(player)->position.x,2,0.001F),"proxy without new target stops rather than repeats old velocity");

    RigidBodyWorld platformWorld;
    if (!init(platformWorld)) return;
    PhysicsWorld platformSource;
    platformSource.addStaticCollider({"platform",SurfaceKind::Cover,{-2,0.5F,0},{2,0.25F,2},true,RampDirection::None,0,{0.5F,0,0}});
    prop.id = "passenger"; prop.position = {-2,1.1F,0}; prop.friction = 1;
    const auto passenger = add(platformWorld,prop);
    for (std::uint64_t tick=1; tick<=240; ++tick) {
        if (tick>1) (void)platformSource.advanceKinematicColliders(1.0F/60);
        expect(platformWorld.step(platformSource,tick).stepped,"moving canonical platform steps");
    }
    const auto passengerState = platformWorld.body(passenger);
    expect(passengerState && passengerState->position.x > -0.8F && passengerState->position.y > 0.9F,
        "rigid body rides frictional moving platform without falling through");
    const auto hit = platformWorld.raycast({1.8F,2,1.8F},{0,-3,0},1);
    expect(hit && near(hit->position.y,0.75F,0.001F),"queries use current canonical platform pose");
}
void testOrientedShapeRestitutionAndCapacity() {
    RigidBodyWorld oriented;
    RigidBodyWorldConfig config; config.gravity = {}; config.maxBodies = 8;
    if (!init(oriented,config)) return;
    RigidBodyDesc desc; desc.id = "rotated"; desc.motion = RigidBodyMotion::Static;
    desc.halfExtents = {1,0.25F,0.25F};
    desc.rotation = {0,0.70710678F,0,0.70710678F};
    (void)add(oriented,desc);
    const auto hit = oriented.raycast({-3,0,0},{6,0,0});
    expect(hit && near(hit->position.x,-0.25F,0.001F),"unit quaternion rotates box shape once without render-scale compensation");
    for (int i=0;i<7;++i) { desc.id = "capacity"+std::to_string(i); (void)add(oriented,desc); }
    std::string error;
    desc.id = "overflow";
    expect(!oriented.createBody(desc,error).valid() && error.find("capacity") != std::string::npos,
        "bounded body capacity reports failure without solver corruption");
    desc.halfExtents.x = -1;
    expect(!oriented.createBody(desc,error).valid(),"negative collider scale is rejected at boundary");

    RigidBodyWorld bounce;
    if (!init(bounce)) return;
    desc = {}; desc.id = "elastic"; desc.shape = RigidBodyShape::Sphere;
    desc.position = {0,2,0}; desc.restitution = 0.8F; desc.linearDamping = 0;
    const auto sphere = add(bounce,desc);
    bool bounced = false;
    for (std::uint64_t tick=1; tick<=120; ++tick) {
        expect(bounce.step(floorWorld(),tick).stepped,"restitution simulation steps");
        const auto state = bounce.body(sphere);
        if (state && state->linearVelocity.y > 2 && tick>15) bounced = true;
    }
    expect(bounced,"configured restitution produces an actual upward bounce after floor impact");
}
void testRemovedSupportWakesSleepingBody() {
    RigidBodyWorld world;
    if (!init(world)) return;
    auto source = floorWorld();
    RigidBodyDesc desc; desc.id = "sleeping_passenger"; desc.position = {0,1,0}; desc.restitution = 0;
    const auto prop = add(world,desc);
    steps(world,source,1,360);
    expect(world.body(prop) && world.body(prop)->sleeping,"body sleeps before support removal");
    source.clearStaticColliders();
    steps(world,source,361,420);
    expect(world.body(prop) && !world.body(prop)->sleeping && world.body(prop)->position.y < -2,
        "removing canonical support wakes nearby bodies instead of leaving floating sleepers");

    RigidBodyWorld stack;
    if (!init(stack)) return;
    desc.id = "bottom"; desc.position = {0,0.25F,0};
    const auto bottom = add(stack,desc);
    desc.id = "top"; desc.position = {0,0.75F,0};
    const auto top = add(stack,desc);
    steps(stack,floorWorld(),1,360);
    expect(stack.body(top) && stack.body(top)->sleeping,"upper body sleeps on another dynamic body");
    expect(stack.removeBody(bottom),"lower dynamic body removal succeeds");
    steps(stack,floorWorld(),361,480);
    expect(stack.body(top) && near(stack.body(top)->position.y,0.25F),"removing dynamic support wakes and drops upper body");
}
bool involves(const RigidBodySensorEvent& event, RigidBodyHandle handle) {
    return event.first.handle == handle || event.second.handle == handle;
}
void testSensorLifecycleTagsAndReuse() {
    RigidBodyWorld world;
    RigidBodyWorldConfig config; config.gravity = {}; config.maxContactEvents = 64;
    if (!init(world,config)) return;
    RigidBodyDesc sensor; sensor.id = "zone"; sensor.motion = RigidBodyMotion::Kinematic;
    sensor.isSensor = true; sensor.halfExtents = {1,1,1}; sensor.filter = {3,0x06};
    sensor.materialId = 7; sensor.surfaceId = 17;
    const auto zone = add(world,sensor);
    RigidBodyDesc desc; desc.id = "player"; desc.motion = RigidBodyMotion::Kinematic;
    desc.position = {-3,0,0}; desc.filter = {2,1U<<3U}; desc.materialId = 8; desc.surfaceId = 18;
    const auto player = add(world,desc);
    desc.id = "excluded"; desc.position = {}; desc.filter.mask = 0;
    (void)add(world,desc);
    expect(world.step({},1).sensorEvents.empty(),"outside player and asymmetric excluded mask never enter sensor");
    expect(world.moveKinematic(player,{},{}),"kinematic player enters sensor using bounded native target");
    const auto movingIn = world.step({},2);
    expect(movingIn.stepped && movingIn.sensorEvents.empty(),"discrete sensor does not invent a swept entry before the target pose is sampled");
    const auto entered = world.step({},3);
    expect(entered.stepped && entered.sensorEvents.size() == 1 && entered.contacts.size() == 1 &&
        entered.sensorEvents[0].phase == RigidBodySensorPhase::Enter && involves(entered.sensorEvents[0],player),
        "native kinematic sensor detects a kinematic player exactly once without physical response");
    expect(world.body(player) && world.body(player)->position.lengthSquared() < 1.0e-6F &&
        world.body(zone) && world.body(zone)->position.lengthSquared() < 1.0e-6F,"sensor overlap leaves both coincident kinematic poses unchanged");
    const auto state = world.body(zone);
    expect(state && state->isSensor && state->materialId == 7 && state->surfaceId == 17,
        "snapshot exposes application material/surface IDs and sensor flag");
    expect(!world.raycast({-3,0,0},{6,0,0},1U<<3U),"ordinary shot ray excludes sensors by default");
    const auto sensorHit = world.raycast({-3,0,0},{6,0,0},1U<<3U,true);
    expect(sensorHit && sensorHit->handle == zone && sensorHit->isSensor && sensorHit->materialId == 7 && sensorHit->surfaceId == 17,
        "explicit sensor ray returns native volume intersection and owned tags");
    expect(world.setBodyTags(zone,9,19) && world.setBodyTags(player,10,20),"live tags may change without altering response coefficients");
    const auto stayed = world.step({},4);
    expect(stayed.stepped && stayed.sensorEvents.size() == 1 && stayed.sensorEvents[0].phase == RigidBodySensorPhase::Stay &&
        stayed.sensorEvents[0].second.materialId == 9 && stayed.sensorEvents[0].second.surfaceId == 19 &&
        stayed.contacts.size() == 1 && stayed.contacts[0].first.materialId == 10 && stayed.contacts[0].tick == 4,
        "metadata edits update tagged contact and Stay without fake lifecycle changes");
    expect(world.moveKinematic(player,{3,0,0},{}),"player moves outside discrete sensor");
    const auto movingOut = world.step({},5);
    expect(movingOut.stepped && movingOut.sensorEvents.size() == 1 && movingOut.sensorEvents[0].phase == RigidBodySensorPhase::Stay,
        "moving target is still an occupant at this tick's pre-solve sample");
    const auto left = world.step({},6);
    expect(left.stepped && left.sensorEvents.size() == 1 && left.sensorEvents[0].phase == RigidBodySensorPhase::Exit &&
        left.sensorEvents[0].exitReason == RigidBodySensorExitReason::Separated,
        "native pair separation emits one owned Exit after the fixed tick");
    expect(world.step({},7).sensorEvents.empty(),"separated pair never repeats Exit");
    expect(world.moveKinematic(player,{},{}),"player returns to sensor");
    expect(world.step({},8).sensorEvents.empty(),"returning target has not yet been sampled inside");
    expect(world.step({},9).sensorEvents.size() == 1,"returning player enters again");
    expect(world.removeBody(player) && !world.body(player) && !world.setBodyTags(player,1,1),"removal invalidates lookup and metadata edits immediately");
    const auto rejected = world.step({},9);
    expect(!rejected.stepped && rejected.sensorEvents.empty() && rejected.contacts.empty(),"rejected tick never drains queued Exit");
    desc.id = "player"; desc.position = {}; desc.filter.mask = 1U<<3U;
    const auto replacement = add(world,desc);
    const auto reused = world.step({},10);
    expect(replacement.index == player.index && replacement.generation != player.generation && reused.stepped && reused.sensorEvents.size() == 2,
        "same ID/slot reuse preserves distinct engine generations in the event packet");
    bool oldExit = false, newEnter = false;
    for (const auto& event : reused.sensorEvents) {
        oldExit |= involves(event,player) && event.phase == RigidBodySensorPhase::Exit &&
            event.exitReason == RigidBodySensorExitReason::BodyRemoved && event.first.id == "player" && event.tick == 10;
        newEnter |= involves(event,replacement) && event.phase == RigidBodySensorPhase::Enter;
    }
    expect(oldExit && newEnter,"deferred native removal cannot alias a new body or erase its Enter");
    expect(world.removeBody(zone),"sensor may itself be removed");
    expect(init(world,config),"reset with pending Exit reinitializes capture");
    const auto resetResult = world.step({},1);
    expect(resetResult.stepped && resetResult.sensorEvents.empty() && resetResult.contacts.empty() && !world.body(replacement),
        "reset clears occupancy and queued stream instead of emitting stale-world exits");
}
void testSleepingSensorAndCanonicalMaterialTags() {
    RigidBodyWorld world;
    RigidBodyWorldConfig config; config.maxContactEvents = 64;
    if (!init(world,config)) return;
    auto source = floorWorld();
    RigidBodyDesc sensor; sensor.id = "zone"; sensor.motion = RigidBodyMotion::Kinematic; sensor.isSensor = true;
    sensor.position = {0,0.5F,0}; sensor.halfExtents = {1,1,1}; sensor.filter = {3,1U<<1U};
    const auto zone = add(world,sensor);
    RigidBodyDesc prop; prop.id = "prop"; prop.position = {0,0.5F,0}; prop.restitution = 0;
    prop.materialId = 42; prop.surfaceId = 24;
    const auto body = add(world,prop);
    for (std::uint64_t tick=1; tick<=360; ++tick) {
        const auto result = world.step(source,tick);
        expect(result.stepped && result.sensorEvents.size() == 1 &&
            result.sensorEvents[0].phase == (tick == 1 ? RigidBodySensorPhase::Enter : RigidBodySensorPhase::Stay),
            "active kinematic sensor retains overlap through dynamic body sleeping");
    }
    expect(world.body(body) && world.body(body)->sleeping,"sensor never prevents its dynamic occupant from sleeping");
    const auto floor = world.raycast({4,2,0},{0,-4,0},1U<<0U);
    expect(floor && world.setBodyTags(floor->handle,100,200),"canonical collider may receive game-owned material tags");
    const auto taggedFloor = world.raycast({4,2,0},{0,-4,0},1U<<0U);
    expect(taggedFloor && taggedFloor->materialId == 100 && taggedFloor->surfaceId == 200,"static query reports assigned material/surface values");
    expect(world.applyImpulse(body,{0,0.1F,0},{0,0.25F,0}),"tagged sleeping body wakes on real impulse");
    const auto contacts = world.step(source,361);
    bool taggedContact = false;
    for (const auto& contact : contacts.contacts) if (contact.first.id == "floor" && contact.second.id == "prop") {
        taggedContact = contact.first.materialId == 100 && contact.second.materialId == 42 && contact.first.surfaceId == 200 &&
            contact.normal.y > 0.9F && std::isfinite(contact.penetrationDepth) && contact.tick == 361;
    }
    expect(contacts.stepped && taggedContact,"native pre-solve manifold has sorted tagged endpoints and correctly oriented normal");
    source.clearStaticColliders();
    source.addStaticCollider({"floor",SurfaceKind::Floor,{0,-0.5F,0},{49,0.5F,50},true});
    expect(world.step(source,362).stepped,"changed canonical shape recreates native fixture");
    const auto replacedFloor = world.raycast({4,2,0},{0,-4,0},1U<<0U);
    expect(floor && replacedFloor && floor->handle != replacedFloor->handle && !world.body(floor->handle) &&
        replacedFloor->materialId == 100 && replacedFloor->surfaceId == 200,"same canonical ID retains tags across generation-changing shape replacement");
    expect(world.moveKinematic(zone,{5,0.5F,0},{}),"sensor moves away from sleeping-capable occupant");
    expect(world.step(source,363).stepped,"sensor target integrates after discrete contact sampling");
    const auto exit = world.step(source,364);
    expect(exit.stepped && exit.sensorEvents.size() == 1 && exit.sensorEvents[0].phase == RigidBodySensorPhase::Exit,
        "moving active sensor emits real separation Exit");
    source.clearStaticColliders();
    expect(world.step(source,365).stepped,"canonical material owner may be removed");
    source = floorWorld();
    expect(world.step(source,366).stepped,"truly removed canonical ID can return");
    const auto returnedFloor = world.raycast({4,2,0},{0,-4,0},1U<<0U);
    expect(returnedFloor && returnedFloor->materialId == 0 && returnedFloor->surfaceId == 0,"true canonical removal clears tags rather than inheriting recycled metadata");
}
void testContactCaptureBoundsAndSensorValidation() {
    std::string error;
    RigidBodyWorld world;
    if (!init(world)) return;
    RigidBodyDesc sensor; sensor.id = "sensor"; sensor.isSensor = true;
    expect(!world.createBody(sensor,error).valid(),"dynamic sensor mode is rejected instead of changing native solver semantics");
    sensor.motion = RigidBodyMotion::Static;
    expect(!world.createBody(sensor,error).valid(),"static sensors cannot falsely exit when occupants sleep");
    sensor.motion = RigidBodyMotion::Kinematic;
    expect(!world.createBody(sensor,error).valid(),"sensor without explicit capture budget is rejected");
    RigidBodyWorldConfig config; config.maxContactEvents = 65537;
    expect(!world.initialize(config,error),"unbounded event budgets are rejected before allocation");
    config.gravity = {}; config.maxContactEvents = 1;
    if (!init(world,config)) return;
    sensor.halfExtents = {2,2,2}; sensor.filter = {3,1U<<1U};
    const auto zone = add(world,sensor);
    RigidBodyDesc prop; prop.motion = RigidBodyMotion::Static; prop.filter = {1,1U<<3U}; prop.id = "a";
    (void)add(world,prop); prop.id = "b"; prop.position = {1,0,0}; (void)add(world,prop);
    const auto overflow = world.step({},1);
    expect(!overflow.stepped && !overflow.error.empty() && overflow.contacts.empty() && overflow.sensorEvents.empty() &&
        !world.initialized() && !world.body(zone),"callback overflow invalidates world and returns explicit error without a partial event packet");

    // One old Exit plus one new Enter can exceed extraction capacity even when
    // this tick's native callbacks fit; neither event may silently disappear.
    if (!init(world,config)) return;
    (void)add(world,sensor); prop.id = "old"; prop.position = {}; const auto old = add(world,prop);
    expect(world.step({},1).stepped,"single native pair fits bounded capture");
    expect(world.removeBody(old),"bounded capture queues old pair Exit");
    prop.id = "new"; (void)add(world,prop);
    const auto packetOverflow = world.step({},2);
    expect(!packetOverflow.stepped && packetOverflow.contacts.empty() && packetOverflow.sensorEvents.empty() && !world.initialized(),
        "combined pending exits and current enters fail closed at extracted packet limit");
}
void testCanonicalSensorReplacementAndSortedReplay() {
    std::vector<std::pair<std::string,RigidBodySensorPhase>> firstEvents;
    for (int repeat=0; repeat<2; ++repeat) {
        RigidBodyWorld world;
        RigidBodyWorldConfig config; config.gravity = {}; config.maxContactEvents = 32;
        if (!init(world,config)) return;
        RigidBodyDesc sensor; sensor.id = "zone"; sensor.isSensor = true; sensor.motion = RigidBodyMotion::Kinematic;
        sensor.halfExtents = {2,2,2}; sensor.filter = {3,0x03};
        (void)add(world,sensor);
        RigidBodyDesc prop; prop.id = "zeta"; prop.motion = RigidBodyMotion::Static; prop.filter = {1,1U<<3U};
        (void)add(world,prop); prop.id = "alpha"; prop.position = {1,0,0}; (void)add(world,prop);
        auto source = floorWorld();
        const auto first = world.step(source,1);
        expect(first.stepped && first.sensorEvents.size() == 3 && first.sensorEvents[0].first.id == "alpha" &&
            first.sensorEvents[1].first.id == "floor" && first.sensorEvents[2].first.id == "zeta",
            "event extraction is application ID sorted despite native creation order");
        auto floor = world.raycast({4,2,0},{0,-4,0},1U<<0U);
        expect(floor && world.setBodyTags(floor->handle,123,456),"canonical sensor occupant receives application tags");
        source.clearStaticColliders();
        source.addStaticCollider({"floor",SurfaceKind::Floor,{0,-0.5F,0},{49,0.5F,50},true});
        const auto replaced = world.step(source,2);
        bool oldExit = false, newEnter = false;
        for (const auto& event : replaced.sensorEvents) if (event.first.id == "floor") {
            oldExit |= floor && event.first.handle == floor->handle && event.phase == RigidBodySensorPhase::Exit &&
                event.exitReason == RigidBodySensorExitReason::ColliderReplaced && event.first.materialId == 123;
            newEnter |= floor && event.first.handle != floor->handle && event.phase == RigidBodySensorPhase::Enter && event.first.surfaceId == 456;
        }
        expect(replaced.stepped && replaced.sensorEvents.size() == 4 && oldExit && newEnter,
            "canonical shape replacement emits old generation Exit and tagged new generation Enter");
        std::vector<std::pair<std::string,RigidBodySensorPhase>> events;
        for (const auto& event : first.sensorEvents) events.emplace_back(event.first.id,event.phase);
        for (const auto& event : replaced.sensorEvents) events.emplace_back(event.first.id,event.phase);
        if (repeat == 0) firstEvents = events;
        else expect(events == firstEvents,"same fixed inputs reproduce sorted sensor lifecycle independently of world token");
    }
}
void testShapeQueryBoundaryAndUnavailable() {
    RigidBodyWorld world;
    RigidBodyQueryShape shape;
    expect(!world.castShape(shape,{1,0,0}).queried && !world.overlapShape(shape,8).queried,
        "shape queries report an uninitialized world rather than a valid miss");
    shape.position.x = std::numeric_limits<float>::quiet_NaN();
    expect(!world.castShape(shape,{1,0,0}).queried && !world.overlapShape(shape,8).queried,
        "shape queries reject non-finite pose before native shape construction");
    shape = {}; shape.rotation = {0,0,0,0};
    expect(!world.castShape(shape,{1,0,0}).queried,"shape cast requires the same unit quaternion as body creation");
    shape = {}; shape.halfExtents.x = -.1F;
    expect(!world.overlapShape(shape,8).queried,"overlap query rejects negative box scale");
    shape = {}; shape.shape = RigidBodyShape::Sphere; shape.radius = 0;
    expect(!world.overlapShape(shape,8).queried,"overlap query rejects zero sphere radius");
    shape = {}; shape.shape = RigidBodyShape::Capsule; shape.capsuleHalfHeight = 0;
    expect(!world.castShape(shape,{1,0,0}).queried,"shape cast rejects invalid capsule cylinder half height");
    shape = {};
    expect(!world.overlapShape(shape,0).queried && !world.overlapShape(shape,8193).queried,
        "overlap capacity must be explicitly bounded between one and 8192");
    expect(!world.castShape(shape,{}).queried && !world.castShape(shape,{std::numeric_limits<float>::infinity(),0,0}).queried,
        "zero and non-finite shape translations are rejected");
}
void testNativeShapeCasts() {
    RigidBodyWorld world;
    RigidBodyWorldConfig config; config.gravity = {};
    if (!init(world,config)) return;
    auto source = floorWorld();
    source.addStaticCollider({"wall",SurfaceKind::Wall,{3,2,0},{.1F,2,2},true});
    expect(world.step(source,1).stepped,"native query world derives canonical floor and wall");
    const auto wall = world.raycast({-2,1,0},{6,0,0});
    expect(wall && world.setBodyTags(wall->handle,8,9),"canonical query target receives owned application tags");
    for (const auto type : {RigidBodyShape::Box,RigidBodyShape::Sphere,RigidBodyShape::Capsule}) {
        RigidBodyQueryShape shape; shape.shape = type; shape.position = {-2,1,0};
        const auto cast = world.castShape(shape,{6,0,0});
        expect(cast.queried && cast.error.empty() && cast.hit && cast.hit->body.id == "wall" &&
            cast.hit->body.canonicalCollider && cast.hit->body.materialId == 8 && cast.hit->body.surfaceId == 9 &&
            near(cast.hit->fraction,.775F,.002F) && cast.hit->normal.x < -.99F &&
            near(cast.hit->pointOnBody.x,2.9F,.002F) && !cast.hit->initialOverlap &&
            cast.tick == 1 && cast.world == cast.hit->body.handle.world,
            "box/sphere/capsule cast finds native wall, normalized outward normal, fraction, tags and world/tick");
        shape.position = {0,2,0};
        const auto floor = world.castShape(shape,{0,-4,0});
        const float halfHeight = type == RigidBodyShape::Capsule ? .75F : .25F;
        expect(floor.queried && floor.hit && floor.hit->body.id == "floor" && floor.hit->normal.y > .99F &&
            near(floor.hit->fraction,(2-halfHeight)/4,.002F) && near(floor.hit->pointOnBody.y,0,.002F),
            "all three native query shapes use their actual vertical extent against the authored floor");
    }
    RigidBodyQueryShape rotated; rotated.position = {-2,1,0}; rotated.halfExtents = {1,.2F,.2F};
    const auto unrotated = world.castShape(rotated,{6,0,0});
    rotated.rotation = {0,.70710678F,0,.70710678F};
    const auto turned = world.castShape(rotated,{6,0,0});
    expect(unrotated.hit && turned.hit && near(unrotated.hit->fraction,.65F,.002F) &&
        near(turned.hit->fraction,4.7F/6,.002F),"oriented query box is rotated once and keeps its orientation during translation");
    rotated.shape = RigidBodyShape::Capsule; rotated.rotation = {0,0,.70710678F,.70710678F};
    const auto capsule = world.castShape(rotated,{6,0,0});
    expect(capsule.hit && near(capsule.hit->fraction,4.15F/6,.002F),"rotated capsule uses cylindrical axis plus radius rather than AABB repair");
    RigidBodyDesc target; target.id = "rotated_target"; target.motion = RigidBodyMotion::Static;
    target.position = {0,1,4}; target.halfExtents = {1,.3F,.2F}; target.rotation = {0,.70710678F,0,.70710678F};
    target.filter = {4,0}; target.materialId = 55; target.surfaceId = 66;
    const auto targetHandle = add(world,target);
    RigidBodyQueryShape query; query.position = {-3,1,4};
    const auto targetCast = world.castShape(query,{6,0,0},1U<<4U);
    expect(targetCast.hit && targetCast.hit->body.handle == targetHandle && near(targetCast.hit->fraction,.425F,.002F) &&
        targetCast.hit->body.materialId == 55 && targetCast.hit->body.surfaceId == 66,
        "query intersects actual rotated target and uses target category like ray, independently of pair-response mask");
    query.position = {-.8F,1,4}; query.halfExtents = {.1F,.1F,.1F};
    expect(world.overlapShape(query,1,1U<<4U).hits.empty(),"native overlap respects rotated target shape rather than its unrotated dimensions");
    target = {}; target.id = "dynamic_target"; target.position = {-1,1,0}; target.filter = {1,0};
    target.materialId = 75; target.surfaceId = 76;
    const auto dynamic = add(world,target);
    query = {}; query.shape = RigidBodyShape::Sphere; query.position = {-2,1,0};
    const auto dynamicCast = world.castShape(query,{6,0,0});
    expect(dynamicCast.hit && dynamicCast.hit->body.handle == dynamic && !dynamicCast.hit->body.canonicalCollider &&
        dynamicCast.hit->body.materialId == 75 && near(dynamicCast.hit->fraction,.5F/6,.002F),
        "closest shape cast selects a nearer dynamic body before the canonical wall");
    const auto unchangedRay = world.raycast({-2,1,0},{6,0,0},1U<<0U);
    expect(wall && unchangedRay && wall->handle == unchangedRay->handle && near(unchangedRay->fraction,wall->fraction,1.e-6F),
        "new native queries do not replace or modify existing closest-ray behavior");
    const auto before = world.body(dynamic);
    expect(world.step(source,2).stepped && before && world.body(dynamic) &&
        near(world.body(dynamic)->position.x,before->position.x,.00001F),"queries never consume a fixed tick or move a body");
}
void testOverlapBoundsSensorsAndLifetime() {
    RigidBodyWorld world;
    RigidBodyWorldConfig config; config.gravity = {}; config.maxContactEvents = 32;
    if (!init(world,config)) return;
    RigidBodyDesc desc; desc.id = "zeta"; desc.position = {}; desc.filter = {1,0};
    desc.materialId = 10; desc.surfaceId = 20; (void)add(world,desc);
    desc.id = "alpha"; desc.position = {.3F,0,0}; desc.materialId = 11; desc.surfaceId = 21;
    const auto alpha = add(world,desc);
    desc = {}; desc.id = "sensor"; desc.motion = RigidBodyMotion::Kinematic; desc.isSensor = true;
    desc.halfExtents = {.5F,.5F,.5F}; desc.filter = {3,0}; desc.materialId = 12; desc.surfaceId = 22;
    const auto sensor = add(world,desc);
    const auto source = floorWorld();
    expect(world.step(source,1).stepped,"overlap fixture synchronizes one canonical floor");
    const auto floor = world.raycast({4,2,0},{0,-4,0},1U<<0U);
    expect(floor && world.setBodyTags(floor->handle,99,199),"overlap canonical tag is assigned through public API");
    RigidBodyQueryShape query; query.position = {0,.1F,0}; query.halfExtents = {1,1,1};
    const auto overlap = world.overlapShape(query,3);
    expect(overlap.queried && !overlap.overflow && overlap.hits.size() == 3 &&
        overlap.hits[0].body.id == "alpha" && overlap.hits[1].body.id == "floor" && overlap.hits[2].body.id == "zeta" &&
        overlap.hits[0].body.materialId == 11 && overlap.hits[1].body.materialId == 99,
        "bounded overlap returns unique owned dynamic/canonical contacts in application-ID order, excluding sensors by default");
    expect(overlap.hits.size() == 3 && near(overlap.hits[1].pointOnBody.y,0,.002F) && overlap.hits[1].normal.y > .99F,
        "overlap adds the query-relative native base offset once and returns the floor's outward normal");
    for (const auto& hit : overlap.hits) expect(hit.initialOverlap && hit.fraction == 0 && std::isfinite(hit.penetrationDepth) &&
        near(hit.normal.lengthSquared(),1,.001F),"native overlap results carry finite depth and normalized outward body normals");
    const auto includingSensor = world.overlapShape(query,4,0xff,true);
    expect(includingSensor.queried && includingSensor.hits.size() == 4 &&
        includingSensor.hits[2].body.handle == sensor && includingSensor.hits[2].body.isSensor,
        "explicit sensor overlap includes the native sensor with its own world/generation identity");
    const auto overflow = world.overlapShape(query,3,0xff,true);
    expect(!overflow.queried && overflow.overflow && !overflow.error.empty() && overflow.hits.empty() && world.initialized() &&
        overflow.world == overlap.world && overflow.tick == 1,"too many bodies fail the read-only query without a traversal subset or world invalidation");
    const auto masked = world.overlapShape(query,1,1U<<0U);
    expect(masked.queried && masked.hits.size() == 1 && masked.hits[0].body.id == "floor",
        "overlap target mask can select canonical geometry independently of nearer dynamic bodies");
    const auto none = world.overlapShape(query,1,1U<<7U);
    expect(none.queried && none.hits.empty() && none.error.empty(),"valid empty overlap is distinct from invalid query and overflow");
    query = {}; query.position = {-2,0,0};
    const auto skippedSensor = world.castShape(query,{4,0,0},1U<<3U);
    const auto sensorCast = world.castShape(query,{4,0,0},1U<<3U,true);
    expect(skippedSensor.queried && !skippedSensor.hit && sensorCast.hit && sensorCast.hit->body.handle == sensor &&
        sensorCast.hit->body.isSensor && sensorCast.hit->body.surfaceId == 22,
        "shape casts default to physical targets and explicitly opt into tagged sensors");
    expect(world.moveKinematic(sensor,{4,0,0},{}),"kinematic sensor receives bounded target for query timing check");
    expect(world.overlapShape({},1,1U<<3U,true).hits.size() == 1,
        "queued kinematic target does not invent a second current-pose query truth");
    expect(world.step(source,2).stepped && world.overlapShape({},1,1U<<3U,true).hits.empty(),
        "current-pose overlap follows actual native integration rather than the pre-solve contact event sample");
    RigidBodyQueryShape movedSensor; movedSensor.position = {4,0,0};
    expect(world.overlapShape(movedSensor,1,1U<<3U,true).hits.size() == 1,"integrated sensor is queryable at its actual new pose");
    expect(world.removeBody(alpha) && !world.body(alpha),"removed overlap endpoint immediately fails live handle lookup");
    desc = {}; desc.id = "alpha"; desc.position = {20,0,0}; desc.filter = {1,0};
    const auto replacement = add(world,desc);
    query.position = {0,.1F,0}; query.halfExtents = {1,1,1};
    const auto afterRemoval = world.overlapShape(query,3);
    expect(replacement.index == alpha.index && replacement.generation != alpha.generation && afterRemoval.queried &&
        afterRemoval.hits.size() == 2 && overlap.hits[0].body.handle == alpha && overlap.hits[0].body.id == "alpha" &&
        overlap.hits[0].body.materialId == 11,"owned hit survives removal while new query never aliases a reused slot generation");
    expect(init(world,config),"query world can be reset after successful and overflow queries");
    desc.id = "reset_target"; desc.position = {}; const auto resetTarget = add(world,desc);
    const auto reset = world.overlapShape({},1);
    expect(reset.queried && reset.tick == 0 && reset.world != overlap.world && reset.hits.size() == 1 &&
        reset.hits[0].body.handle == resetTarget && !world.body(alpha) && !world.body(replacement),
        "reset changes result world identity and invalidates every old query handle");
    expect(world.step({},1).stepped,"reset queries do not advance simulation or require canonical dummy ticks");
}
void testNativeInitialOverlapAndSleepingQueries() {
    RigidBodyWorld world;
    RigidBodyWorldConfig config; config.gravity = {};
    if (!init(world,config)) return;
    RigidBodyDesc box; box.id = "box"; box.motion = RigidBodyMotion::Static; box.halfExtents = {1,1,1}; box.filter = {0,0};
    (void)add(world,box);
    RigidBodyQueryShape shape; shape.shape = RigidBodyShape::Sphere; shape.position = {.9F,0,0};
    for (const auto type : {RigidBodyShape::Box,RigidBodyShape::Sphere,RigidBodyShape::Capsule}) {
        shape.shape = type;
        for (const auto displacement : {math::Vec3{2,0,0},math::Vec3{-2,0,0}}) {
            const auto hit = world.castShape(shape,displacement,1U<<0U);
            expect(hit.queried && hit.hit && hit.hit->initialOverlap && hit.hit->fraction == 0 &&
                near(hit.hit->penetrationDepth,.35F,.005F) && hit.hit->normal.x > .99F &&
                near(hit.hit->pointOnBody.x,1,.005F),"all native start-overlapping shapes report outward body normal even while casting out of it");
        }
        const auto overlap = world.overlapShape(shape,1,1U<<0U);
        expect(overlap.queried && overlap.hits.size() == 1 && overlap.hits[0].initialOverlap &&
            near(overlap.hits[0].penetrationDepth,.35F,.005F) && overlap.hits[0].normal.x > .99F,
            "all three overlap shapes preserve native depth and outward-body normal");
    }
    shape.shape = RigidBodyShape::Sphere;
    box.id = "deeper"; box.position = {.5F,0,0}; box.filter.layer = 1; (void)add(world,box);
    const auto deepest = world.castShape(shape,{2,0,0});
    expect(deepest.hit && deepest.hit->body.id == "deeper" && deepest.hit->initialOverlap &&
        deepest.hit->penetrationDepth > .8F && deepest.hit->normal.x > .99F,
        "native deepest initial penetration wins among zero-fraction cast hits without applying depenetration");

    RigidBodyWorld sleeping;
    if (!init(sleeping)) return;
    RigidBodyDesc prop; prop.id = "sleeper"; prop.position = {0,1,0}; prop.restitution = 0;
    const auto body = add(sleeping,prop);
    steps(sleeping,floorWorld(),1,360);
    expect(sleeping.body(body) && sleeping.body(body)->sleeping,"query fixture first reaches real native sleep");
    shape = {}; shape.position = {0,.25F,0};
    const auto overlap = sleeping.overlapShape(shape,1,1U<<1U);
    shape.position = {-2,.25F,0};
    const auto cast = sleeping.castShape(shape,{4,0,0},1U<<1U);
    expect(overlap.queried && overlap.hits.size() == 1 && overlap.hits[0].body.handle == body &&
        cast.hit && cast.hit->body.handle == body && sleeping.body(body)->sleeping,
        "native overlap/cast detects sleeping dynamic body without waking it");
}
void testKccReplayIsUnchanged() {
    PhysicsWorld source;
    source.setBounds({12,8,12});
    for (auto collider : makeDefaultPhysicsSandboxColliders()) source.addStaticCollider(std::move(collider));
    CharacterMotorState start; start.position = {0,0,-2};
    const auto script = makeSandboxMovementScript(720);
    const auto before = runCharacterMotorReplay(source,start,script);
    RigidBodyWorld world;
    RigidBodyWorldConfig config; config.maxContactEvents = 64;
    if (!init(world,config)) return;
    RigidBodyDesc prop; prop.id = "replay_prop"; prop.position = {3,4,3};
    (void)add(world,prop);
    steps(world,source,1,720);
    const auto after = runCharacterMotorReplay(source,start,script);
    expect(before.deterministicHash == after.deterministicHash &&
        hashCharacterMotorState(before.finalState) == hashCharacterMotorState(after.finalState),
        "dynamic adapter never mutates canonical source or competitive KCC replay");
    std::cout << "KCC unchanged replay ticks=" << before.simulatedTicks << " hash=" << before.deterministicHash << '\n';
}
bool finiteSnapshot(const RigidBodySnapshot& body) {
    const auto finite3 = [](math::Vec3 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    return finite3(body.position) && finite3(body.linearVelocity) && finite3(body.angularVelocity) &&
        std::isfinite(body.rotation.x) && std::isfinite(body.rotation.y) &&
        std::isfinite(body.rotation.z) && std::isfinite(body.rotation.w);
}
void benchmarkQuery(std::string_view name, std::string_view mode, auto&& query) {
    constexpr int warmup = 32, batches = 32, callsPerBatch = 16;
    for (int i=0; i<warmup; ++i) expect(query(),"query benchmark warmup hits its actual target");
    std::vector<double> times;
    int hits = 0;
    for (int batch=0; batch<batches; ++batch) {
        const auto begin = std::chrono::steady_clock::now();
        for (int i=0; i<callsPerBatch; ++i) hits += query() ? 1 : 0;
        const auto end = std::chrono::steady_clock::now();
        times.push_back(std::chrono::duration<double,std::micro>(end-begin).count()/callsPerBatch);
    }
    expect(hits == batches*callsPerBatch,"bounded hit-only query batches all succeed");
    std::sort(times.begin(),times.end());
    std::cout << "Jolt query grid_v2 bodies=32 mode=" << mode << " query=" << name
        << " warmup_calls=" << warmup << " batches=" << batches << " calls_per_batch=" << callsPerBatch
        << " hits=" << hits << " median_us_per_call=" << times[times.size()/2]
        << " p95_us_per_call=" << times[times.size()*95/100] << '\n';
}
void benchmarkQueries(const RigidBodyWorld& world, const RigidBodySnapshot& target,
    std::string_view mode, std::uint64_t tick) {
    constexpr std::uint8_t mask = 1U<<1U; // Dynamic targets only; exclude the floor.
    benchmarkQuery("ray_hit",mode,[&]() {
        const auto hit = world.raycast(target.position+math::Vec3{0,2,0},{0,-4,0},mask);
        return hit && hit->handle == target.handle;
    });
    for (const auto type : {RigidBodyShape::Box,RigidBodyShape::Sphere,RigidBodyShape::Capsule}) {
        const std::string_view shape = type == RigidBodyShape::Box ? "box" :
            type == RigidBodyShape::Sphere ? "sphere" : "capsule";
        RigidBodyQueryShape query; query.shape = type; query.position = target.position+math::Vec3{0,2,0};
        query.halfExtents = {.1F,.1F,.1F}; query.radius = .1F; query.capsuleHalfHeight = .1F;
        benchmarkQuery(std::string(shape)+"_cast_hit",mode,[&]() {
            const auto result = world.castShape(query,{0,-4,0},mask);
            return result.queried && result.tick == tick && result.hit && result.hit->body.handle == target.handle;
        });
        query.position = target.position;
        benchmarkQuery(std::string(shape)+"_overlap_hit",mode,[&]() {
            const auto result = world.overlapShape(query,1,mask);
            return result.queried && result.tick == tick && !result.overflow && result.hits.size() == 1 &&
                result.hits.front().body.handle == target.handle;
        });
    }
    const auto after = world.body(target.handle);
    expect(after && after->sleeping == target.sleeping &&
        near(after->position.x,target.position.x,1.e-5F) && near(after->position.y,target.position.y,1.e-5F) &&
        near(after->position.z,target.position.z,1.e-5F),"query batches do not move or wake their target");
}
void benchmarkAndReplay() {
    // grid_v2 uses separated boxes on one floor. It replaces the historical
    // mixed falling-stack measurement; its timings are not directly comparable.
    for (const int count : {32,128,256}) for (const bool sleeping : {false,true}) {
        RigidBodyWorld world;
        if (!init(world)) return;
        const auto source = floorWorld();
        RigidBodyHandle queryTarget;
        for (int i=0; i<count; ++i) {
            RigidBodyDesc desc; desc.id = "bench_"+std::to_string(i);
            desc.position = {static_cast<float>(i%16)-8,1,static_cast<float>(i/16)-8};
            desc.allowSleeping = sleeping;
            const auto handle = add(world,desc);
            if (i == 0) queryTarget = handle;
        }
        std::uint64_t tick = 1;
        auto last = world.step(source,tick);
        expect(last.stepped && last.staticBodiesCreated == 1 && last.staticBodiesRemoved == 0 &&
            last.staticBodiesUpdated == 0,"benchmark first step derives exactly one canonical floor");
        if (!last.stepped) return;
        const auto untimedStep = [&]() {
            auto result = world.step(source,++tick);
            expect(result.stepped && result.staticBodiesCreated == 0 && result.staticBodiesRemoved == 0 &&
                result.staticBodiesUpdated == 0,"benchmark warmup reuses the unchanged canonical floor");
            return result;
        };
        int settleTicks = 0;
        while (sleeping && last.activeDynamicCount != 0 && settleTicks < 600 && last.stepped) {
            last = untimedStep(); ++settleTicks;
        }
        expect(!sleeping || (last.stepped && last.activeDynamicCount == 0),"sleeping benchmark reaches native sleep before timing");
        if (!last.stepped || (sleeping && last.activeDynamicCount != 0)) return;
        for (int i=0; i<30; ++i) last = untimedStep();
        if (!last.stepped) return;
        std::vector<double> times;
        std::uint32_t minimumActive = count, maximumActive = 0, created = 0, removed = 0, updated = 0;
        for (int sample=0; sample<270; ++sample) {
            const auto begin = std::chrono::steady_clock::now();
            last = world.step(source,++tick);
            const auto end = std::chrono::steady_clock::now();
            expect(last.stepped,"bounded object count benchmark step succeeds");
            if (!last.stepped) return;
            minimumActive = std::min(minimumActive,last.activeDynamicCount);
            maximumActive = std::max(maximumActive,last.activeDynamicCount);
            created += last.staticBodiesCreated; removed += last.staticBodiesRemoved; updated += last.staticBodiesUpdated;
            times.push_back(std::chrono::duration<double,std::micro>(end-begin).count());
        }
        const std::string_view mode = sleeping ? "sleeping_verified" : "active_no_sleep";
        expect(minimumActive == (sleeping ? 0U : static_cast<std::uint32_t>(count)) && minimumActive == maximumActive,
            "timed steps retain the declared native active/sleeping body count");
        expect(created == 0 && removed == 0 && updated == 0,"timed steps never recreate or update unchanged statics");
        std::sort(times.begin(),times.end());
        std::cout << "Jolt step grid_v2 single-thread fixed60Hz bodies=" << count << " mode=" << mode
            << " initial_statics=1 settle_ticks=" << settleTicks << " warmup=30 samples=270 active_min=" << minimumActive
            << " active_max=" << maximumActive << " static_created=" << created << " static_removed=" << removed
            << " static_updated=" << updated << " median_us=" << times[times.size()/2]
            << " p95_us=" << times[times.size()*95/100] << '\n';
        for (const auto& body : world.bodies()) {
            expect(finiteSnapshot(body),"benchmark ends with finite full poses and velocities");
            if (body.motion == RigidBodyMotion::Dynamic)
                expect(body.sleeping == sleeping,"snapshot sleeping flags agree with measured native activity");
        }
        if (count == 32) {
            const auto target = world.body(queryTarget);
            expect(target.has_value(),"query benchmark retains its actual dynamic target");
            if (target) benchmarkQueries(world,*target,mode,tick);
        }
    }
    std::vector<std::vector<RigidBodySnapshot>> first;
    bool observedZMotion = false, observedAngularMotion = false;
    for (int repeat=0; repeat<2; ++repeat) {
        RigidBodyWorld world;
        if (!init(world)) return;
        RigidBodyHandle impulseTarget;
        for (int i=0; i<8; ++i) {
            RigidBodyDesc desc; desc.id = "replay"+std::to_string(i); desc.position = {0,1.0F+static_cast<float>(i)*0.6F,0};
            desc.materialId = 100+i; desc.surfaceId = 200;
            impulseTarget = add(world,desc);
        }
        const auto source = floorWorld();
        std::size_t checkpoint = 0;
        for (std::uint64_t tick=1; tick<=300; ++tick) {
            if (tick == 179) {
                const auto target = world.body(impulseTarget);
                expect(target && world.applyImpulse(impulseTarget,{3,2,-1},target->position+math::Vec3{.12F,.18F,.07F}),
                    "authored replay applies the same off-centre impulse before its moving checkpoint");
            }
            expect(world.step(source,tick).stepped,"same-build replay advances each authored fixed tick");
            if (tick != 60 && tick != 180 && tick != 300) continue;
            const auto state = world.bodies();
            for (const auto& body : state) {
                expect(finiteSnapshot(body) && world.body(body.handle).has_value(),"replay checkpoints contain finite live owned snapshots");
                observedZMotion |= std::abs(body.linearVelocity.z) > .001F;
                observedAngularMotion |= body.angularVelocity.lengthSquared() > .001F;
            }
            if (repeat==0) first.push_back(state);
            else {
                const auto& expected = first[checkpoint];
                expect(state.size()==expected.size(),"same authored replay has same checkpoint snapshot count");
                if (state.size()==expected.size()) for (std::size_t i=0;i<state.size();++i) {
                    const auto& a = state[i]; const auto& b = expected[i];
                    const auto same3 = [](math::Vec3 x, math::Vec3 y) {
                        return near(x.x,y.x,1.e-5F) && near(x.y,y.y,1.e-5F) && near(x.z,y.z,1.e-5F);
                    };
                    expect(a.id == b.id && a.handle.world != b.handle.world,"replay matches application IDs across independent world identities");
                    expect(same3(a.position,b.position),"same-build replay repeats full XYZ position");
                    expect(near(a.rotation.x,b.rotation.x,1.e-5F) && near(a.rotation.y,b.rotation.y,1.e-5F) &&
                        near(a.rotation.z,b.rotation.z,1.e-5F) && near(a.rotation.w,b.rotation.w,1.e-5F),
                        "same-build replay repeats all four quaternion components");
                    expect(same3(a.linearVelocity,b.linearVelocity) && same3(a.angularVelocity,b.angularVelocity),
                        "same-build replay repeats full linear and angular velocities");
                    expect(a.motion == b.motion && a.sleeping == b.sleeping && a.canonicalCollider == b.canonicalCollider &&
                        a.isSensor == b.isSensor && a.materialId == b.materialId && a.surfaceId == b.surfaceId,
                        "same-build replay repeats motion/sleep/canonical/sensor flags and application tags");
                }
            }
            ++checkpoint;
        }
    }
    expect(first.size() == 3 && observedZMotion && observedAngularMotion,
        "full-state replay includes three checkpoints and real out-of-plane/angular motion");
    std::cout << "Jolt same-build replay checkpoints=3 full_pose_and_velocities=true tolerance=1e-5\n";
}
} // namespace
int main() {
    testUnavailableAndInvalidWorld();
    testShapeQueryBoundaryAndUnavailable();
    PhysicsWorld replaySource;
    replaySource.setBounds({12,8,12});
    for (auto collider : makeDefaultPhysicsSandboxColliders()) replaySource.addStaticCollider(std::move(collider));
    CharacterMotorState replayStart; replayStart.position = {0,0,-2};
    const auto replay = runCharacterMotorReplay(replaySource,replayStart,makeSandboxMovementScript(720));
    expect(replay.stable,"original KCC replay remains stable in both dependency configurations");
    std::cout << "KCC configuration replay ticks=" << replay.simulatedTicks << " hash=" << replay.deterministicHash << '\n';
    if (RigidBodyWorld::backendAvailable()) {
        testShapesStackSleepAndImpulse(); testOwnershipLifetimeAndReset(); testCanonicalChangesAndRamps();
        testFiltersCcdAndValidation(); testKinematicProxyAndMovingSupport(); testOrientedShapeRestitutionAndCapacity();
        testRemovedSupportWakesSleepingBody(); testKccReplayIsUnchanged(); benchmarkAndReplay();
        testSensorLifecycleTagsAndReuse(); testSleepingSensorAndCanonicalMaterialTags(); testContactCaptureBoundsAndSensorValidation();
        testCanonicalSensorReplacementAndSortedReplay();
        testNativeShapeCasts(); testOverlapBoundsSensorsAndLifetime(); testNativeInitialOverlapAndSleepingQueries();
    }
    if (failures) { std::cerr << failures << " rigid-body test(s) failed\n"; return 1; }
    std::cout << "NovaCore rigid-body tests passed (backend=" << (RigidBodyWorld::backendAvailable() ? "Jolt5.6.0" : "disabled") << ")\n";
}
