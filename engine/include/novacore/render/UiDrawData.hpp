#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace novacore::render {

// Premultiplied RGBA8, immutable after submission. The backend retains sources
// until every frame using them has completed; callers never own Vulkan objects.
struct UiTexture final {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
    [[nodiscard]] bool valid() const {
        return width > 0 && height > 0 && width <= 4096 && height <= 4096 &&
            rgba.size() == static_cast<std::size_t>(width) * height * 4;
    }
};

struct UiVertex final {
    std::array<float, 2> position{};
    std::array<float, 2> texcoord{};
    std::array<std::uint8_t, 4> color{255, 255, 255, 255};
};

struct UiDrawCommand final {
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    // Pixel-space x, y, width, height. Applied after transforms.
    std::array<float, 4> clip{};
    std::shared_ptr<const UiTexture> texture;
};

struct UiDrawData final {
    float width = 0;
    float height = 0;
    std::vector<UiVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<UiDrawCommand> commands;
    [[nodiscard]] bool valid() const;
};

} // namespace novacore::render
