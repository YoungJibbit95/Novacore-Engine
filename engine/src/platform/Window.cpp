#include "novacore/platform/Window.hpp"

#include "novacore/core/Log.hpp"

#if NOVACORE_HAS_SDL3
#include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <cstdint>
#include <limits>

namespace novacore::platform {

#if NOVACORE_HAS_SDL3
namespace {

[[nodiscard]] std::uint16_t keyCodeFromSdl(SDL_Keycode key) {
    switch (key) {
    case SDLK_SPACE:
        return 32;
    case SDLK_LSHIFT:
        return 160;
    case SDLK_LALT:
        return 164;
    case SDLK_RSHIFT:
        return 161;
    case SDLK_RALT:
        return 165;
    default:
        break;
    }

    if (key >= SDLK_a && key <= SDLK_z) {
        return static_cast<std::uint16_t>('A' + (key - SDLK_a));
    }
    if (key >= 0 && key <= std::numeric_limits<std::uint16_t>::max()) {
        return static_cast<std::uint16_t>(key);
    }
    return 0;
}

[[nodiscard]] float normalizedGamepadAxis(Sint16 value) {
    const float divisor = value < 0 ? 32768.0F : 32767.0F;
    return std::clamp(static_cast<float>(value) / divisor, -1.0F, 1.0F);
}

} // namespace
#endif

Window::~Window() {
    shutdown();
}

bool Window::create(const WindowDesc& desc) {
    width_ = desc.width;
    height_ = desc.height;
    shouldClose_ = false;
    inputSnapshot_.clear();

#if NOVACORE_HAS_SDL3
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        core::logError("platform", SDL_GetError());
        headless_ = true;
        return false;
    }

    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
    if (desc.preferVulkan) {
        flags = static_cast<SDL_WindowFlags>(flags | SDL_WINDOW_VULKAN);
    }

    SDL_Window* window = SDL_CreateWindow(desc.title.c_str(), desc.width, desc.height, flags);
    if (window == nullptr) {
        core::logError("platform", SDL_GetError());
        headless_ = true;
        return false;
    }

    handle_ = window;
    headless_ = false;
    core::logInfo("platform", "SDL3 window created");
    return true;
#else
    (void)desc;
    headless_ = true;
    core::logWarning("platform", "SDL3 unavailable; using headless window fallback");
    return false;
#endif
}

void Window::pollEvents(InputSystem& input) {
    input.beginFrame();

#if NOVACORE_HAS_SDL3
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            shouldClose_ = true;
            break;
        case SDL_EVENT_KEY_DOWN:
            input.noteKeyboardMouseActivity();
            inputSnapshot_.setButton(
                InputControl{InputControlKind::KeyboardKey, keyCodeFromSdl(event.key.key)},
                true,
                InputDeviceKind::KeyboardMouse);
            break;
        case SDL_EVENT_KEY_UP:
            input.noteKeyboardMouseActivity();
            inputSnapshot_.setButton(
                InputControl{InputControlKind::KeyboardKey, keyCodeFromSdl(event.key.key)},
                false,
                InputDeviceKind::KeyboardMouse);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            input.noteKeyboardMouseActivity();
            inputSnapshot_.setButton(
                InputControl{InputControlKind::MouseButton, static_cast<std::uint16_t>(event.button.button)},
                true,
                InputDeviceKind::KeyboardMouse);
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            input.noteKeyboardMouseActivity();
            inputSnapshot_.setButton(
                InputControl{InputControlKind::MouseButton, static_cast<std::uint16_t>(event.button.button)},
                false,
                InputDeviceKind::KeyboardMouse);
            break;
        case SDL_EVENT_MOUSE_MOTION:
            input.noteKeyboardMouseActivity();
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            input.noteControllerConnected(static_cast<std::int32_t>(event.gdevice.which), "SDL Gamepad");
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            input.noteControllerDisconnected(static_cast<std::int32_t>(event.gdevice.which));
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            input.noteControllerActivity();
            inputSnapshot_.setButton(
                InputControl{InputControlKind::GamepadButton, static_cast<std::uint16_t>(event.gbutton.button)},
                true,
                InputDeviceKind::Controller);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            input.noteControllerActivity();
            inputSnapshot_.setButton(
                InputControl{InputControlKind::GamepadButton, static_cast<std::uint16_t>(event.gbutton.button)},
                false,
                InputDeviceKind::Controller);
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            input.noteControllerActivity();
            inputSnapshot_.setAxis(
                InputControl{InputControlKind::GamepadAxis, static_cast<std::uint16_t>(event.gaxis.axis)},
                normalizedGamepadAxis(event.gaxis.value),
                InputDeviceKind::Controller);
            break;
        default:
            break;
        }
    }
#else
    (void)input;
#endif
}

void Window::shutdown() {
#if NOVACORE_HAS_SDL3
    if (handle_ != nullptr) {
        SDL_DestroyWindow(static_cast<SDL_Window*>(handle_));
        handle_ = nullptr;
        SDL_Quit();
    }
#else
    handle_ = nullptr;
#endif
}

bool Window::shouldClose() const {
    return shouldClose_;
}

bool Window::isHeadless() const {
    return headless_;
}

void* Window::nativeHandle() const {
    return handle_;
}

const InputSnapshot& Window::inputSnapshot() const {
    return inputSnapshot_;
}

std::int32_t Window::width() const {
    return width_;
}

std::int32_t Window::height() const {
    return height_;
}

} // namespace novacore::platform







