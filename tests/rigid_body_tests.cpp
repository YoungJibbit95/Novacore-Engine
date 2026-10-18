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
void testKccReplayIsUnchanged() {
    PhysicsWorld source;
    source.setBounds({12,8,12});
    for (auto collider : makeDefaultPhysicsSandboxColliders()) source.addStaticCollider(std::move(collider));
    CharacterMotorState start; start.position = {0,0,-2};
    const auto script = makeSandboxMovementScript(720);
    const auto before = runCharacterMotorReplay(source,start,script);
    RigidBodyWorld world;
    if (!init(world)) return;
    RigidBodyDesc prop; prop.id = "replay_prop"; prop.position = {3,4,3};
    (void)add(world,prop);
    steps(world,source,1,720);
    const auto after = runCharacterMotorReplay(source,start,script);
    expect(before.deterministicHash == after.deterministicHash &&
        hashCharacterMotorState(before.finalState) == hashCharacterMotorState(after.finalState),
        "dynamic adapter never mutates canonical source or competitive KCC replay");
    std::cout << "KCC unchanged replay ticks=" << before.simulatedTicks << " hash=" << before.deterministicHash << '\n';
}
void benchmarkAndReplay() {
    for (const int count : {32,128,256}) {
        RigidBodyWorld world;
        if (!init(world)) return;
        const auto source = floorWorld();
        for (int i=0; i<count; ++i) {
            RigidBodyDesc desc; desc.id = "bench_"+std::to_string(i);
            desc.position = {static_cast<float>(i%16)-8,1.0F+0.6F*static_cast<float>(i/16),0};
            (void)add(world,desc);
        }
        std::vector<double> times;
        for (std::uint64_t tick=1; tick<=300; ++tick) {
            const auto begin = std::chrono::steady_clock::now();
            expect(world.step(source,tick).stepped,"bounded object count benchmark step succeeds");
            const auto end = std::chrono::steady_clock::now();
            if (tick>30) times.push_back(std::chrono::duration<double,std::micro>(end-begin).count());
        }
        std::sort(times.begin(),times.end());
        std::cout << "Jolt single-thread fixed60Hz bodies=" << count << " warmup=30 samples=270 median_us="
            << times[times.size()/2] << " p95_us=" << times[times.size()*95/100] << '\n';
        for (const auto& body : world.bodies()) expect(std::isfinite(body.position.y) && std::isfinite(body.rotation.w),
            "stack benchmark ends with finite snapshots");
    }
    std::vector<RigidBodySnapshot> first;
    for (int repeat=0; repeat<2; ++repeat) {
        RigidBodyWorld world;
        if (!init(world)) return;
        for (int i=0; i<8; ++i) {
            RigidBodyDesc desc; desc.id = "replay"+std::to_string(i); desc.position = {0,1.0F+static_cast<float>(i)*0.6F,0};
            (void)add(world,desc);
        }
        steps(world,floorWorld(),1,300);
        const auto state = world.bodies();
        if (repeat==0) first=state;
        else {
            expect(state.size()==first.size(),"same authored replay has same snapshot identities");
            if (state.size()==first.size()) for (std::size_t i=0;i<state.size();++i)
                expect(state[i].id == first[i].id && near(state[i].position.x,first[i].position.x,1.0e-5F) &&
                    near(state[i].position.y,first[i].position.y,1.0e-5F) && near(state[i].rotation.w,first[i].rotation.w,1.0e-5F),
                    "same build and fixed inputs repeat bounded rigid-body state");
        }
    }
}
} // namespace
int main() {
    testUnavailableAndInvalidWorld();
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
    }
    if (failures) { std::cerr << failures << " rigid-body test(s) failed\n"; return 1; }
    std::cout << "NovaCore rigid-body tests passed (backend=" << (RigidBodyWorld::backendAvailable() ? "Jolt5.6.0" : "disabled") << ")\n";
}
