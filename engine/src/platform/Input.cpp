#include "novacore/platform/Input.hpp"

#include <algorithm>
#include <utility>

namespace novacore::platform {

void InputSystem::beginFrame() {
    keyboardMouseActiveThisFrame_ = false;
}

void InputSystem::noteKeyboardMouseActivity() {
    keyboardMouseAvailable_ = true;
    keyboardMouseActiveThisFrame_ = true;
    lastActiveDevice_ = InputDeviceKind::KeyboardMouse;
}

void InputSystem::noteControllerConnected(std::int32_t id, std::string name) {
    if (knownControllerIds_.contains(id)) {
        return;
    }
    knownControllerIds_.insert(id);
    controllers_.push_back(ControllerInfo{id, std::move(name)});
}

void InputSystem::noteControllerDisconnected(std::int32_t id) {
    knownControllerIds_.erase(id);
    controllers_.erase(
        std::remove_if(controllers_.begin(), controllers_.end(), [id](const ControllerInfo& info) {
            return info.id == id;
        }),
        controllers_.end());
}

bool InputSystem::hasKeyboardMouse() const {
    return keyboardMouseAvailable_;
}

bool InputSystem::hasController() const {
    return !controllers_.empty();
}

const std::vector<ControllerInfo>& InputSystem::controllers() const {
    return controllers_;
}

InputDeviceKind InputSystem::lastActiveDevice() const {
    return lastActiveDevice_;
}

} // namespace novacore::platform







