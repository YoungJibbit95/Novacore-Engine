#include "novacore/physics/CharacterController.hpp"
#include "novacore/physics/MovementSimulation.hpp"
#include "novacore/physics/PhysicsSystem.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

[[nodiscard]] bool near(float lhs, float rhs, float epsilon = 0.002F) {
    return std::abs(lhs - rhs) <= epsilon;
}

[[nodiscard]] bool hasContact(
    const std::vector<novacore::physics::CharacterContact>& contacts,
    std::string_view colliderId,
    novacore::physics::CharacterContactRole role) {
    return std::any_of(contacts.begin(), contacts.end(), [&](const auto& contact) {
        return contact.colliderId == colliderId && contact.role == role;
    });
}

novacore::physics::PhysicsWorld makeCornerWorld(bool reverseInsertion) {
    using namespace novacore::physics;
    const StaticCollider wallX{
        "corner_x", SurfaceKind::Wall, {2.0F, 1.0F, 0.0F}, {0.10F, 1.0F, 4.0F}, true};
    const StaticCollider wallZ{
        "corner_z", SurfaceKind::Wall, {0.0F, 1.0F, 2.0F}, {4.0F, 1.0F, 0.10F}, true};
    PhysicsWorld world;
    world.setBounds({12.0F, 8.0F, 12.0F});
    if (reverseInsertion) {
        world.addStaticCollider(wallZ);
        world.addStaticCollider(wallX);
    } else {
        world.addStaticCollider(wallX);
        world.addStaticCollider(wallZ);
    }
    return world;
}

void testDeterministicCornerManifold() {
    using namespace novacore::physics;
    CharacterSweepQuery query{};
    query.startPosition = {0.0F, 0.0F, 0.0F};
    query.desiredDisplacement = {4.0F, 0.0F, 4.0F};
    query.radius = 0.42F;
    query.height = 1.80F;
    query.enableGroundSnap = false;
    query.enableStepUp = false;
    query.maxIterations = 6;
    query.deltaSeconds = 1.0F / 60.0F;

    const auto forward = makeCornerWorld(false).sweepCharacter(query);
    const auto reverse = makeCornerWorld(true).sweepCharacter(query);
    expect(forward.hit, "diagonal capsule sweep hits the corner");
    expect(forward.sweepContacts.size() >= 2U, "corner sweep emits a multi-contact manifold");
    expect(hasContact(forward.sweepContacts, "corner_x", CharacterContactRole::Sweep),
        "corner manifold contains the X wall");
    expect(hasContact(forward.sweepContacts, "corner_z", CharacterContactRole::Sweep),
        "corner manifold contains the Z wall");
    expect(forward.slidePlaneCount >= 2U, "corner sweep tracks both clipping planes");
    expect(forward.stoppedOnCrease, "two perpendicular walls stop displacement on their crease");
    expect(forward.resolve.position.x < 1.50F && forward.resolve.position.z < 1.50F,
        "capsule remains outside both corner walls");
    expect(forward.contactHash != 0U, "sweep manifold has a deterministic hash");
    expect(forward.contactHash == reverse.contactHash,
        "contact hash is independent of collider insertion order");
    expect(near(forward.resolve.position.x, reverse.resolve.position.x) &&
        near(forward.resolve.position.z, reverse.resolve.position.z),
        "corner resolution is independent of collider insertion order");
}

void testRoundedCornerSweep() {
    using namespace novacore::physics;
    PhysicsWorld world;
    world.setBounds({20.0F, 8.0F, 20.0F});
    world.addStaticCollider(StaticCollider{
        "small_block", SurfaceKind::Wall, {3.0F, 1.0F, 3.0F}, {0.5F, 1.0F, 0.5F}, true});
    CharacterSweepQuery query{};
    query.startPosition = {0.0F, 0.0F, 0.0F};
    query.desiredDisplacement = {8.0F, 0.0F, 8.0F};
    query.radius = 0.50F;
    query.height = 1.80F;
    query.enableGroundSnap = false;
    query.enableStepUp = false;
    const auto sweep = world.sweepCharacter(query);
    expect(sweep.hit, "high speed capsule sweep catches a rounded AABB corner");
    expect(sweep.hitColliderId == "small_block", "rounded corner sweep reports collider id");
    expect(sweep.hitNormal.x < -0.60F && sweep.hitNormal.z < -0.60F,
        "rounded corner sweep reports a radial corner normal");
    expect(sweep.firstHitFraction > 0.20F && sweep.firstHitFraction < 0.40F,
        "rounded corner sweep reports a stable impact fraction");
}

