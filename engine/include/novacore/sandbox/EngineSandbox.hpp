#pragma once

#include "novacore/math/Types.hpp"
#include "novacore/physics/PhysicsSystem.hpp"
#include "novacore/render/Renderer.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace novacore::sandbox {

struct EngineSandboxOptions final {
    std::uint32_t tickCount = 180;
    float fixedDeltaSeconds = 1.0F / 60.0F;
    bool sprint = true;
    bool emitPreviewFrame = true;
};

struct EngineSandboxRunResult final {
    std::uint32_t simulatedTicks = 0;
    std::uint32_t groundedTicks = 0;
    std::uint32_t sweptTicks = 0;
    std::uint32_t wallProbeTicks = 0;
    physics::PhysicsWorldStats physicsStats{};
    physics::CharacterMotorState finalCharacter{};
    std::vector<math::Vec3> trajectory;
    render::RenderFrameInfo previewFrame{};
    std::string summary;
    bool stable = false;
};

[[nodiscard]] EngineSandboxRunResult runEngineSandbox(const EngineSandboxOptions& options = {});

} // namespace novacore::sandbox
