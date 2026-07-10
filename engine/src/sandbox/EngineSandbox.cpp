#include "novacore/sandbox/EngineSandbox.hpp"

#include "novacore/animation/AnimationRuntime.hpp"
#include "novacore/assets/AssetManifest.hpp"
#include "novacore/core/ConfigDocument.hpp"
#include "novacore/core/FixedStep.hpp"
#include "novacore/ecs/Components.hpp"
#include "novacore/ecs/World.hpp"
#include "novacore/net/BitStream.hpp"
#include "novacore/net/Loopback.hpp"
#include "novacore/physics/MovementSimulation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <utility>

namespace novacore::sandbox {

namespace {

constexpr std::string_view kFixedStepScenario = "fixed_step_clock";
constexpr std::string_view kEcsLifecycleScenario = "ecs_lifecycle";
constexpr std::string_view kNetLoopbackScenario = "net_loopback_packets";
constexpr std::string_view kAssetManifestScenario = "asset_manifest_parse";
constexpr std::string_view kMovementReplayScenario = "movement_replay";
constexpr std::string_view kMovingSupportScenario = "moving_support";
constexpr std::string_view kAnimationBlendScenario = "animation_blend";

[[nodiscard]] bool finite(novacore::math::Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] std::string toString(std::string_view value) {
    return std::string(value.data(), value.size());
}

[[nodiscard]] EngineSandboxMetric metric(std::string name, double value, std::string unit = {}) {
    EngineSandboxMetric result{};
    result.name = std::move(name);
    result.value = value;
    result.unit = std::move(unit);
    return result;
}

[[nodiscard]] EngineSandboxEvent event(std::uint32_t tick, std::string channel, std::string message) {
    EngineSandboxEvent result{};
    result.tick = tick;
    result.channel = std::move(channel);
    result.message = std::move(message);
    return result;
}

[[nodiscard]] int exitValue(EngineSandboxExitCode code) {
    switch (code) {
    case EngineSandboxExitCode::Success:
        return 0;
    case EngineSandboxExitCode::ScenarioFailure:
        return 1;
    case EngineSandboxExitCode::InvalidArguments:
        return 2;
    case EngineSandboxExitCode::NoScenarioSelected:
        return 3;
    }
    return 1;
}

[[nodiscard]] bool wantsScenario(const EngineSandboxOptions& options, std::string_view id) {
    if (options.scenarioIds.empty()) {
        return true;
    }

    return std::any_of(
        options.scenarioIds.begin(),
        options.scenarioIds.end(),
        [id](const std::string& requested) {
            return requested == id;
        });
}

[[nodiscard]] std::string passText(bool passed) {
    return passed ? "pass" : "fail";
}

[[nodiscard]] std::string formatMetricValue(double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << value;
    return stream.str();
}

[[nodiscard]] std::string escapeJson(std::string_view value) {
    std::ostringstream stream;
    for (const unsigned char character : value) {
        switch (character) {
        case '"':
            stream << "\\\"";
            break;
        case '\\':
            stream << "\\\\";
            break;
        case '\n':
            stream << "\\n";
            break;
        case '\r':
            stream << "\\r";
            break;
        case '\t':
            stream << "\\t";
            break;
        default:
            stream << static_cast<char>(character);
            break;
        }
    }
    return stream.str();
}

[[nodiscard]] EngineSandboxScenarioResult runFixedStepClockScenario(const EngineSandboxOptions& options) {
    EngineSandboxScenarioResult scenario{};
    scenario.id = toString(kFixedStepScenario);
    scenario.name = "Fixed-step clock smoke";

    const auto ticks = std::clamp(options.tickCount, 1U, 7200U);
    const double dt = std::clamp(static_cast<double>(options.fixedDeltaSeconds), 1.0 / 240.0, 1.0 / 15.0);
    core::FixedStepAccumulator accumulator(core::FixedStepConfig{1.0 / dt, 0.25});

    constexpr std::array<double, 5> framePattern{0.5, 0.5, 1.5, 0.5, 1.0};
    std::uint32_t frames = 0;
    std::uint32_t maxSubsteps = 0;
    const std::uint32_t checkpoint = std::max(1U, ticks / 3U);
    std::uint64_t lastCheckpoint = 0;

    while (accumulator.consumedSteps() < ticks && frames < (ticks * 4U + 16U)) {
        const auto remainingTicks = static_cast<double>(ticks - accumulator.consumedSteps());
        const double maxMultiplier = std::max(0.0, remainingTicks - accumulator.alpha());
        accumulator.advance(dt * std::min(framePattern[frames % framePattern.size()], maxMultiplier));

        std::uint32_t substeps = 0;
        while (accumulator.shouldStep() && accumulator.consumedSteps() < ticks) {
            accumulator.consumeStep();
            ++substeps;
        }

        ++frames;
        maxSubsteps = std::max(maxSubsteps, substeps);
        const auto consumed = accumulator.consumedSteps();
        if (consumed > 0U && consumed != lastCheckpoint && (consumed % checkpoint) == 0U && options.includeTelemetry) {
            scenario.telemetry.push_back(event(static_cast<std::uint32_t>(consumed), "clock", "fixed-step checkpoint consumed"));
            lastCheckpoint = consumed;
        }
    }

    scenario.simulatedTicks = static_cast<std::uint32_t>(accumulator.consumedSteps());
    scenario.passed = scenario.simulatedTicks == ticks && accumulator.alpha() < 0.0001 && maxSubsteps <= 2U;
    scenario.metrics.push_back(metric("frames", static_cast<double>(frames), "frames"));
    scenario.metrics.push_back(metric("consumed_steps", static_cast<double>(accumulator.consumedSteps()), "ticks"));
    scenario.metrics.push_back(metric("fixed_delta_ms", dt * 1000.0, "ms"));
    scenario.metrics.push_back(metric("max_substeps_per_frame", static_cast<double>(maxSubsteps), "ticks"));
    scenario.metrics.push_back(metric("residual_alpha", accumulator.alpha(), "ratio"));

    std::ostringstream summary;
    summary << scenario.id
            << " ticks=" << scenario.simulatedTicks
            << " frames=" << frames
            << " max_substeps=" << maxSubsteps
            << " status=" << passText(scenario.passed);
    scenario.summary = summary.str();
    return scenario;
}

[[nodiscard]] EngineSandboxScenarioResult runEcsLifecycleSmokeScenario(const EngineSandboxOptions& options) {
    EngineSandboxScenarioResult scenario{};
    scenario.id = toString(kEcsLifecycleScenario);
    scenario.name = "ECS lifecycle smoke";

    ecs::World world;
    const auto ticks = std::clamp(options.tickCount, 1U, 7200U);
    const std::uint32_t entityCount = std::clamp(4U + (ticks / 24U), 4U, 96U);
    std::vector<ecs::EntityId> entities;
    entities.reserve(entityCount);
    for (std::uint32_t index = 0; index < entityCount; ++index) {
        const auto entity = world.createEntity();
        entities.push_back(entity);
        world.addComponent(entity, ecs::NameComponent{"sandbox_entity_" + std::to_string(index)});
        world.addComponent(entity, ecs::TransformComponent{
            math::Vec3{static_cast<float>(index) * 0.25F, 0.0F, static_cast<float>(index) * -0.10F},
            {},
            {1.0F, 1.0F, 1.0F},
        });
    }

    std::uint32_t destroyedCount = 0;
    const std::uint32_t destroyTick = std::max(1U, ticks / 2U);
    bool componentsReadable = true;
    for (std::uint32_t tick = 0; tick < ticks; ++tick) {
        if (tick == destroyTick) {
            for (std::size_t index = 0; index < entities.size(); ++index) {
                if ((index % 5U) == 0U && world.isAlive(entities[index])) {
                    world.destroyEntity(entities[index]);
                    ++destroyedCount;
                }
            }
            if (options.includeTelemetry) {
                scenario.telemetry.push_back(event(tick, "ecs", "destroyed scheduled transient entities"));
            }
        }

        for (std::size_t index = 0; index < entities.size(); ++index) {
            if (!world.isAlive(entities[index])) {
                continue;
            }
            auto* transform = world.getComponent<ecs::TransformComponent>(entities[index]);
            const auto* name = world.getComponent<ecs::NameComponent>(entities[index]);
            componentsReadable = componentsReadable && transform != nullptr && name != nullptr && !name->name.empty();
            if (transform != nullptr) {
                transform->position.x += 0.005F * static_cast<float>((index % 3U) + 1U);
                transform->position.z += 0.002F * static_cast<float>((tick % 5U) + 1U);
            }
        }
    }

    double checksum = 0.0;
    bool staleIdsRejected = true;
    for (const auto entity : entities) {
        if (!world.isAlive(entity)) {
            staleIdsRejected = staleIdsRejected &&
                world.getComponent<ecs::NameComponent>(entity) == nullptr &&
                world.getComponent<ecs::TransformComponent>(entity) == nullptr;
            continue;
        }
        const auto* transform = world.getComponent<ecs::TransformComponent>(entity);
        if (transform != nullptr) {
            checksum += transform->position.x + transform->position.y + transform->position.z;
        }
    }

    const auto expectedAlive = static_cast<std::size_t>(entityCount - destroyedCount);
    scenario.simulatedTicks = ticks;
    scenario.passed = world.aliveCount() == expectedAlive && componentsReadable && staleIdsRejected && std::isfinite(checksum);
    scenario.metrics.push_back(metric("entities_created", static_cast<double>(entityCount), "entities"));
    scenario.metrics.push_back(metric("entities_destroyed", static_cast<double>(destroyedCount), "entities"));
    scenario.metrics.push_back(metric("entities_alive", static_cast<double>(world.aliveCount()), "entities"));
    scenario.metrics.push_back(metric("transform_checksum", checksum, "units"));

    std::ostringstream summary;
    summary << scenario.id
            << " ticks=" << scenario.simulatedTicks
            << " alive=" << world.aliveCount()
            << " destroyed=" << destroyedCount
            << " status=" << passText(scenario.passed);
    scenario.summary = summary.str();
    return scenario;
}

[[nodiscard]] EngineSandboxScenarioResult runNetLoopbackSmokeScenario(const EngineSandboxOptions& options) {
    EngineSandboxScenarioResult scenario{};
    scenario.id = toString(kNetLoopbackScenario);
    scenario.name = "Loopback packet smoke";

    net::LoopbackChannel loopback;
    const auto ticks = std::clamp(options.tickCount, 1U, 7200U);
    std::uint32_t serverPackets = 0;
    std::uint32_t clientPackets = 0;
    std::uint32_t rejectedOverreads = 0;
    std::size_t payloadBytes = 0;
    bool packetsValid = true;

    for (std::uint32_t tick = 0; tick < ticks; ++tick) {
        net::PacketWriter writer;
        writer.writeU32(tick);
        writer.writeU16(static_cast<std::uint16_t>(tick % 65535U));
        writer.writeFloat(static_cast<float>(tick) * 0.25F);
        writer.writeBytes("nc");

        net::Packet packet{};
        packet.sequence = net::PacketSequence{tick};
        packet.payload = writer.finish();
        payloadBytes += packet.payload.size();
        loopback.sendToServer(std::move(packet));

        net::Packet received{};
        packetsValid = packetsValid && loopback.tryReceiveForServer(received);
        if (!packetsValid) {
            continue;
        }
        ++serverPackets;

        std::uint32_t readTick = 0;
        std::uint16_t readChannel = 0;
        float readMarker = 0.0F;
        std::uint8_t extra = 0;
        net::PacketReader reader(received.payload);
        const auto suffix = [&]() {
            return reader.readU32(readTick) &&
                reader.readU16(readChannel) &&
                reader.readFloat(readMarker)
                    ? reader.readBytes(2)
                    : std::optional<std::string_view>{};
        }();
        packetsValid = packetsValid &&
            suffix.has_value() &&
            *suffix == "nc" &&
            reader.consumed() &&
            readTick == tick &&
            readChannel == static_cast<std::uint16_t>(tick % 65535U) &&
            std::abs(readMarker - (static_cast<float>(tick) * 0.25F)) < 0.001F;
        if (!reader.readU8(extra)) {
            ++rejectedOverreads;
        }

        net::Packet echo{};
        echo.sequence = received.sequence.next();
        echo.payload = received.payload;
        loopback.sendToClient(std::move(echo));
        net::Packet echoed{};
        packetsValid = packetsValid && loopback.tryReceiveForClient(echoed) && echoed.sequence == net::PacketSequence{tick + 1U};
        if (packetsValid) {
            ++clientPackets;
        }
    }

    scenario.simulatedTicks = ticks;
    scenario.passed = packetsValid &&
        serverPackets == ticks &&
        clientPackets == ticks &&
        rejectedOverreads == ticks;
    if (options.includeTelemetry) {
        scenario.telemetry.push_back(event(ticks, "net", "loopback packet stream acknowledged"));
    }
    scenario.metrics.push_back(metric("server_packets", static_cast<double>(serverPackets), "packets"));
    scenario.metrics.push_back(metric("client_packets", static_cast<double>(clientPackets), "packets"));
    scenario.metrics.push_back(metric("payload_bytes", static_cast<double>(payloadBytes), "bytes"));
    scenario.metrics.push_back(metric("rejected_overreads", static_cast<double>(rejectedOverreads), "reads"));

    std::ostringstream summary;
    summary << scenario.id
            << " ticks=" << scenario.simulatedTicks
            << " packets=" << serverPackets
            << " bytes=" << payloadBytes
            << " status=" << passText(scenario.passed);
    scenario.summary = summary.str();
    return scenario;
}

[[nodiscard]] EngineSandboxScenarioResult runAssetManifestSmokeScenario(const EngineSandboxOptions& options) {
    EngineSandboxScenarioResult scenario{};
    scenario.id = toString(kAssetManifestScenario);
    scenario.name = "Asset manifest parse smoke";

    constexpr std::string_view manifestJson = R"({
        "manifest": { "name": "sandbox_assets", "root": "assets" },
        "assets": [
            {
                "id": "mesh_debug_block",
                "kind": "mesh",
                "source": "source/mesh_debug_block.glb",
                "cooked": "cooked/mesh_debug_block.glb",
                "streamable": true,
                "priority": 10,
                "estimated_bytes": 2048,
                "dependencies": ["mat_debug_grid"],
                "tags": ["sandbox", "geometry"]
            },
            {
                "id": "mat_debug_grid",
                "kind": "material",
                "source": "source/mat_debug_grid.json",
                "cooked": "cooked/mat_debug_grid.bin",
                "estimated_bytes": 512
            },
            {
                "id": "data_spawn_probe",
                "kind": "data",
                "source": "source/data_spawn_probe.json",
                "cooked": "cooked/data_spawn_probe.bin",
                "estimated_bytes": 128
            }
        ]
    })";

