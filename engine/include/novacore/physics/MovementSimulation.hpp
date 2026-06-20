#pragma once

#include "novacore/physics/PhysicsSystem.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace novacore::physics {

struct CharacterMotorCommand final {
    CharacterMotorInput input{};
    std::uint32_t tickCount = 1;
    std::string label;
};

struct CharacterMotorReplayFrame final {
    std::uint64_t tick = 0;
    CharacterMotorInput input{};
    CharacterMotorStepResult step{};
};

struct CharacterMotorReplayDesc final {
    CharacterMotorConfig motor{};
    PhysicsStepConfig step{};
    bool keepFrames = true;
};

struct CharacterMotorReplayResult final {
    CharacterMotorState initialState{};
    CharacterMotorState finalState{};
    std::vector<CharacterMotorReplayFrame> frames;
    std::uint32_t simulatedTicks = 0;
    std::uint32_t groundedTicks = 0;
    std::uint32_t sweptTicks = 0;
    std::uint32_t jumpTicks = 0;
    std::uint32_t slideSurfaceTicks = 0;
    std::uint32_t wallProbeTicks = 0;
    bool stable = false;
};

[[nodiscard]] CharacterMotorReplayResult runCharacterMotorReplay(
    const PhysicsWorld& world,
    CharacterMotorState initialState,
    std::span<const CharacterMotorCommand> commands,
    const CharacterMotorReplayDesc& desc = {});

[[nodiscard]] std::vector<CharacterMotorCommand> makeSandboxMovementScript(std::uint32_t tickCount);

} // namespace novacore::physics
