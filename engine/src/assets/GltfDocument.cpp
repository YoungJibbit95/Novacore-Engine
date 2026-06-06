#include "novacore/assets/GltfDocument.hpp"

#include "novacore/io/FileSystem.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace novacore::assets {

namespace {

constexpr std::uint32_t kGlbMagic = 0x46546C67U;
constexpr std::uint32_t kGlbJsonChunk = 0x4E4F534AU;
constexpr std::uint32_t kGlbBinaryChunk = 0x004E4942U;

[[nodiscard]] std::string lowercaseExtension(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}

[[nodiscard]] std::uint32_t readU32(std::string_view bytes, std::size_t offset) {
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data() + offset);
    return static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8U) |
        (static_cast<std::uint32_t>(data[2]) << 16U) |
        (static_cast<std::uint32_t>(data[3]) << 24U);
}

[[nodiscard]] std::size_t countArrayEntries(
    const core::ConfigDocument& document,
    std::string_view prefix) {
    const std::string keyPrefix = std::string(prefix) + ".";
    std::unordered_set<std::size_t> indices;

    for (const auto& [key, _] : document.values()) {
        if (!key.starts_with(keyPrefix)) {
            continue;
        }

        std::size_t cursor = keyPrefix.size();
        if (cursor >= key.size() || !std::isdigit(static_cast<unsigned char>(key[cursor]))) {
            continue;
        }

        std::size_t index = 0;
        while (cursor < key.size() && std::isdigit(static_cast<unsigned char>(key[cursor]))) {
            index = (index * 10U) + static_cast<std::size_t>(key[cursor] - '0');
            ++cursor;
        }
        indices.insert(index);
    }

    return indices.size();
}

[[nodiscard]] GltfSceneInfoLoadResult fillSceneInfoFromJson(
    std::string_view json,
    GltfSceneInfo& info) {
    core::ConfigDocument document;
    const auto parseResult = core::parseJsonConfig(json, document);
    if (!parseResult.ok()) {
        GltfSceneInfoLoadResult result;
        for (const auto& error : parseResult.errors) {
            result.errors.push_back(error.message + " at offset " + std::to_string(error.offset));
        }
        return result;
    }

    const auto version = document.stringOr("asset.version", "");
    if (version.empty()) {
        return GltfSceneInfoLoadResult{{"glTF document is missing asset.version"}};
    }

    info.meshCount = countArrayEntries(document, "meshes");
    info.nodeCount = countArrayEntries(document, "nodes");
    info.materialCount = countArrayEntries(document, "materials");
    info.accessorCount = countArrayEntries(document, "accessors");
    info.bufferViewCount = countArrayEntries(document, "bufferViews");
    info.bufferCount = countArrayEntries(document, "buffers");
    info.imageCount = countArrayEntries(document, "images");
    info.animationCount = countArrayEntries(document, "animations");
    info.skinCount = countArrayEntries(document, "skins");

    if (info.meshCount == 0 && info.nodeCount == 0) {
        return GltfSceneInfoLoadResult{{"glTF document has no meshes or nodes"}};
    }

    return {};
}

[[nodiscard]] GltfSceneInfoLoadResult parseGlb(
    const std::filesystem::path& path,
    std::string_view bytes,
    GltfSceneInfo& outInfo) {
    if (bytes.size() < 20) {
        return GltfSceneInfoLoadResult{{"GLB file is too small: " + path.string()}};
    }

    const std::uint32_t magic = readU32(bytes, 0);
    const std::uint32_t version = readU32(bytes, 4);
    const std::uint32_t declaredLength = readU32(bytes, 8);
    if (magic != kGlbMagic) {
        return GltfSceneInfoLoadResult{{"GLB file has invalid magic: " + path.string()}};
    }
    if (version != 2) {
        return GltfSceneInfoLoadResult{{"Only GLB version 2 is supported: " + path.string()}};
    }
    if (declaredLength > bytes.size()) {
        return GltfSceneInfoLoadResult{{"GLB declared length exceeds file size: " + path.string()}};
    }

    std::size_t offset = 12;
    bool sawJson = false;
    std::string_view json;
    std::uint64_t binaryBytes = 0;

    while (offset + 8 <= declaredLength) {
        const std::uint32_t chunkLength = readU32(bytes, offset);
        const std::uint32_t chunkType = readU32(bytes, offset + 4);
        offset += 8;

        if (offset + chunkLength > declaredLength) {
            return GltfSceneInfoLoadResult{{"GLB chunk exceeds declared file length: " + path.string()}};
        }

        const std::string_view chunk = bytes.substr(offset, chunkLength);
        if (chunkType == kGlbJsonChunk) {
            sawJson = true;
            json = chunk;
        } else if (chunkType == kGlbBinaryChunk) {
            binaryBytes += chunkLength;
        }

        offset += chunkLength;
    }

    if (!sawJson) {
        return GltfSceneInfoLoadResult{{"GLB file is missing JSON chunk: " + path.string()}};
    }

    GltfSceneInfo info{};
    info.path = path;
    info.container = GltfContainerKind::BinaryGlb;
    info.glbVersion = version;
    info.declaredLengthBytes = declaredLength;
    info.jsonBytes = json.size();
    info.binaryBytes = binaryBytes;

    const auto result = fillSceneInfoFromJson(json, info);
    if (!result.ok()) {
        return result;
    }

    outInfo = std::move(info);
    return {};
}

} // namespace

std::string_view gltfContainerKindName(GltfContainerKind kind) {
    switch (kind) {
    case GltfContainerKind::Text:
        return "gltf";
    case GltfContainerKind::BinaryGlb:
        return "glb";
    }
    return "unknown";
}

GltfSceneInfoLoadResult parseGltfSceneInfoFromJson(
    std::string_view json,
    GltfSceneInfo& outInfo) {
    GltfSceneInfo info{};
    info.container = GltfContainerKind::Text;
    info.jsonBytes = json.size();

    const auto result = fillSceneInfoFromJson(json, info);
    if (!result.ok()) {
        return result;
    }

    outInfo = std::move(info);
    return {};
}

GltfSceneInfoLoadResult loadGltfSceneInfo(
    const std::filesystem::path& path,
    GltfSceneInfo& outInfo) {
    const auto file = io::readTextFile(path);
    if (!file.has_value()) {
        return GltfSceneInfoLoadResult{{"Could not read glTF file: " + path.string()}};
    }

    const auto extension = lowercaseExtension(path);
    if (extension == ".glb") {
        return parseGlb(path, file->text, outInfo);
    }
    if (extension == ".gltf") {
        auto result = parseGltfSceneInfoFromJson(file->text, outInfo);
        if (result.ok()) {
            outInfo.path = path;
            outInfo.container = GltfContainerKind::Text;
        }
        return result;
    }

    return GltfSceneInfoLoadResult{{"Unsupported glTF file extension: " + path.string()}};
}

} // namespace novacore::assets