    core::ConfigDocument document;
    const auto configResult = core::parseJsonConfig(manifestJson, document);
    assets::AssetManifest manifest;
    const auto manifestResult = configResult.ok()
        ? assets::parseAssetManifest(document, manifest)
        : assets::AssetManifestLoadResult{{"config parse failed"}};

    std::size_t dependencyCount = 0;
    std::size_t streamableCount = 0;
    std::uint64_t estimatedBytes = 0;
    if (manifestResult.ok()) {
        for (const auto& record : manifest.records()) {
            dependencyCount += record.dependencies.size();
            streamableCount += record.streamable ? 1U : 0U;
            estimatedBytes += record.estimatedBytes;
        }
    }

    scenario.simulatedTicks = 0;
    scenario.passed = configResult.ok() &&
        manifestResult.ok() &&
        manifest.recordCount() == 3U &&
        manifest.contains("mesh_debug_block") &&
        manifest.contains("mat_debug_grid") &&
        dependencyCount == 1U &&
        streamableCount == 1U &&
        estimatedBytes == 2688U;
    if (options.includeTelemetry) {
        scenario.telemetry.push_back(event(0U, "assets", "parsed in-memory sandbox manifest"));
    }
    scenario.metrics.push_back(metric("config_values", static_cast<double>(document.valueCount()), "values"));
    scenario.metrics.push_back(metric("manifest_records", static_cast<double>(manifest.recordCount()), "assets"));
    scenario.metrics.push_back(metric("dependencies", static_cast<double>(dependencyCount), "assets"));
    scenario.metrics.push_back(metric("streamable_assets", static_cast<double>(streamableCount), "assets"));
    scenario.metrics.push_back(metric("estimated_bytes", static_cast<double>(estimatedBytes), "bytes"));

