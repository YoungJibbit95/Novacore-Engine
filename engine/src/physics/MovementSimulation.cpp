#include "novacore/physics/MovementSimulation.hpp"

#include <algorithm>
#include <cmath>

namespace novacore::physics {

namespace {

[[nodiscard]] bool finite(math::Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] std::uint32_t sanitizedTickCount(std::uint32_t tickCount) {
    return std::clamp(tickCount, 1U, 7200U);
}

} // namespace

CharacterMotorReplayResult runCharacterMotorReplay(
    const PhysicsWorld& world,
    CharacterMotorState initialState,
    std::span<const CharacterMotorCommand> commands,
    const CharacterMotorReplayDesc& desc) {
    CharacterMotorReplayResult result{};
    result.initialState = initialState;
    result.finalState = initialState;

    const float fixedDeltaSeconds = std::clamp(
        desc.step.fixedDeltaSeconds,
        1.0F / 240.0F,
        1.0F / 15.0F);

    std::uint32_t totalRequestedTicks = 0;
    for (const auto& command : commands) {
        totalRequestedTicks += sanitizedTickCount(command.tickCount);
    }
    if (desc.keepFrames) {
        result.frames.reserve(totalRequestedTicks);
    }

    auto state = initialState;
    for (const auto& command : commands) {
        const auto ticks = sanitizedTickCount(command.tickCount);
        for (std::uint32_t localTick = 0; localTick < ticks; ++localTick) {
            auto input = command.input;
            input.jumpPressed = input.jumpPressed && localTick == 0U;
            const auto step = stepCharacterMotor(world, state, input, desc.motor, fixedDeltaSeconds);
            state = step.state;

            ++result.simulatedTicks;
            if (step.state.grounded) {
                ++result.groundedTicks;
            }
            if (step.swept) {
                ++result.sweptTicks;
            }
            if (step.jumped) {
                ++result.jumpTicks;
            }
            if (step.touchedSlideSurface) {
                ++result.slideSurfaceTicks;
            }
            if (step.state.nearWallRunSurface || step.touchedWallRunSurface) {
                ++result.wallProbeTicks;
            }

            if (desc.keepFrames) {
                result.frames.push_back(CharacterMotorReplayFrame{
                    step.state.tick,
                    input,
                    step,
                });
            }
        }
    }

    result.finalState = state;
    result.stable = finite(state.position) &&
        finite(state.velocity) &&
        result.simulatedTicks == totalRequestedTicks &&
        result.simulatedTicks > 0U;
    return result;
}

std::vector<CharacterMotorCommand> makeSandboxMovementScript(std::uint32_t tickCount) {
    const std::uint32_t totalTicks = std::clamp(tickCount, 1U, 720U);
    const std::uint32_t sprintTicks = std::max(12U, totalTicks / 3U);
    const std::uint32_t jumpTicks = std::max(12U, totalTicks / 8U);
    const std::uint32_t crouchTicks = std::max(12U, totalTicks / 5U);
    const std::uint32_t turnTicks = std::max(12U, totalTicks / 4U);
    const std::uint32_t consumed = std::min(totalTicks, sprintTicks + jumpTicks + crouchTicks + turnTicks);
    const std::uint32_t settleTicks = totalTicks - consumed;

    std::vector<CharacterMotorCommand> script;
    script.reserve(5U);
    CharacterMotorInput sprint{};
    sprint.forward = {0.0F, 0.0F, 1.0F};
    sprint.move = {0.0F, 0.0F, 1.0F};
    sprint.sprintHeld = true;
    script.push_back({sprint, sprintTicks, "sprint_forward"});

    CharacterMotorInput jump = sprint;
    jump.jumpPressed = true;
    script.push_back({jump, jumpTicks, "jump_arc"});

    CharacterMotorInput crouch{};
    crouch.forward = {0.0F, 0.0F, 1.0F};
    crouch.move = {0.0F, 0.0F, 1.0F};
    crouch.crouchHeld = true;
    script.push_back({crouch, crouchTicks, "crouch_slide_probe"});

    CharacterMotorInput turn{};
    turn.forward = {-1.0F, 0.0F, 0.35F};
    turn.move = {0.0F, 0.0F, 1.0F};
    turn.sprintHeld = true;
    script.push_back({turn, turnTicks, "wall_probe_turn"});

    if (settleTicks > 0U) {
        CharacterMotorInput settle{};
        settle.forward = {-1.0F, 0.0F, 0.35F};
        settle.move = {0.0F, 0.0F, 1.0F};
        script.push_back({settle, settleTicks, "settle"});
    }
    return script;
}

} // namespace novacore::physics
