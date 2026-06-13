#pragma once

#include "novacore/math/Types.hpp"
#include "novacore/platform/Input.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace novacore::platform {

enum class InputControlKind : std::uint8_t {
    KeyboardKey,
    MouseButton,
    MouseAxis,
    GamepadButton,
    GamepadAxis
};

struct InputControl final {
    InputControlKind kind = InputControlKind::KeyboardKey;
    std::uint16_t code = 0;

    [[nodiscard]] constexpr std::uint64_t key() const {
        return (static_cast<std::uint64_t>(kind) << 32U) | code;
    }
};

struct InputBinding final {
    std::string action;
    InputControl control{};
    float scale = 1.0F;
    float activationThreshold = 0.5F;
};

struct InputActionState final {
    float value = 0.0F;
    bool down = false;
    bool pressed = false;
    bool released = false;
    InputDeviceKind device = InputDeviceKind::KeyboardMouse;
};

class InputSnapshot final {
public:
    void beginFrame();
    void clear();
    void setButton(InputControl control, bool down, InputDeviceKind device);
    void setAxis(InputControl control, float value, InputDeviceKind device);
    void addAxisDelta(InputControl control, float delta, InputDeviceKind device);
    void setPointerPosition(math::Vec2 position, InputDeviceKind device);

    [[nodiscard]] bool isDown(InputControl control) const;
    [[nodiscard]] bool wasPressed(InputControl control) const;
    [[nodiscard]] bool wasReleased(InputControl control) const;
    [[nodiscard]] float axisValue(InputControl control) const;
    [[nodiscard]] InputDeviceKind deviceFor(InputControl control) const;
    [[nodiscard]] bool hasPointerPosition() const;
    [[nodiscard]] math::Vec2 pointerPosition() const;

private:
    std::unordered_set<std::uint64_t> downControls_;
    std::unordered_set<std::uint64_t> pressedControls_;
    std::unordered_set<std::uint64_t> releasedControls_;
    std::unordered_map<std::uint64_t, float> axes_;
    std::unordered_map<std::uint64_t, InputDeviceKind> devices_;
    math::Vec2 pointerPosition_{};
    bool pointerPositionValid_ = false;
};

class InputActionMap final {
public:
    void bind(InputBinding binding);
    void clearBindings();
    void update(const InputSnapshot& snapshot);

    [[nodiscard]] const InputActionState* state(std::string_view action) const;
    [[nodiscard]] InputActionState stateOrDefault(std::string_view action) const;
    [[nodiscard]] std::size_t bindingCount() const;

private:
    std::unordered_map<std::string, std::vector<InputBinding>> bindings_;
    std::unordered_map<std::string, InputActionState> states_;
};

} // namespace novacore::platform