novacore::physics::PhysicsWorld makeStepWorld(bool addLowCeiling) {
    using namespace novacore::physics;
    PhysicsWorld world;
    world.setBounds({8.0F, 5.0F, 8.0F});
    world.addStaticCollider(StaticCollider{
        "step", SurfaceKind::Cover, {0.0F, 0.16F, 1.0F}, {1.0F, 0.16F, 0.40F},
        true, RampDirection::None, 0.32F});
    if (addLowCeiling) {
        world.addStaticCollider(StaticCollider{
            "step_ceiling", SurfaceKind::Wall, {0.0F, 1.62F, 1.0F}, {1.0F, 0.18F, 0.40F}, true});
    }
    return world;
}

void testStepClearanceAndTelemetry() {
    using namespace novacore::physics;
    CharacterSweepQuery query{};
    query.startPosition = {0.0F, 0.0F, 0.0F};
    query.desiredDisplacement = {0.0F, 0.0F, 1.10F};
    query.radius = 0.42F;
    query.height = 1.80F;
    query.maxStepHeight = 0.42F;
    query.enableStepUp = true;
    query.enableGroundSnap = true;
    const auto clearStep = makeStepWorld(false).sweepCharacter(query);
    expect(clearStep.stepped, "clear low obstacle performs a step-up");
    expect(clearStep.resolve.grounded, "step-up ends on walkable support");
    expect(clearStep.resolve.groundColliderId == "step", "step-up records support collider");
    expect(clearStep.stepHeight > 0.30F && clearStep.stepHeight < 0.34F,
        "step-up reports deterministic rise height");

    const auto blockedStep = makeStepWorld(true).sweepCharacter(query);
    expect(!blockedStep.stepped, "low ceiling rejects step-up without capsule clearance");
    expect(blockedStep.hit, "rejected step remains a blocking sweep");
    expect(blockedStep.resolve.position.z < clearStep.resolve.position.z - 0.30F,
        "rejected step cannot tunnel beneath low ceiling");
}

void testUnwalkableSlopeBlocksClimb() {
    using namespace novacore::physics;
    PhysicsWorld world;
    world.setBounds({10.0F, 8.0F, 10.0F});
    world.addStaticCollider(StaticCollider{
        "steep_ramp", SurfaceKind::Ramp, {0.0F, 1.0F, 0.0F}, {1.5F, 1.0F, 1.0F},
        true, RampDirection::PositiveZ});
    CharacterSweepQuery query{};
    query.startPosition = {0.0F, 0.0F, -2.0F};
    query.desiredDisplacement = {0.0F, 0.0F, 4.0F};
    query.radius = 0.42F;
    query.height = 1.80F;
    query.walkableSlopeCosine = 0.80F;
    query.enableGroundSnap = false;
    query.enableStepUp = false;
    const auto sweep = world.sweepCharacter(query);
    expect(sweep.hit, "unwalkable slope blocks an uphill capsule sweep");
    expect(sweep.hitColliderId == "steep_ramp", "steep slope reports ramp collider");
    expect(!sweep.sweepContacts.empty() && !sweep.sweepContacts.front().walkable,
        "steep slope contact is explicitly non-walkable");
    expect(sweep.resolve.position.z < -1.35F, "controller does not climb through steep ramp");
    expect(!sweep.resolve.grounded || sweep.resolve.groundColliderId != "steep_ramp",
        "steep ramp is never selected as ground support");
}

