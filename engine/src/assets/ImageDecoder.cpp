#include "ImageDecoder.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 8192
#include "stb_image.h"

#include <algorithm>
#include <memory>

namespace novacore::assets::detail {

DecodedImage decodeGltfImage(std::span<const std::uint8_t> encoded, std::string_view mimeType) {
    DecodedImage result{};
    constexpr std::size_t maximumEncodedBytes = 64U * 1024U * 1024U;
    constexpr std::size_t maximumPixels = 16U * 1024U * 1024U;
    constexpr std::uint8_t pngMagic[]{137, 80, 78, 71, 13, 10, 26, 10};
    const bool png =
        encoded.size() >= sizeof(pngMagic) && std::equal(std::begin(pngMagic), std::end(pngMagic), encoded.begin());
    const bool jpeg = encoded.size() >= 3 && encoded[0] == 0xff && encoded[1] == 0xd8 && encoded[2] == 0xff;
    if (encoded.empty() || encoded.size() > maximumEncodedBytes || (!png && !jpeg) ||
        (!mimeType.empty() && mimeType != (png ? "image/png" : "image/jpeg"))) {
        result.error = "image must be bounded PNG/JPEG data with a matching MIME type";
        return result;
    }
    int width = 0, height = 0, channels = 0;
    const int length = static_cast<int>(encoded.size());
    if (!stbi_info_from_memory(encoded.data(), length, &width, &height, &channels) || width <= 0 || height <= 0 ||
        width > STBI_MAX_DIMENSIONS || height > STBI_MAX_DIMENSIONS ||
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) > maximumPixels) {
        result.error = "image header is invalid or exceeds 8192 axes / 16 million pixels";
        return result;
    }
    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(encoded.data(), length, &width, &height, &channels, STBI_rgb_alpha), &stbi_image_free);
    if (!pixels) {
        result.error = "PNG/JPEG pixel decoding failed";
        return result;
    }
    result.width = static_cast<std::uint32_t>(width);
    result.height = static_cast<std::uint32_t>(height);
    result.pixels.assign(pixels.get(),
                         pixels.get() + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    return result;
}

} // namespace novacore::assets::detail
