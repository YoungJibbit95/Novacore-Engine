#include "novacore/platform/InputAction.hpp"

#include <cmath>
#include <utility>

namespace novacore::platform {

void InputSnapshot::clear() {
    downControls_.clear();
    axes_.clear();
    devices_.clear();
}

void InputSnapshot::setButton(InputControl control, bool down, InputDeviceKind device) {
    const auto key = control.key();
    if (down) {
        downControls_.insert(key);
    } else {
        downControls_.erase(key);
    }
    devices_.insert_or_assign(key, device);
}

void InputSnapshot::setAxis(InputControl control, float value, InputDeviceKind device) {
    const auto key = control.key();
    axes_.insert_or_assign(key, value);
    devices_.insert_or_assign(key, device);
}

bool InputSnapshot::isDown(InputControl control) const {
    return downControls_.contains(control.key());
}

float InputSnapshot::axisValue(InputControl control) const {
    auto it = axes_.find(control.key());
    if (it == axes_.end()) {
        return 0.0F;
    }
    return it->second;
}

InputDeviceKind InputSnapshot::deviceFor(InputControl control) const {
    auto it = devices_.find(control.key());
    if (it == devices_.end()) {
        return InputDeviceKind::KeyboardMouse;
    }
    return it->second;
}

void InputActionMap::bind(InputBinding binding) {
    const auto action = binding.action;
    bindings_[action].push_back(std::move(binding));
    states_.try_emplace(action);
}

void InputActionMap::clearBindings() {
    bindings_.clear();
    states_.clear();
}

void InputActionMap::update(const InputSnapshot& snapshot) {
    for (auto& [action, bindings] : bindings_) {
        const auto previous = stateOrDefault(action);
        InputActionState next{};
        next.device = previous.device;

        float activationThreshold = 0.5F;
        for (const auto& binding : bindings) {
            float value = 0.0F;
            if (binding.control.kind == InputControlKind::GamepadAxis) {
                value = snapshot.axisValue(binding.control) * binding.scale;
            } else if (snapshot.isDown(binding.control)) {
                value = binding.scale;
            }

            if (std::abs(value) > std::abs(next.value)) {
                next.value = value;
                next.device = snapshot.deviceFor(binding.control);
                activationThreshold = binding.activationThreshold;
            }
        }

        next.down = std::abs(next.value) >= activationThreshold;
        next.pressed = next.down && !previous.down;
        next.released = !next.down && previous.down;
        states_.insert_or_assign(action, next);
    }
}

const InputActionState* InputActionMap::state(std::string_view action) const {
    auto it = states_.find(std::string(action));
    if (it == states_.end()) {
        return nullptr;
    }
    return &it->second;
}

InputActionState InputActionMap::stateOrDefault(std::string_view action) const {
    const auto* existing = state(action);
    if (existing == nullptr) {
        return {};
    }
    return *existing;
}

std::size_t InputActionMap::bindingCount() const {
    std::size_t count = 0;
    for (const auto& [_, bindings] : bindings_) {
        count += bindings.size();
    }
    return count;
}

} // namespace novacore::platform
