#include "novacore/platform/Window.hpp"

#include "novacore/core/Log.hpp"

#if NOVACORE_HAS_SDL3
#include <SDL3/SDL.h>
#endif

namespace novacore::platform {

Window::~Window() {
    shutdown();
}

bool Window::create(const WindowDesc& desc) {
    width_ = desc.width;
    height_ = desc.height;

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
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_MOTION:
            input.noteKeyboardMouseActivity();
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            input.noteControllerConnected(static_cast<std::int32_t>(event.gdevice.which), "SDL Gamepad");
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            input.noteControllerDisconnected(static_cast<std::int32_t>(event.gdevice.which));
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

std::int32_t Window::width() const {
    return width_;
}

std::int32_t Window::height() const {
    return height_;
}

} // namespace novacore::platform







