#pragma once

#include "novacore/platform/InputAction.hpp"
#include "novacore/platform/Input.hpp"

#include <cstdint>
#include <string>

namespace novacore::platform {

struct WindowDesc final {
    std::string title = "NovaCore";
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
    void setTitle(const std::string& title);
    [[nodiscard]] bool setRelativeMouseMode(bool enabled);
    void shutdown();

    [[nodiscard]] bool shouldClose() const;
    [[nodiscard]] bool isHeadless() const;
    [[nodiscard]] bool relativeMouseMode() const;
    [[nodiscard]] void* nativeHandle() const;
    [[nodiscard]] const InputSnapshot& inputSnapshot() const;
    [[nodiscard]] std::int32_t width() const;
    [[nodiscard]] std::int32_t height() const;

private:
    void* handle_ = nullptr;
    bool shouldClose_ = false;
    bool headless_ = true;
    bool relativeMouseMode_ = false;
    std::int32_t width_ = 0;
    std::int32_t height_ = 0;
    InputSnapshot inputSnapshot_;
};

} // namespace novacore::platform







