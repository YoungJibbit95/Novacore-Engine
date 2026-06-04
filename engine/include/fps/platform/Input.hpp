#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace novacore::platform {

enum class InputDeviceKind {
    KeyboardMouse,
    Controller
};

struct ControllerInfo final {
    std::int32_t id = -1;
    std::string name;
};

class InputSystem final {
public:
    void beginFrame();
    void noteKeyboardMouseActivity();
    void noteControllerConnected(std::int32_t id, std::string name);
    void noteControllerDisconnected(std::int32_t id);

    [[nodiscard]] bool hasKeyboardMouse() const;
    [[nodiscard]] bool hasController() const;
    [[nodiscard]] const std::vector<ControllerInfo>& controllers() const;
    [[nodiscard]] InputDeviceKind lastActiveDevice() const;

private:
    bool keyboardMouseAvailable_ = true;
    bool keyboardMouseActiveThisFrame_ = false;
    InputDeviceKind lastActiveDevice_ = InputDeviceKind::KeyboardMouse;
    std::vector<ControllerInfo> controllers_;
    std::unordered_set<std::int32_t> knownControllerIds_;
};

} // namespace novacore::platform







