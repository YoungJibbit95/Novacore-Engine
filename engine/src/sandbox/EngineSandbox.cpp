#include "novacore/sandbox/EngineSandbox.hpp"

#include "novacore/physics/MovementSimulation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace novacore::sandbox {

namespace {

[[nodiscard]] bool finite(novacore::math::Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] std::array<float, 4> colliderColor(novacore::physics::SurfaceKind kind, bool blocksMovement) {
    if (!blocksMovement) {
        return {0.46F, 0.28F, 0.82F, 0.38F};
    }
    switch (kind) {
    case novacore::physics::SurfaceKind::Floor:
        return {0.28F, 0.34F, 0.36F, 1.0F};
    case novacore::physics::SurfaceKind::Ramp:
        return {0.26F, 0.54F, 0.42F, 0.92F};
    case novacore::physics::SurfaceKind::Slide:
        return {0.22F, 0.62F, 0.54F, 0.92F};
    case novacore::physics::SurfaceKind::WallRun:
        return {0.12F, 0.70F, 0.86F, 0.92F};
    case novacore::physics::SurfaceKind::Ledge:
        return {0.72F, 0.58F, 0.28F, 0.95F};
    case novacore::physics::SurfaceKind::Cover:
        return {0.42F, 0.48F, 0.44F, 0.95F};
    case novacore::physics::SurfaceKind::Wall:
        return {0.38F, 0.42F, 0.46F, 0.95F};
    case novacore::physics::SurfaceKind::Trigger:
        return {0.46F, 0.28F, 0.82F, 0.38F};
    }
    return {0.50F, 0.50F, 0.50F, 1.0F};
}

void appendColliderPreview(
    novacore::render::RenderFrameInfo& frame,
    const novacore::physics::StaticCollider& collider) {
    frame.worldBoxes.push_back(novacore::render::RenderBox3D{
        collider.center,
        collider.halfExtents,
        colliderColor(collider.kind, collider.blocksMovement),
    });
}

void appendTrajectoryPreview(
    novacore::render::RenderFrameInfo& frame,
    const std::vector<novacore::math::Vec3>& trajectory) {
    if (trajectory.size() < 2U) {
        return;
    }
    for (std::size_t index = 1; index < trajectory.size(); index += 2U) {
        frame.worldLines.push_back(novacore::render::RenderLine3D{
            trajectory[index - 1U] + novacore::math::Vec3{0.0F, 0.10F, 0.0F},
            trajectory[index] + novacore::math::Vec3{0.0F, 0.10F, 0.0F},
            {0.96F, 0.82F, 0.20F, 0.95F},
        });
    }
}

[[nodiscard]] std::string buildSummary(const EngineSandboxRunResult& result) {
    std::ostringstream stream;
    stream << "NovaCore Engine Sandbox"
           << " ticks=" << result.simulatedTicks
           << " grounded=" << result.groundedTicks
           << " swept=" << result.sweptTicks
           << " wall=" << result.wallProbeTicks
           << " colliders=" << result.physicsStats.staticColliderCount
           << " blocking=" << result.physicsStats.blockingColliderCount
           << " final=(" << std::fixed << std::setprecision(2)
           << result.finalCharacter.position.x << ", "
           << result.finalCharacter.position.y << ", "
           << result.finalCharacter.position.z << ")"
           << " velocity=("
           << result.finalCharacter.velocity.x << ", "
           << result.finalCharacter.velocity.y << ", "
           << result.finalCharacter.velocity.z << ")"
           << " stable=" << (result.stable ? "yes" : "no");
    return stream.str();
}

} // namespace

EngineSandboxRunResult runEngineSandbox(const EngineSandboxOptions& options) {
    physics::PhysicsWorld world;
    world.setBounds({14.0F, 8.0F, 14.0F});
    for (auto collider : physics::makeDefaultPhysicsSandboxColliders()) {
        world.addStaticCollider(std::move(collider));
    }

    physics::CharacterMotorConfig motorConfig{};
    physics::CharacterMotorState motor{};
    motor.position = {0.0F, 0.0F, -3.5F};
    motor.capsuleHeight = motorConfig.standingHeight;

    EngineSandboxRunResult result{};
    result.physicsStats = physics::summarizePhysicsWorld(world);
    const std::uint32_t ticks = std::clamp(options.tickCount, 1U, 720U);
    const float dt = std::clamp(options.fixedDeltaSeconds, 1.0F / 240.0F, 1.0F / 20.0F);
    auto script = physics::makeSandboxMovementScript(ticks);
    if (!options.sprint) {
        for (auto& command : script) {
            command.input.sprintHeld = false;
        }
    }
    physics::CharacterMotorReplayDesc replayDesc{};
    replayDesc.motor = motorConfig;
    replayDesc.step.fixedDeltaSeconds = dt;
    replayDesc.keepFrames = true;
    const auto replay = physics::runCharacterMotorReplay(world, motor, script, replayDesc);

    result.trajectory.reserve((ticks / 6U) + 2U);
    result.trajectory.push_back(motor.position);
    for (std::size_t index = 0; index < replay.frames.size(); ++index) {
        if ((index % 6U) == 0U) {
            result.trajectory.push_back(replay.frames[index].step.state.position);
        }
    }

    result.simulatedTicks = replay.simulatedTicks;
    result.groundedTicks = replay.groundedTicks;
    result.sweptTicks = replay.sweptTicks;
    result.wallProbeTicks = replay.wallProbeTicks;
    result.finalCharacter = replay.finalState;
    result.stable = replay.stable &&
        finite(result.finalCharacter.position) &&
        finite(result.finalCharacter.velocity) &&
        result.simulatedTicks == ticks &&
        result.groundedTicks > 0U &&
        result.physicsStats.blockingColliderCount >= 4U;

    if (options.emitPreviewFrame) {
        result.previewFrame.sky.enabled = true;
        result.previewFrame.camera3D.enabled = true;
        result.previewFrame.camera3D.position = {0.0F, 6.0F, -10.0F};
        result.previewFrame.camera3D.pitchDegrees = -24.0F;
        result.previewFrame.camera3D.verticalFovDegrees = 72.0F;
        result.previewFrame.worldBoxes.push_back(novacore::render::RenderBox3D{
            {0.0F, -0.04F, 0.0F},
            {8.0F, 0.04F, 8.0F},
            {0.24F, 0.30F, 0.32F, 1.0F},
        });
        for (const auto& collider : world.staticColliders()) {
            appendColliderPreview(result.previewFrame, collider);
        }
        result.previewFrame.worldBoxes.push_back(novacore::render::RenderBox3D{
            result.finalCharacter.position + novacore::math::Vec3{0.0F, result.finalCharacter.capsuleHeight * 0.50F, 0.0F},
            {motorConfig.radius, result.finalCharacter.capsuleHeight * 0.50F, motorConfig.radius},
            {0.96F, 0.82F, 0.20F, 0.96F},
        });
        appendTrajectoryPreview(result.previewFrame, result.trajectory);
    }

    result.summary = buildSummary(result);
    return result;
}

} // namespace novacore::sandbox