    std::ostringstream summary;
    summary << scenario.id
            << " records=" << manifest.recordCount()
            << " dependencies=" << dependencyCount
            << " status=" << passText(scenario.passed);
    scenario.summary = summary.str();
    return scenario;
}

[[nodiscard]] std::string buildSandboxSummary(const EngineSandboxRunResult& result) {
    std::ostringstream stream;
    stream << "NovaCore Engine Sandbox"
           << " scenarios=" << result.scenarios.size()
           << " passed=" << result.passedCount
           << " failed=" << result.failedCount
           << " ticks=" << result.options.tickCount
           << " exit=" << exitValue(result.exitCode);
    return stream.str();
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

    if (collider.velocity.lengthSquared() > 0.000001F) {
        const auto start = collider.center + novacore::math::Vec3{0.0F, collider.halfExtents.y + 0.12F, 0.0F};
        frame.worldLines.push_back(novacore::render::RenderLine3D{
            start,
            start + (collider.velocity * 0.55F),
            {0.30F, 0.84F, 1.0F, 0.95F},
        });
    }
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
           << " kinematic=" << result.physicsStats.kinematicColliderCount
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

void appendScenario(EngineSandboxRunResult& result, EngineSandboxScenarioResult scenario) {
    if (scenario.passed) {
        ++result.passedCount;
    } else {
        ++result.failedCount;
    }
    result.scenarios.push_back(std::move(scenario));
}

[[nodiscard]] EngineSandboxScenarioResult runMovingSupportScenario(const EngineSandboxOptions& options) {
    physics::PhysicsWorld world;
    world.setBounds({8.0F, 4.0F, 8.0F});
    world.addStaticCollider(physics::StaticCollider{
        "support_platform",
        physics::SurfaceKind::Cover,
        {0.0F, 0.16F, 0.0F},
        {1.25F, 0.16F, 1.25F},
        true,
        physics::RampDirection::None,
        0.32F,
        {1.20F, 0.0F, 0.0F},
    });

    physics::CharacterMotorConfig config{};
    physics::CharacterMotorState state{};
    state.position = {0.0F, 0.32F, 0.0F};
    state.capsuleHeight = config.standingHeight;
    const float dt = std::clamp(options.fixedDeltaSeconds, 1.0F / 240.0F, 1.0F / 20.0F);
    const std::uint32_t carryTicks = std::min(std::max(options.tickCount / 4U, 8U), 36U);

    std::uint32_t supportedTicks = 0;
    std::uint32_t manifoldTicks = 0;
    std::uint32_t movedColliderSteps = 0;
    float firstSupportVelocityX = 0.0F;
    std::uint64_t telemetryHash = 0;
    for (std::uint32_t tick = 0; tick < carryTicks; ++tick) {
        physics::CharacterMotorInput input{};
        input.forward = {0.0F, 0.0F, 1.0F};
        const auto step = physics::stepCharacterMotor(world, state, input, config, dt);
        if (step.carriedBySupport && step.supportColliderId == "support_platform") {
            ++supportedTicks;
            if (firstSupportVelocityX == 0.0F) {
                firstSupportVelocityX = step.supportVelocity.x;
            }
        }
        manifoldTicks += step.telemetry.contactCount > 0U ? 1U : 0U;
        telemetryHash ^= step.telemetry.stateHash + (step.telemetry.contactHash * 0x9E3779B185EBCA87ULL);
        state = step.state;
        movedColliderSteps += static_cast<std::uint32_t>(world.advanceKinematicColliders(dt));
    }

    physics::CharacterMotorInput jumpInput{};
    jumpInput.forward = {0.0F, 0.0F, 1.0F};
    jumpInput.jumpPressed = true;
    const auto jumpStep = physics::stepCharacterMotor(world, state, jumpInput, config, dt);
    const auto* finalSupport = world.findStaticCollider("support_platform");
    const float platformDistanceX = finalSupport != nullptr ? finalSupport->center.x : 0.0F;

    EngineSandboxScenarioResult scenario{};
    scenario.id = toString(kMovingSupportScenario);
    scenario.name = "Moving support carry";
    scenario.simulatedTicks = carryTicks + 1U;
    scenario.passed = supportedTicks >= (carryTicks / 2U) &&
        firstSupportVelocityX > 1.0F &&
        state.position.x > 0.05F &&
        platformDistanceX > 0.05F &&
        std::abs(state.position.x - platformDistanceX) < 0.08F &&
        manifoldTicks >= (carryTicks / 2U) &&
        movedColliderSteps == carryTicks &&
        telemetryHash != 0U &&
        jumpStep.jumped &&
        jumpStep.state.velocity.x > 1.0F &&
        finite(jumpStep.state.position) &&
        finite(jumpStep.state.velocity);
    scenario.metrics.push_back(metric("supported_ticks", supportedTicks, "ticks"));
    scenario.metrics.push_back(metric("support_velocity_x", firstSupportVelocityX, "mps"));
    scenario.metrics.push_back(metric("carried_distance_x", state.position.x, "meters"));
    scenario.metrics.push_back(metric("platform_distance_x", platformDistanceX, "meters"));
    scenario.metrics.push_back(metric("manifold_ticks", manifoldTicks, "ticks"));
    scenario.metrics.push_back(metric("moved_collider_steps", movedColliderSteps, "steps"));
    scenario.metrics.push_back(metric("jump_velocity_x", jumpStep.state.velocity.x, "mps"));
    scenario.metrics.push_back(metric("jump_velocity_y", jumpStep.state.velocity.y, "mps"));
    if (options.includeTelemetry) {
        scenario.telemetry.push_back(event(0U, "support", "character resolved onto moving support"));
        scenario.telemetry.push_back(event(carryTicks, "jump", jumpStep.jumped ? "jump inherited support velocity" : "jump did not fire"));
    }

    std::ostringstream summary;
    summary << "supported=" << supportedTicks << "/" << carryTicks
            << " carried_x=" << std::fixed << std::setprecision(2) << state.position.x
            << " jump_vx=" << jumpStep.state.velocity.x;
    scenario.summary = summary.str();
    return scenario;
}

[[nodiscard]] EngineSandboxScenarioResult runAnimationBlendScenario(const EngineSandboxOptions& options) {
    EngineSandboxScenarioResult scenario{};
    scenario.id = toString(kAnimationBlendScenario);
    scenario.name = "Animation blending and socket evaluation";
    animation::Skeleton skeleton{};
    skeleton.name = "sandbox_operator";
    skeleton.joints = {
        animation::Joint{"root", -1, {}},
        animation::Joint{"spine", 0, {{0.0F, 0.9F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}},
        animation::Joint{"hand_r", 1, {{0.32F, 0.42F, 0.10F}, {}, {1.0F, 1.0F, 1.0F}}},
    };
    skeleton.sockets.push_back(animation::Socket{
        "socket_weapon_root",
        2U,
        {{0.0F, 0.0F, 0.18F}, {}, {1.0F, 1.0F, 1.0F}},
    });

    animation::AnimationClip idle{};
    idle.name = "idle";
    idle.duration = 1.0F;
    animation::JointTrack idleSpine{1U};
    idleSpine.rotation = animation::QuatTrack{
            animation::InterpolationMode::Linear,
            {
                {0.0F, {0.0F, 0.0F, 0.0F, 1.0F}},
                {0.5F, {0.0F, 0.0F, 0.025F, 0.9996875F}},
                {1.0F, {0.0F, 0.0F, 0.0F, 1.0F}},
            },
        };
    idle.tracks.push_back(std::move(idleSpine));

    animation::AnimationClip run{};
    run.name = "run";
    run.duration = 1.0F;
    animation::JointTrack runRoot{0U};
    runRoot.translation = animation::Vec3Track{
            animation::InterpolationMode::Linear,
            {{0.0F, {}}, {1.0F, {0.0F, 0.0F, 1.25F}}},
        };
    run.tracks.push_back(std::move(runRoot));
    animation::JointTrack runHand{2U};
    runHand.translation = animation::Vec3Track{
            animation::InterpolationMode::Linear,
            {
                {0.0F, {0.32F, 0.42F, 0.10F}},
                {0.5F, {0.32F, 0.36F, 0.24F}},
                {1.0F, {0.32F, 0.42F, 0.10F}},
            },
        };
    run.tracks.push_back(std::move(runHand));

    animation::AnimationRuntime runtime;
    const auto skeletonValidation = runtime.setSkeleton(skeleton);
    const auto idleValidation = runtime.play(idle, animation::WrapMode::Loop);
    const auto rootMotionValidation = runtime.setRootMotionJoint(0U);
    const std::uint32_t ticks = std::clamp(options.tickCount, 30U, 720U);
    const float dt = std::clamp(options.fixedDeltaSeconds, 1.0F / 240.0F, 1.0F / 20.0F);
    bool runtimeStable = skeletonValidation.valid() && idleValidation.valid() && rootMotionValidation.valid();
    float rootMotionDistance = 0.0F;
    std::uint32_t transitionTicks = 0U;
    for (std::uint32_t tick = 0; tick < ticks && runtimeStable; ++tick) {
        if (tick == ticks / 3U) {
            const auto transition = runtime.crossFade(run, 0.18F, animation::WrapMode::Loop);
            runtimeStable = transition.valid();
            if (options.includeTelemetry) {
                scenario.telemetry.push_back(event(tick, "animation", "crossfade idle to run"));
            }
        }
        runtimeStable = runtimeStable && runtime.update(dt);
        rootMotionDistance += runtime.rootMotionDelta().translation.z;
        transitionTicks += runtime.transitioning() ? 1U : 0U;
    }

    animation::Mat4 weaponSocket{};
    const bool socketReady = runtime.socketTransform("socket_weapon_root", weaponSocket);
    const auto socketPosition = animation::matrixTranslation(weaponSocket);

    scenario.simulatedTicks = ticks;
    scenario.passed = runtimeStable && socketReady && finite(socketPosition) &&
        rootMotionDistance > 0.15F && transitionTicks > 0U && !runtime.transitioning();
    scenario.metrics.push_back(metric("joints", static_cast<double>(skeleton.joints.size()), "joints"));
    scenario.metrics.push_back(metric("clips", 2.0, "clips"));
    scenario.metrics.push_back(metric("transition_ticks", transitionTicks, "ticks"));
    scenario.metrics.push_back(metric("root_motion_z", rootMotionDistance, "meters"));
    scenario.metrics.push_back(metric("weapon_socket_y", socketPosition.y, "meters"));
    if (options.includeTelemetry) {
        scenario.telemetry.push_back(event(ticks, "animation", scenario.passed
            ? "animation runtime and socket remained stable"
            : "animation runtime validation failed"));
    }
    std::ostringstream summary;
    summary << scenario.id
            << " ticks=" << ticks
            << " root_motion=" << std::fixed << std::setprecision(2) << rootMotionDistance
            << " socket_y=" << socketPosition.y
            << " status=" << passText(scenario.passed);
    scenario.summary = summary.str();
    return scenario;
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
    result.options = options;
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
        result.physicsStats.blockingColliderCount >= 5U &&
        result.physicsStats.kinematicColliderCount >= 1U;

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

    if (wantsScenario(options, kFixedStepScenario)) {
        appendScenario(result, runFixedStepClockScenario(options));
    }

    if (!(options.failFast && result.failedCount > 0U) && wantsScenario(options, kEcsLifecycleScenario)) {
        appendScenario(result, runEcsLifecycleSmokeScenario(options));
    }

    if (!(options.failFast && result.failedCount > 0U) && wantsScenario(options, kNetLoopbackScenario)) {
        appendScenario(result, runNetLoopbackSmokeScenario(options));
    }

    if (!(options.failFast && result.failedCount > 0U) && wantsScenario(options, kAssetManifestScenario)) {
        appendScenario(result, runAssetManifestSmokeScenario(options));
    }

    if (wantsScenario(options, kMovementReplayScenario)) {
        EngineSandboxScenarioResult movement{};
        movement.id = toString(kMovementReplayScenario);
        movement.name = "Movement replay fixture";
        movement.simulatedTicks = replay.simulatedTicks;
        movement.passed = result.stable;
        movement.metrics.push_back(metric("grounded_ticks", replay.groundedTicks, "ticks"));
        movement.metrics.push_back(metric("swept_ticks", replay.sweptTicks, "ticks"));
        movement.metrics.push_back(metric("wall_probe_ticks", replay.wallProbeTicks, "ticks"));
        movement.metrics.push_back(metric("landed_ticks", replay.landedTicks, "ticks"));
        movement.metrics.push_back(metric("stepped_ticks", replay.steppedTicks, "ticks"));
        movement.metrics.push_back(metric("ground_snap_ticks", replay.groundSnapTicks, "ticks"));
        movement.metrics.push_back(metric("support_ticks", replay.supportTicks, "ticks"));
        movement.metrics.push_back(metric("max_contacts", replay.maximumContactCount, "contacts"));
        movement.metrics.push_back(metric("max_impact_speed", replay.maximumImpactSpeed, "mps"));
        movement.metrics.push_back(metric("static_colliders", result.physicsStats.staticColliderCount, "colliders"));
        movement.metrics.push_back(metric("kinematic_colliders", result.physicsStats.kinematicColliderCount, "colliders"));
        movement.metrics.push_back(metric("final_speed", std::sqrt(result.finalCharacter.velocity.lengthSquared()), "mps"));
        if (options.includeTelemetry) {
            movement.telemetry.push_back(event(0U, "spawn", "movement replay character spawned"));
            movement.telemetry.push_back(event(
                replay.simulatedTicks,
                "finish",
                movement.passed ? "stable replay finished" : "replay failed stability checks"));
        }
        std::ostringstream movementSummary;
        movementSummary << "ticks=" << replay.simulatedTicks
                        << " grounded=" << replay.groundedTicks
                        << " swept=" << replay.sweptTicks
                        << " final_z=" << std::fixed << std::setprecision(2) << result.finalCharacter.position.z;
        movement.summary = movementSummary.str();
        appendScenario(result, std::move(movement));
    }

    if (!(options.failFast && result.failedCount > 0U) && wantsScenario(options, kMovingSupportScenario)) {
        appendScenario(result, runMovingSupportScenario(options));
    }

    if (!(options.failFast && result.failedCount > 0U) && wantsScenario(options, kAnimationBlendScenario)) {
        appendScenario(result, runAnimationBlendScenario(options));
    }

    if (result.scenarios.empty()) {
        result.stable = false;
        result.exitCode = EngineSandboxExitCode::NoScenarioSelected;
        result.summary = "NovaCore Engine Sandbox no scenarios selected";
        return result;
    }

    result.stable = result.failedCount == 0U;
    result.exitCode = result.stable ? EngineSandboxExitCode::Success : EngineSandboxExitCode::ScenarioFailure;
    result.summary = buildSandboxSummary(result);
    return result;
}

std::vector<std::string_view> availableEngineSandboxScenarioIds() {
    return {
        kFixedStepScenario,
        kEcsLifecycleScenario,
        kNetLoopbackScenario,
        kAssetManifestScenario,
        kMovementReplayScenario,
        kMovingSupportScenario,
        kAnimationBlendScenario,
    };
}

bool isEngineSandboxScenarioAvailable(std::string_view id) {
    const auto scenarios = availableEngineSandboxScenarioIds();
    return std::find(scenarios.begin(), scenarios.end(), id) != scenarios.end();
}

int engineSandboxExitCodeValue(EngineSandboxExitCode code) {
    return static_cast<int>(code);
}

std::string formatEngineSandboxText(const EngineSandboxRunResult& result) {
    std::ostringstream stream;
    stream << result.summary << '\n';
    for (const auto& scenario : result.scenarios) {
        stream << '[' << (scenario.passed ? "pass" : "fail") << "] "
               << scenario.summary << '\n';
        for (const auto& value : scenario.metrics) {
            stream << "  metric " << value.name << '=' << formatMetricValue(value.value);
            if (!value.unit.empty()) {
                stream << ' ' << value.unit;
            }
            stream << '\n';
        }
        for (const auto& telemetry : scenario.telemetry) {
            stream << "  event tick=" << telemetry.tick
                   << " channel=" << telemetry.channel
                   << " message=\"" << telemetry.message << "\"\n";
        }
    }
    return stream.str();
}

std::string formatEngineSandboxJson(const EngineSandboxRunResult& result) {
    std::ostringstream stream;
    stream << "{"
           << "\"stable\":" << (result.stable ? "true" : "false") << ","
           << "\"exitCode\":" << engineSandboxExitCodeValue(result.exitCode) << ","
           << "\"summary\":\"" << escapeJson(result.summary) << "\","
           << "\"passedCount\":" << result.passedCount << ","
           << "\"failedCount\":" << result.failedCount << ","
           << "\"scenarios\":[";

    for (std::size_t index = 0; index < result.scenarios.size(); ++index) {
        const auto& scenario = result.scenarios[index];
        if (index > 0U) {
            stream << ",";
        }
        stream << "{"
               << "\"id\":\"" << escapeJson(scenario.id) << "\","
               << "\"name\":\"" << escapeJson(scenario.name) << "\","
               << "\"passed\":" << (scenario.passed ? "true" : "false") << ","
               << "\"simulatedTicks\":" << scenario.simulatedTicks << ","
               << "\"summary\":\"" << escapeJson(scenario.summary) << "\","
               << "\"metrics\":[";
        for (std::size_t metricIndex = 0; metricIndex < scenario.metrics.size(); ++metricIndex) {
            const auto& value = scenario.metrics[metricIndex];
            if (metricIndex > 0U) {
                stream << ",";
            }
            stream << "{"
                   << "\"name\":\"" << escapeJson(value.name) << "\","
                   << "\"value\":" << std::setprecision(8) << value.value << ","
                   << "\"unit\":\"" << escapeJson(value.unit) << "\""
                   << "}";
        }
        stream << "],\"telemetry\":[";
        for (std::size_t eventIndex = 0; eventIndex < scenario.telemetry.size(); ++eventIndex) {
            const auto& telemetry = scenario.telemetry[eventIndex];
            if (eventIndex > 0U) {
                stream << ",";
            }
            stream << "{"
                   << "\"tick\":" << telemetry.tick << ","
                   << "\"channel\":\"" << escapeJson(telemetry.channel) << "\","
                   << "\"message\":\"" << escapeJson(telemetry.message) << "\""
                   << "}";
        }
        stream << "]}";
    }

    stream << "]}";
    return stream.str();
}

} // namespace novacore::sandbox