void testMovingSupportIntegrationAndJumpInheritance() {
    using namespace novacore::physics;
    PhysicsWorld world;
    world.setBounds({12.0F, 8.0F, 12.0F});
    world.addStaticCollider(StaticCollider{
        "moving_support", SurfaceKind::Cover, {0.0F, 0.16F, 0.0F}, {1.25F, 0.16F, 1.25F},
        true, RampDirection::None, 0.32F, {0.80F, 0.10F, 0.0F}});
    CharacterMotorConfig config{};
    CharacterMotorState state{};
    state.position = {0.0F, 0.32F, 0.0F};
    state.capsuleHeight = config.standingHeight;
    constexpr float dt = 1.0F / 60.0F;
    std::uint32_t carriedTicks = 0;
    std::uint32_t contactTicks = 0;
    for (std::uint32_t tick = 0; tick < 45U; ++tick) {
        const auto step = stepCharacterMotor(world, state, {}, config, dt);
        carriedTicks += step.carriedBySupport ? 1U : 0U;
        contactTicks += step.telemetry.contactCount > 0U ? 1U : 0U;
        state = step.state;
        expect(world.advanceKinematicColliders(dt) == 1U,
            "kinematic world step advances exactly one support");
    }
    const auto* support = world.findStaticCollider("moving_support");
    expect(support != nullptr, "moving support remains addressable by stable id");
    expect(carriedTicks >= 40U, "character remains carried by moving support");
    expect(contactTicks >= 40U, "moving support produces stable contact telemetry");
    expect(support != nullptr && near(state.position.x, support->center.x, 0.035F),
        "character and support preserve their relative horizontal anchor");
    expect(support != nullptr && near(state.position.y, support->center.y + support->halfExtents.y, 0.035F),
        "character follows vertical support motion without ground loss");

    CharacterMotorInput jump{};
    jump.jumpPressed = true;
    const auto jumpStep = stepCharacterMotor(world, state, jump, config, dt);
    expect(jumpStep.jumped, "character jumps from moving support");
    expect(jumpStep.state.velocity.x > 0.75F, "jump inherits horizontal support velocity");
    expect(jumpStep.state.velocity.y > config.jumpSpeed - (config.gravity * dt) + 0.05F,
        "jump inherits upward support velocity");
    expect(hasCharacterMotorEvent(jumpStep.telemetry.events, CharacterMotorEvent::Jumped),
        "jump event is present in deterministic telemetry");
}

void testLandingSnapAndReplayDeterminism() {
    using namespace novacore::physics;
    PhysicsWorld world;
    world.setBounds({10.0F, 8.0F, 10.0F});
    CharacterMotorConfig config{};
    config.hardLandingSpeed = 6.0F;
    config.landingRecoverySeconds = 0.25F;
    CharacterMotorState falling{};
    falling.position = {0.0F, 4.0F, 0.0F};
    falling.capsuleHeight = config.standingHeight;
    falling.timeSinceGrounded = 1.0F;
    falling.airborneSeconds = 1.0F;
    std::uint32_t landingEvents = 0;
    float impactSpeed = 0.0F;
    for (std::uint32_t tick = 0; tick < 180U; ++tick) {
        const auto step = stepCharacterMotor(world, falling, {}, config, 1.0F / 60.0F);
        falling = step.state;
        if (step.landed) {
            ++landingEvents;
            impactSpeed = step.impactSpeed;
            expect(step.hardLanded, "high fall emits hard landing state");
            expect(hasCharacterMotorEvent(step.telemetry.events, CharacterMotorEvent::Landed),
                "landing event is present in telemetry on impact tick");
            expect(hasCharacterMotorEvent(step.telemetry.events, CharacterMotorEvent::HardLanded),
                "hard landing event is present in telemetry on impact tick");
        }
    }
    expect(landingEvents == 1U, "landing transition emits exactly once");
    expect(impactSpeed > 10.0F, "landing telemetry preserves pre-impact vertical speed");
    expect(falling.grounded && near(falling.position.y, 0.0F), "fall settles on world floor");
    expect(falling.lastImpactSpeed == impactSpeed, "motor state preserves last impact speed");

    CharacterMotorState snapState{};
    snapState.position = {0.0F, 0.20F, 0.0F};
    snapState.grounded = true;
    snapState.capsuleHeight = config.standingHeight;
    const auto snap = stepCharacterMotor(world, snapState, {}, config, 1.0F / 60.0F);
    expect(hasCharacterMotorEvent(snap.telemetry.events, CharacterMotorEvent::GroundSnapped),
        "ground snap event captures initial support correction");
    expect(snap.telemetry.groundSnapDistance > 0.15F, "ground snap telemetry reports correction distance");

    auto replayWorld = makeStepWorld(false);
    CharacterMotorState start{};
    start.position = {0.0F, 0.0F, -2.0F};
    start.capsuleHeight = config.standingHeight;
    const auto script = makeSandboxMovementScript(120U);
    CharacterMotorReplayDesc desc{};
    desc.motor = config;
    const auto first = runCharacterMotorReplay(replayWorld, start, script, desc);
    const auto second = runCharacterMotorReplay(replayWorld, start, script, desc);
    expect(first.stable && second.stable, "movement replays remain numerically stable");
    expect(first.deterministicHash != 0U, "movement replay produces deterministic hash");
    expect(first.deterministicHash == second.deterministicHash,
        "identical movement replay produces identical telemetry hash");
    expect(hashCharacterMotorState(first.finalState) == hashCharacterMotorState(second.finalState),
        "identical movement replay produces identical final state hash");
    expect(first.maximumContactCount > 0U, "replay aggregates contact manifold telemetry");
}

