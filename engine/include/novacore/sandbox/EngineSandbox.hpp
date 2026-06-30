#pragma once

#include "novacore/math/Types.hpp"
#include "novacore/physics/PhysicsSystem.hpp"
#include "novacore/render/Renderer.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace novacore::sandbox {

enum class EngineSandboxExitCode : int {
    Success = 0,
    ScenarioFailure = 1,
    InvalidArguments = 2,
    NoScenarioSelected = 3,
};

struct EngineSandboxOptions final {
    std::uint32_t tickCount = 180;
    float fixedDeltaSeconds = 1.0F / 60.0F;
    bool sprint = true;
    bool emitPreviewFrame = true;
    std::vector<std::string> scenarioIds;
    bool failFast = false;
    bool includeTelemetry = true;
};

struct EngineSandboxMetric final {
    std::string name;
    double value = 0.0;
    std::string unit;
};

struct EngineSandboxEvent final {
    std::uint32_t tick = 0;
    std::string channel;
    std::string message;
};

struct EngineSandboxScenarioResult final {
    std::string id;
    std::string name;
    std::uint32_t simulatedTicks = 0;
    std::vector<EngineSandboxMetric> metrics;
    std::vector<EngineSandboxEvent> telemetry;
    std::string summary;
    bool passed = false;
};

struct EngineSandboxRunResult final {
    EngineSandboxOptions options;
    std::uint32_t simulatedTicks = 0;
    std::uint32_t groundedTicks = 0;
    std::uint32_t sweptTicks = 0;
    std::uint32_t wallProbeTicks = 0;
    physics::PhysicsWorldStats physicsStats{};
    physics::CharacterMotorState finalCharacter{};
    std::vector<math::Vec3> trajectory;
    render::RenderFrameInfo previewFrame{};
    std::vector<EngineSandboxScenarioResult> scenarios;
    std::uint32_t passedCount = 0;
    std::uint32_t failedCount = 0;
    EngineSandboxExitCode exitCode = EngineSandboxExitCode::NoScenarioSelected;
    std::string summary;
    bool stable = false;
};

[[nodiscard]] std::vector<std::string_view> availableEngineSandboxScenarioIds();
[[nodiscard]] bool isEngineSandboxScenarioAvailable(std::string_view id);
[[nodiscard]] EngineSandboxRunResult runEngineSandbox(const EngineSandboxOptions& options = {});
[[nodiscard]] int engineSandboxExitCodeValue(EngineSandboxExitCode code);
[[nodiscard]] std::string formatEngineSandboxText(const EngineSandboxRunResult& result);
[[nodiscard]] std::string formatEngineSandboxJson(const EngineSandboxRunResult& result);

} // namespace novacore::sandbox
