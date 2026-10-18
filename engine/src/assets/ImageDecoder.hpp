#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace novacore::assets::detail {

struct DecodedImage final {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Unpremultiplied RGBA8, glTF top-left row order, unchanged color values.
    // The GPU image format supplies sRGB decoding only for color textures.
    std::vector<std::uint8_t> pixels;
    std::string error;
    [[nodiscard]] bool ok() const { return error.empty() && !pixels.empty(); }
};

[[nodiscard]] DecodedImage decodeGltfImage(std::span<const std::uint8_t> encoded, std::string_view mimeType);

} // namespace novacore::assets::detail