void testCoyoteTimeAndJumpBuffer() {
    using namespace novacore::physics;
    PhysicsWorld world;
    world.setBounds({8.0F, 8.0F, 8.0F});
    CharacterMotorConfig config{};
    CharacterMotorInput jump{};
    jump.jumpPressed = true;

    CharacterMotorState coyote{};
    coyote.position = {0.0F, 0.80F, 0.0F};
    coyote.capsuleHeight = config.standingHeight;
    coyote.groundedSeconds = 0.25F;
    coyote.timeSinceGrounded = config.coyoteTimeSeconds * 0.50F;
    const auto coyoteJump = stepCharacterMotor(world, coyote, jump, config, 1.0F / 60.0F);
    expect(coyoteJump.jumped, "coyote window accepts a jump shortly after support loss");

    coyote.timeSinceGrounded = config.coyoteTimeSeconds + 0.05F;
    const auto expiredJump = stepCharacterMotor(world, coyote, jump, config, 1.0F / 60.0F);
    expect(!expiredJump.jumped, "expired coyote window rejects an airborne jump");

    CharacterMotorState buffered{};
    buffered.position = {0.0F, 0.50F, 0.0F};
    buffered.velocity = {0.0F, -5.0F, 0.0F};
    buffered.capsuleHeight = config.standingHeight;
    buffered.airborneSeconds = 0.30F;
    buffered.timeSinceGrounded = 0.30F;
    auto bufferedStep = stepCharacterMotor(world, buffered, jump, config, 1.0F / 60.0F);
    expect(!bufferedStep.jumped && bufferedStep.state.jumpBufferRemaining > 0.0F,
        "airborne jump input enters the deterministic jump buffer");
    bool firedBufferedJump = false;
    for (std::uint32_t tick = 0; tick < 8U; ++tick) {
        bufferedStep = stepCharacterMotor(world, bufferedStep.state, {}, config, 1.0F / 60.0F);
        firedBufferedJump = firedBufferedJump || bufferedStep.jumped;
    }
    expect(firedBufferedJump, "buffered jump fires when the character reaches walkable ground");
}

} // namespace

int main() {
    testDeterministicCornerManifold();
    testRoundedCornerSweep();
    testStepClearanceAndTelemetry();
    testUnwalkableSlopeBlocksClimb();
    testMovingSupportIntegrationAndJumpInheritance();
    testLandingSnapAndReplayDeterminism();
    testCoyoteTimeAndJumpBuffer();
    if (failures != 0) {
        std::cerr << failures << " physics KCC test(s) failed\n";
        return 1;
    }
    std::cout << "NovaCore physics KCC tests passed\n";
    return 0;
}
