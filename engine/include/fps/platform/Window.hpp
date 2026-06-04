#pragma once

#include "fps/platform/Input.hpp"

#include <cstdint>
#include <string>

namespace riftline::platform {

struct WindowDesc final {
    std::string title = "Project Riftline";
    std::int32_t width = 1280;
    std::int32_t height = 720;
    bool preferVulkan = true;
};

class Window final {
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool create(const WindowDesc& desc);
    void pollEvents(InputSystem& input);
    void shutdown();

    [[nodiscard]] bool shouldClose() const;
    [[nodiscard]] bool isHeadless() const;
    [[nodiscard]] void* nativeHandle() const;
    [[nodiscard]] std::int32_t width() const;
    [[nodiscard]] std::int32_t height() const;

private:
    void* handle_ = nullptr;
    bool shouldClose_ = false;
    bool headless_ = true;
    std::int32_t width_ = 0;
    std::int32_t height_ = 0;
};

} // namespace riftline::platform

