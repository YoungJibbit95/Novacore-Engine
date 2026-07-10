#include "novacore/assets/GltfDocument.hpp"

#include "novacore/io/FileSystem.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace novacore::assets {

namespace {

constexpr std::uint32_t kGlbMagic = 0x46546C67U;
constexpr std::uint32_t kGlbJsonChunk = 0x4E4F534AU;
constexpr std::uint32_t kGlbBinaryChunk = 0x004E4942U;

constexpr int kGltfComponentByte = 5120;
constexpr int kGltfComponentUnsignedByte = 5121;
constexpr int kGltfComponentShort = 5122;
constexpr int kGltfComponentUnsignedShort = 5123;
constexpr int kGltfComponentUnsignedInt = 5125;
constexpr int kGltfComponentFloat = 5126;

struct GltfPayload final {
    std::filesystem::path path;
    GltfContainerKind container = GltfContainerKind::Text;
    std::uint32_t glbVersion = 0;
    std::uint64_t declaredLengthBytes = 0;
    std::string json;
    std::string binary;
};

struct BufferViewDesc final {
    std::size_t bufferIndex = 0;
    std::size_t byteOffset = 0;
    std::size_t byteLength = 0;
    std::size_t byteStride = 0;
};

struct AccessorDesc final {
    std::size_t bufferViewIndex = 0;
    std::size_t byteOffset = 0;
    int componentType = 0;
    std::size_t count = 0;
    std::string type;
    bool normalized = false;
};

struct GltfMat4 final {
    float value[16]{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };

    [[nodiscard]] float at(std::size_t row, std::size_t col) const {
        return value[(row * 4U) + col];
    }

    float& at(std::size_t row, std::size_t col) {
        return value[(row * 4U) + col];
    }
};

struct GltfNodeDesc final {
    std::string name;
    std::optional<std::size_t> meshIndex;
    std::optional<std::size_t> skinIndex;
    int parentNodeIndex = -1;
    std::vector<std::size_t> children;
    GltfNodeTransform transform{};
    GltfMat4 localTransform{};
};

struct MeshNodeInstance final {
    std::size_t nodeIndex = 0;
    int skinIndex = -1;
    GltfMat4 worldTransform{};
};

using GltfBuffers = std::vector<std::string>;

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

[[nodiscard]] std::uint16_t readU16(std::string_view bytes, std::size_t offset) {
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data() + offset);
    return static_cast<std::uint16_t>(data[0]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[1]) << 8U);
}

[[nodiscard]] float readF32(std::string_view bytes, std::size_t offset) {
    float value = 0.0F;
    std::memcpy(&value, bytes.data() + offset, sizeof(float));
    return value;
}

[[nodiscard]] std::optional<std::size_t> sizeValue(
    const core::ConfigDocument& document,
    const std::string& key) {
    const auto value = document.intValue(key);
    if (!value.has_value() || *value < 0) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(*value);
}

[[nodiscard]] std::vector<std::size_t> collectArrayIndices(
    const core::ConfigDocument& document,
    std::string_view prefix) {
    const std::string keyPrefix = std::string(prefix) + ".";
    std::unordered_set<std::size_t> uniqueIndices;

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
        uniqueIndices.insert(index);
    }

    std::vector<std::size_t> indices(uniqueIndices.begin(), uniqueIndices.end());
    std::ranges::sort(indices);
    return indices;
}

[[nodiscard]] std::size_t countArrayEntries(
    const core::ConfigDocument& document,
    std::string_view prefix) {
    return collectArrayIndices(document, prefix).size();
}

[[nodiscard]] std::vector<GltfMaterialData> readMaterials(
    const core::ConfigDocument& document) {
    std::vector<GltfMaterialData> materials;
    const auto indices = collectArrayIndices(document, "materials");
    materials.reserve(indices.size());
    for (const auto index : indices) {
        const std::string prefix = "materials." + std::to_string(index);
        const std::string pbr = prefix + ".pbrMetallicRoughness";
        GltfMaterialData material{};
        material.materialIndex = index;
        material.name = document.stringOr(prefix + ".name", "material_" + std::to_string(index));
        for (std::size_t component = 0; component < material.baseColorFactor.size(); ++component) {
            material.baseColorFactor[component] = static_cast<float>(document.numberOr(
                pbr + ".baseColorFactor." + std::to_string(component),
                1.0));
        }
        material.emissiveFactor = {
            static_cast<float>(document.numberOr(prefix + ".emissiveFactor.0", 0.0)),
            static_cast<float>(document.numberOr(prefix + ".emissiveFactor.1", 0.0)),
            static_cast<float>(document.numberOr(prefix + ".emissiveFactor.2", 0.0)),
        };
        material.metallicFactor = static_cast<float>(document.numberOr(pbr + ".metallicFactor", 1.0));
        material.roughnessFactor = static_cast<float>(document.numberOr(pbr + ".roughnessFactor", 1.0));
        material.baseColorTextureIndex = document.intOr(pbr + ".baseColorTexture.index", -1);
        material.metallicRoughnessTextureIndex = document.intOr(
            pbr + ".metallicRoughnessTexture.index", -1);
        material.normalTextureIndex = document.intOr(prefix + ".normalTexture.index", -1);
        material.emissiveTextureIndex = document.intOr(prefix + ".emissiveTexture.index", -1);
        material.doubleSided = document.boolOr(prefix + ".doubleSided", false);
        materials.push_back(std::move(material));
    }
    return materials;
}

[[nodiscard]] GltfSceneInfoLoadResult parseDocument(
    std::string_view json,
    core::ConfigDocument& outDocument) {
    const auto parseResult = core::parseJsonConfig(json, outDocument);
    if (parseResult.ok()) {
        return {};
    }

    GltfSceneInfoLoadResult result;
    for (const auto& error : parseResult.errors) {
        result.errors.push_back(error.message + " at offset " + std::to_string(error.offset));
    }
    return result;
}

[[nodiscard]] GltfSceneInfoLoadResult fillSceneInfoFromDocument(
    const core::ConfigDocument& document,
    GltfSceneInfo& info) {
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

[[nodiscard]] GltfSceneInfoLoadResult fillSceneInfoFromPayload(
    const GltfPayload& payload,
    const core::ConfigDocument& document,
    GltfSceneInfo& outInfo) {
    GltfSceneInfo info{};
    info.path = payload.path;
    info.container = payload.container;
    info.glbVersion = payload.glbVersion;
    info.declaredLengthBytes = payload.declaredLengthBytes;
    info.jsonBytes = payload.json.size();
    info.binaryBytes = payload.binary.size();

    const auto result = fillSceneInfoFromDocument(document, info);
    if (!result.ok()) {
        return result;
    }

    outInfo = std::move(info);
    return {};
}

[[nodiscard]] GltfSceneInfoLoadResult parsePayloadFromGlb(
    const std::filesystem::path& path,
    std::string_view bytes,
    GltfPayload& outPayload) {
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

    GltfPayload payload{};
    payload.path = path;
    payload.container = GltfContainerKind::BinaryGlb;
    payload.glbVersion = version;
    payload.declaredLengthBytes = declaredLength;

    std::size_t offset = 12;
    while (offset + 8 <= declaredLength) {
        const std::uint32_t chunkLength = readU32(bytes, offset);
        const std::uint32_t chunkType = readU32(bytes, offset + 4);
        offset += 8;

        if (offset + chunkLength > declaredLength) {
            return GltfSceneInfoLoadResult{{"GLB chunk exceeds declared file length: " + path.string()}};
        }

        const std::string_view chunk = bytes.substr(offset, chunkLength);
        if (chunkType == kGlbJsonChunk) {
            payload.json.assign(chunk);
        } else if (chunkType == kGlbBinaryChunk) {
            payload.binary.append(chunk);
        }

        offset += chunkLength;
    }

    if (payload.json.empty()) {
        return GltfSceneInfoLoadResult{{"GLB file is missing JSON chunk: " + path.string()}};
    }

    outPayload = std::move(payload);
    return {};
}

[[nodiscard]] GltfSceneInfoLoadResult loadPayload(
    const std::filesystem::path& path,
    GltfPayload& outPayload) {
    const auto file = io::readTextFile(path);
    if (!file.has_value()) {
        return GltfSceneInfoLoadResult{{"Could not read glTF file: " + path.string()}};
    }

    const auto extension = lowercaseExtension(path);
    if (extension == ".glb") {
        return parsePayloadFromGlb(path, file->text, outPayload);
    }
    if (extension == ".gltf") {
        GltfPayload payload{};
        payload.path = path;
        payload.container = GltfContainerKind::Text;
        payload.json = file->text;
        outPayload = std::move(payload);
        return {};
    }

    return GltfSceneInfoLoadResult{{"Unsupported glTF file extension: " + path.string()}};
}

[[nodiscard]] int hexDigit(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return 10 + value - 'a';
    }
    if (value >= 'A' && value <= 'F') {
        return 10 + value - 'A';
    }
    return -1;
}

[[nodiscard]] bool percentDecode(
    std::string_view encoded,
    std::string& decoded,
    std::string& error) {
    decoded.clear();
    decoded.reserve(encoded.size());
    for (std::size_t index = 0; index < encoded.size(); ++index) {
        if (encoded[index] != '%') {
            decoded.push_back(encoded[index]);
            continue;
        }
        if (index + 2 >= encoded.size()) {
            error = "URI contains an incomplete percent escape";
            return false;
        }
        const int high = hexDigit(encoded[index + 1]);
        const int low = hexDigit(encoded[index + 2]);
        if (high < 0 || low < 0) {
            error = "URI contains an invalid percent escape";
            return false;
        }
        decoded.push_back(static_cast<char>((high << 4) | low));
        index += 2;
    }
    return true;
}

[[nodiscard]] int base64Digit(char value) {
    if (value >= 'A' && value <= 'Z') {
        return value - 'A';
    }
    if (value >= 'a' && value <= 'z') {
        return 26 + value - 'a';
    }
    if (value >= '0' && value <= '9') {
        return 52 + value - '0';
    }
    if (value == '+') {
        return 62;
    }
    if (value == '/') {
        return 63;
    }
    return -1;
}

[[nodiscard]] bool decodeBase64(
    std::string_view encoded,
    std::string& decoded,
    std::string& error) {
    decoded.clear();
    std::array<int, 4> quartet{};
    std::size_t quartetSize = 0;
    bool reachedPadding = false;

    for (const char value : encoded) {
        if (std::isspace(static_cast<unsigned char>(value))) {
            continue;
        }
        if (reachedPadding) {
            error = "base64 data has bytes after padding";
            return false;
        }
        quartet[quartetSize++] = value == '=' ? -2 : base64Digit(value);
        if (quartet[quartetSize - 1] == -1) {
            error = "base64 data contains an invalid character";
            return false;
        }
        if (quartetSize != 4) {
            continue;
        }

        if (quartet[0] < 0 || quartet[1] < 0 || (quartet[2] == -2 && quartet[3] != -2)) {
            error = "base64 data has invalid padding";
            return false;
        }
        decoded.push_back(static_cast<char>((quartet[0] << 2) | (quartet[1] >> 4)));
        if (quartet[2] == -2) {
            reachedPadding = true;
        } else {
            decoded.push_back(static_cast<char>(((quartet[1] & 0x0F) << 4) | (quartet[2] >> 2)));
            if (quartet[3] == -2) {
                reachedPadding = true;
            } else {
                decoded.push_back(static_cast<char>(((quartet[2] & 0x03) << 6) | quartet[3]));
            }
        }
        quartetSize = 0;
    }

    if (quartetSize == 1) {
        error = "base64 data has an invalid trailing group";
        return false;
    }
    if (quartetSize >= 2) {
        decoded.push_back(static_cast<char>((quartet[0] << 2) | (quartet[1] >> 4)));
        if (quartetSize == 3) {
            decoded.push_back(static_cast<char>(((quartet[1] & 0x0F) << 4) | (quartet[2] >> 2)));
        }
    }
    return true;
}

[[nodiscard]] bool loadBufferUri(
    const std::filesystem::path& documentPath,
    std::string_view uri,
    std::string& outBytes,
    std::string& error) {
    if (uri.starts_with("data:")) {
        const auto comma = uri.find(',');
        if (comma == std::string_view::npos) {
            error = "data URI is missing its comma separator";
            return false;
        }
        const auto metadata = uri.substr(5, comma - 5);
        const auto data = uri.substr(comma + 1);
        if (metadata.ends_with(";base64")) {
            return decodeBase64(data, outBytes, error);
        }
        return percentDecode(data, outBytes, error);
    }

    if (uri.find("://") != std::string_view::npos) {
        error = "remote buffer URIs are unsupported";
        return false;
    }

    std::string decodedUri;
    if (!percentDecode(uri, decodedUri, error)) {
        return false;
    }
    const auto bufferPath = (documentPath.parent_path() / std::filesystem::path(decodedUri)).lexically_normal();
    const auto file = io::readTextFile(bufferPath);
    if (!file.has_value()) {
        error = "could not read external buffer '" + bufferPath.string() + "'";
        return false;
    }
    outBytes = file->text;
    return true;
}

[[nodiscard]] bool loadBuffers(
    const GltfPayload& payload,
    const core::ConfigDocument& document,
    GltfBuffers& outBuffers,
    std::vector<std::string>& errors) {
    const auto bufferIndices = collectArrayIndices(document, "buffers");
    if (bufferIndices.empty()) {
        outBuffers.clear();
        return true;
    }

    outBuffers.clear();
    outBuffers.resize(*std::ranges::max_element(bufferIndices) + 1U);
    for (const auto bufferIndex : bufferIndices) {
        const std::string prefix = "buffers." + std::to_string(bufferIndex);
        const auto declaredLength = sizeValue(document, prefix + ".byteLength");
        if (!declaredLength.has_value()) {
            errors.push_back("buffer " + std::to_string(bufferIndex) + " is missing byteLength");
            continue;
        }

        const auto uri = document.stringValue(prefix + ".uri");
        if (!uri.has_value()) {
            if (payload.container != GltfContainerKind::BinaryGlb || bufferIndex != 0) {
                errors.push_back("buffer " + std::to_string(bufferIndex) + " has no URI or GLB BIN chunk");
                continue;
            }
            outBuffers[bufferIndex] = payload.binary;
        } else {
            std::string error;
            if (!loadBufferUri(payload.path, *uri, outBuffers[bufferIndex], error)) {
                errors.push_back("buffer " + std::to_string(bufferIndex) + ": " + error);
                continue;
            }
        }

        if (outBuffers[bufferIndex].size() < *declaredLength) {
            errors.push_back(
                "buffer " + std::to_string(bufferIndex) + " contains " +
                std::to_string(outBuffers[bufferIndex].size()) + " bytes but declares " +
                std::to_string(*declaredLength));
        }
    }
    return errors.empty();
}

[[nodiscard]] GltfMeshDataLoadResult meshError(std::string message) {
    return GltfMeshDataLoadResult{{std::move(message)}};
}

[[nodiscard]] GltfMeshDataLoadResult toMeshResult(const GltfSceneInfoLoadResult& result) {
    return GltfMeshDataLoadResult{result.errors};
}

[[nodiscard]] float numberOrFloat(
    const core::ConfigDocument& document,
    const std::string& key,
    float fallback) {
    return static_cast<float>(document.numberOr(key, static_cast<double>(fallback)));
}

[[nodiscard]] math::Vec3 normalizedOrFallback(math::Vec3 value, math::Vec3 fallback) {
    const float lengthSquared = value.lengthSquared();
    if (lengthSquared <= 0.000001F) {
        return fallback;
    }
    const float invLength = 1.0F / std::sqrt(lengthSquared);
    return math::Vec3{value.x * invLength, value.y * invLength, value.z * invLength};
}

[[nodiscard]] GltfMat4 identityMatrix() {
    return {};
}

[[nodiscard]] GltfMat4 multiply(GltfMat4 lhs, GltfMat4 rhs) {
    GltfMat4 result{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t col = 0; col < 4; ++col) {
            result.at(row, col) =
                (lhs.at(row, 0) * rhs.at(0, col)) +
                (lhs.at(row, 1) * rhs.at(1, col)) +
                (lhs.at(row, 2) * rhs.at(2, col)) +
                (lhs.at(row, 3) * rhs.at(3, col));
        }
    }
    return result;
}

[[nodiscard]] GltfMat4 translation(math::Vec3 value) {
    auto result = identityMatrix();
    result.at(0, 3) = value.x;
    result.at(1, 3) = value.y;
    result.at(2, 3) = value.z;
    return result;
}

[[nodiscard]] GltfMat4 scale(math::Vec3 value) {
    GltfMat4 result{};
    result.at(0, 0) = value.x;
    result.at(1, 1) = value.y;
    result.at(2, 2) = value.z;
    result.at(3, 3) = 1.0F;
    return result;
}

[[nodiscard]] GltfMat4 rotation(math::Quat value) {
    const float lengthSquared =
        (value.x * value.x) +
        (value.y * value.y) +
        (value.z * value.z) +
        (value.w * value.w);
    if (lengthSquared <= 0.000001F) {
        return identityMatrix();
    }

    const float invLength = 1.0F / std::sqrt(lengthSquared);
    const float x = value.x * invLength;
    const float y = value.y * invLength;
    const float z = value.z * invLength;
    const float w = value.w * invLength;

    GltfMat4 result = identityMatrix();
    result.at(0, 0) = 1.0F - (2.0F * y * y) - (2.0F * z * z);
    result.at(0, 1) = (2.0F * x * y) - (2.0F * z * w);
    result.at(0, 2) = (2.0F * x * z) + (2.0F * y * w);
    result.at(1, 0) = (2.0F * x * y) + (2.0F * z * w);
    result.at(1, 1) = 1.0F - (2.0F * x * x) - (2.0F * z * z);
    result.at(1, 2) = (2.0F * y * z) - (2.0F * x * w);
    result.at(2, 0) = (2.0F * x * z) - (2.0F * y * w);
    result.at(2, 1) = (2.0F * y * z) + (2.0F * x * w);
    result.at(2, 2) = 1.0F - (2.0F * x * x) - (2.0F * y * y);
    return result;
}

[[nodiscard]] math::Vec3 transformPoint(GltfMat4 transform, math::Vec3 point) {
    return math::Vec3{
        (transform.at(0, 0) * point.x) + (transform.at(0, 1) * point.y) + (transform.at(0, 2) * point.z) + transform.at(0, 3),
        (transform.at(1, 0) * point.x) + (transform.at(1, 1) * point.y) + (transform.at(1, 2) * point.z) + transform.at(1, 3),
        (transform.at(2, 0) * point.x) + (transform.at(2, 1) * point.y) + (transform.at(2, 2) * point.z) + transform.at(2, 3),
    };
}

[[nodiscard]] math::Vec3 transformVector(GltfMat4 transform, math::Vec3 vector) {
    return math::Vec3{
        (transform.at(0, 0) * vector.x) + (transform.at(0, 1) * vector.y) + (transform.at(0, 2) * vector.z),
        (transform.at(1, 0) * vector.x) + (transform.at(1, 1) * vector.y) + (transform.at(1, 2) * vector.z),
        (transform.at(2, 0) * vector.x) + (transform.at(2, 1) * vector.y) + (transform.at(2, 2) * vector.z),
    };
}

[[nodiscard]] GltfMat4 readMatrixTransform(
    const core::ConfigDocument& document,
    const std::string& prefix) {
    GltfMat4 result{};
    for (std::size_t col = 0; col < 4; ++col) {
        for (std::size_t row = 0; row < 4; ++row) {
            const std::size_t gltfIndex = (col * 4U) + row;
            result.at(row, col) = numberOrFloat(document, prefix + ".matrix." + std::to_string(gltfIndex), row == col ? 1.0F : 0.0F);
        }
    }
    return result;
}

[[nodiscard]] GltfMat4 readTrsTransform(
    const core::ConfigDocument& document,
    const std::string& prefix) {
    const math::Vec3 nodeTranslation{
        numberOrFloat(document, prefix + ".translation.0", 0.0F),
        numberOrFloat(document, prefix + ".translation.1", 0.0F),
        numberOrFloat(document, prefix + ".translation.2", 0.0F),
    };
    const math::Quat nodeRotation{
        numberOrFloat(document, prefix + ".rotation.0", 0.0F),
        numberOrFloat(document, prefix + ".rotation.1", 0.0F),
        numberOrFloat(document, prefix + ".rotation.2", 0.0F),
        numberOrFloat(document, prefix + ".rotation.3", 1.0F),
    };
    const math::Vec3 nodeScale{
        numberOrFloat(document, prefix + ".scale.0", 1.0F),
        numberOrFloat(document, prefix + ".scale.1", 1.0F),
        numberOrFloat(document, prefix + ".scale.2", 1.0F),
    };

    return multiply(translation(nodeTranslation), multiply(rotation(nodeRotation), scale(nodeScale)));
}

[[nodiscard]] GltfMat4 readNodeTransform(
    const core::ConfigDocument& document,
    const std::string& prefix) {
    if (document.contains(prefix + ".matrix.0")) {
        return readMatrixTransform(document, prefix);
    }
    return readTrsTransform(document, prefix);
}

[[nodiscard]] GltfMatrix4 toPublicMatrix(GltfMat4 matrix) {
    GltfMatrix4 result{};
    for (std::size_t col = 0; col < 4; ++col) {
        for (std::size_t row = 0; row < 4; ++row) {
            result.values[(col * 4U) + row] = matrix.at(row, col);
        }
    }
    return result;
}

[[nodiscard]] math::Quat quaternionFromRotationMatrix(GltfMat4 matrix) {
    math::Quat result{};
    const float trace = matrix.at(0, 0) + matrix.at(1, 1) + matrix.at(2, 2);
    if (trace > 0.0F) {
        const float scale = std::sqrt(trace + 1.0F) * 2.0F;
        result.w = 0.25F * scale;
        result.x = (matrix.at(2, 1) - matrix.at(1, 2)) / scale;
        result.y = (matrix.at(0, 2) - matrix.at(2, 0)) / scale;
        result.z = (matrix.at(1, 0) - matrix.at(0, 1)) / scale;
    } else if (matrix.at(0, 0) > matrix.at(1, 1) && matrix.at(0, 0) > matrix.at(2, 2)) {
        const float scale = std::sqrt(1.0F + matrix.at(0, 0) - matrix.at(1, 1) - matrix.at(2, 2)) * 2.0F;
        result.w = (matrix.at(2, 1) - matrix.at(1, 2)) / scale;
        result.x = 0.25F * scale;
        result.y = (matrix.at(0, 1) + matrix.at(1, 0)) / scale;
        result.z = (matrix.at(0, 2) + matrix.at(2, 0)) / scale;
    } else if (matrix.at(1, 1) > matrix.at(2, 2)) {
        const float scale = std::sqrt(1.0F + matrix.at(1, 1) - matrix.at(0, 0) - matrix.at(2, 2)) * 2.0F;
        result.w = (matrix.at(0, 2) - matrix.at(2, 0)) / scale;
        result.x = (matrix.at(0, 1) + matrix.at(1, 0)) / scale;
        result.y = 0.25F * scale;
        result.z = (matrix.at(1, 2) + matrix.at(2, 1)) / scale;
    } else {
        const float scale = std::sqrt(1.0F + matrix.at(2, 2) - matrix.at(0, 0) - matrix.at(1, 1)) * 2.0F;
        result.w = (matrix.at(1, 0) - matrix.at(0, 1)) / scale;
        result.x = (matrix.at(0, 2) + matrix.at(2, 0)) / scale;
        result.y = (matrix.at(1, 2) + matrix.at(2, 1)) / scale;
        result.z = 0.25F * scale;
    }
    const float lengthSquared =
        (result.x * result.x) + (result.y * result.y) + (result.z * result.z) + (result.w * result.w);
    if (lengthSquared <= 0.000001F || !std::isfinite(lengthSquared)) {
        return {};
    }
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    result.x *= inverseLength;
    result.y *= inverseLength;
    result.z *= inverseLength;
    result.w *= inverseLength;
    return result;
}

void decomposeNodeMatrix(GltfMat4 matrix, GltfNodeTransform& transform) {
    transform.translation = math::Vec3{matrix.at(0, 3), matrix.at(1, 3), matrix.at(2, 3)};
    math::Vec3 column0{matrix.at(0, 0), matrix.at(1, 0), matrix.at(2, 0)};
    const math::Vec3 column1{matrix.at(0, 1), matrix.at(1, 1), matrix.at(2, 1)};
    const math::Vec3 column2{matrix.at(0, 2), matrix.at(1, 2), matrix.at(2, 2)};
    float scaleX = math::length(column0);
    const float scaleY = math::length(column1);
    const float scaleZ = math::length(column2);
    const float determinant =
        (column0.x * ((column1.y * column2.z) - (column1.z * column2.y))) -
        (column1.x * ((column0.y * column2.z) - (column0.z * column2.y))) +
        (column2.x * ((column0.y * column1.z) - (column0.z * column1.y)));
    if (determinant < 0.0F) {
        scaleX = -scaleX;
    }
    transform.scale = math::Vec3{scaleX, scaleY, scaleZ};
    if (std::abs(scaleX) <= 0.000001F || scaleY <= 0.000001F || scaleZ <= 0.000001F) {
        transform.rotation = {};
        return;
    }

    GltfMat4 rotationMatrix = identityMatrix();
    for (std::size_t row = 0; row < 3; ++row) {
        rotationMatrix.at(row, 0) = matrix.at(row, 0) / scaleX;
        rotationMatrix.at(row, 1) = matrix.at(row, 1) / scaleY;
        rotationMatrix.at(row, 2) = matrix.at(row, 2) / scaleZ;
    }
    transform.rotation = quaternionFromRotationMatrix(rotationMatrix);
}

[[nodiscard]] GltfNodeTransform readNodeTransformData(
    const core::ConfigDocument& document,
    const std::string& prefix) {
    GltfNodeTransform transform{};
    transform.translation = math::Vec3{
        numberOrFloat(document, prefix + ".translation.0", 0.0F),
        numberOrFloat(document, prefix + ".translation.1", 0.0F),
        numberOrFloat(document, prefix + ".translation.2", 0.0F),
    };
    transform.rotation = math::Quat{
        numberOrFloat(document, prefix + ".rotation.0", 0.0F),
        numberOrFloat(document, prefix + ".rotation.1", 0.0F),
        numberOrFloat(document, prefix + ".rotation.2", 0.0F),
        numberOrFloat(document, prefix + ".rotation.3", 1.0F),
    };
    transform.scale = math::Vec3{
        numberOrFloat(document, prefix + ".scale.0", 1.0F),
        numberOrFloat(document, prefix + ".scale.1", 1.0F),
        numberOrFloat(document, prefix + ".scale.2", 1.0F),
    };
    transform.usesMatrix = document.contains(prefix + ".matrix.0");
    const auto localMatrix = readNodeTransform(document, prefix);
    transform.matrix = toPublicMatrix(localMatrix);
    if (transform.usesMatrix) {
        decomposeNodeMatrix(localMatrix, transform);
    } else {
        const float lengthSquared =
            (transform.rotation.x * transform.rotation.x) +
            (transform.rotation.y * transform.rotation.y) +
            (transform.rotation.z * transform.rotation.z) +
            (transform.rotation.w * transform.rotation.w);
        if (lengthSquared > 0.000001F && std::isfinite(lengthSquared)) {
            const float inverseLength = 1.0F / std::sqrt(lengthSquared);
            transform.rotation.x *= inverseLength;
            transform.rotation.y *= inverseLength;
            transform.rotation.z *= inverseLength;
            transform.rotation.w *= inverseLength;
        }
    }
    return transform;
}

[[nodiscard]] std::vector<GltfNodeDesc> readNodes(
    const core::ConfigDocument& document,
    std::vector<std::string>& errors) {
    const auto nodeIndices = collectArrayIndices(document, "nodes");
    std::vector<GltfNodeDesc> nodes;
    if (nodeIndices.empty()) {
        return nodes;
    }

    const auto maxNodeIndex = *std::ranges::max_element(nodeIndices);
    nodes.resize(maxNodeIndex + 1U);

    for (const auto nodeIndex : nodeIndices) {
        const std::string prefix = "nodes." + std::to_string(nodeIndex);
        const bool hasMatrix = document.contains(prefix + ".matrix.0");
        const bool hasTrs = document.contains(prefix + ".translation.0") ||
            document.contains(prefix + ".rotation.0") || document.contains(prefix + ".scale.0");
        if (hasMatrix && hasTrs) {
            errors.push_back("node " + std::to_string(nodeIndex) + " must not define both matrix and TRS transforms");
        }
        nodes[nodeIndex].name = document.stringOr(prefix + ".name", "");
        if (const auto meshIndex = sizeValue(document, prefix + ".mesh"); meshIndex.has_value()) {
            nodes[nodeIndex].meshIndex = *meshIndex;
        }
        if (const auto skinIndex = sizeValue(document, prefix + ".skin"); skinIndex.has_value()) {
            nodes[nodeIndex].skinIndex = *skinIndex;
        }
        for (const auto childArrayIndex : collectArrayIndices(document, prefix + ".children")) {
            if (const auto childNodeIndex = sizeValue(document, prefix + ".children." + std::to_string(childArrayIndex));
                childNodeIndex.has_value()) {
                nodes[nodeIndex].children.push_back(*childNodeIndex);
            }
        }
        nodes[nodeIndex].transform = readNodeTransformData(document, prefix);
        nodes[nodeIndex].localTransform = readNodeTransform(document, prefix);
    }

    for (std::size_t parentIndex = 0; parentIndex < nodes.size(); ++parentIndex) {
        for (const auto childIndex : nodes[parentIndex].children) {
            if (childIndex >= nodes.size()) {
                errors.push_back(
                    "node " + std::to_string(parentIndex) + " references missing child node " +
                    std::to_string(childIndex));
                continue;
            }
            if (nodes[childIndex].parentNodeIndex >= 0) {
                errors.push_back(
                    "node " + std::to_string(childIndex) + " has multiple parents " +
                    std::to_string(nodes[childIndex].parentNodeIndex) + " and " + std::to_string(parentIndex));
                continue;
            }
            nodes[childIndex].parentNodeIndex = static_cast<int>(parentIndex);
        }
    }

    return nodes;
}

[[nodiscard]] std::vector<GltfNodeData> publicNodes(const std::vector<GltfNodeDesc>& nodes) {
    std::vector<GltfNodeData> result;
    result.reserve(nodes.size());
    for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
        const auto& node = nodes[nodeIndex];
        result.push_back(GltfNodeData{
            nodeIndex,
            node.name,
            node.parentNodeIndex,
            node.children,
            node.meshIndex.has_value() ? static_cast<int>(*node.meshIndex) : -1,
            node.skinIndex.has_value() ? static_cast<int>(*node.skinIndex) : -1,
            node.transform,
        });
    }
    return result;
}

[[nodiscard]] std::vector<std::size_t> sceneRootNodes(
    const core::ConfigDocument& document,
    const std::vector<GltfNodeDesc>& nodes) {
    std::vector<std::size_t> roots;
    const auto sceneIndex = sizeValue(document, "scene").value_or(0);
    const std::string sceneNodesPrefix = "scenes." + std::to_string(sceneIndex) + ".nodes";
    for (const auto rootArrayIndex : collectArrayIndices(document, sceneNodesPrefix)) {
        if (const auto rootNodeIndex = sizeValue(document, sceneNodesPrefix + "." + std::to_string(rootArrayIndex));
            rootNodeIndex.has_value()) {
            roots.push_back(*rootNodeIndex);
        }
    }

    if (!roots.empty()) {
        return roots;
    }

    std::vector<bool> referenced(nodes.size(), false);
    for (const auto& node : nodes) {
        for (const auto childIndex : node.children) {
            if (childIndex < referenced.size()) {
                referenced[childIndex] = true;
            }
        }
    }
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        if (!referenced[index]) {
            roots.push_back(index);
        }
    }
    return roots;
}

void collectMeshNodeTransforms(
    const std::vector<GltfNodeDesc>& nodes,
    std::size_t nodeIndex,
    GltfMat4 parentTransform,
    std::vector<bool>& activeStack,
    std::unordered_map<std::size_t, std::vector<MeshNodeInstance>>& meshTransforms,
    std::vector<GltfNodeMarker>& nodeMarkers,
    std::vector<std::string>& errors) {
    if (nodeIndex >= nodes.size()) {
        errors.push_back("node hierarchy references missing node " + std::to_string(nodeIndex));
        return;
    }
    if (activeStack[nodeIndex]) {
        errors.push_back("node hierarchy contains a cycle at node " + std::to_string(nodeIndex));
        return;
    }

    activeStack[nodeIndex] = true;
    const auto& node = nodes[nodeIndex];
    const auto worldTransform = multiply(parentTransform, node.localTransform);
    if (!node.name.empty()) {
        nodeMarkers.push_back(GltfNodeMarker{
            node.name,
            transformPoint(worldTransform, {}),
            normalizedOrFallback(transformVector(worldTransform, {0.0F, 0.0F, 1.0F}), {0.0F, 0.0F, 1.0F}),
            normalizedOrFallback(transformVector(worldTransform, {0.0F, 1.0F, 0.0F}), {0.0F, 1.0F, 0.0F}),
            node.meshIndex.has_value() ? static_cast<int>(*node.meshIndex) : -1,
        });
    }
    if (node.meshIndex.has_value()) {
        meshTransforms[*node.meshIndex].push_back(MeshNodeInstance{
            nodeIndex,
            node.skinIndex.has_value() ? static_cast<int>(*node.skinIndex) : -1,
            worldTransform,
        });
    }

    for (const auto childIndex : node.children) {
        collectMeshNodeTransforms(nodes, childIndex, worldTransform, activeStack, meshTransforms, nodeMarkers, errors);
    }
    activeStack[nodeIndex] = false;
}

[[nodiscard]] std::unordered_map<std::size_t, std::vector<MeshNodeInstance>> meshNodeTransforms(
    const core::ConfigDocument& document,
    const std::vector<GltfNodeDesc>& nodes,
    std::vector<GltfNodeMarker>& nodeMarkers,
    std::vector<std::string>& errors) {
    std::unordered_map<std::size_t, std::vector<MeshNodeInstance>> transforms;
    if (nodes.empty()) {
        return transforms;
    }

    std::vector<bool> activeStack(nodes.size(), false);
    for (const auto rootIndex : sceneRootNodes(document, nodes)) {
        collectMeshNodeTransforms(nodes, rootIndex, identityMatrix(), activeStack, transforms, nodeMarkers, errors);
    }
    return transforms;
}

void validateNodeHierarchyVisit(
    const std::vector<GltfNodeDesc>& nodes,
    std::size_t nodeIndex,
    std::vector<unsigned char>& states,
    std::vector<std::string>& errors) {
    if (nodeIndex >= nodes.size() || states[nodeIndex] == 2) {
        return;
    }
    if (states[nodeIndex] == 1) {
        errors.push_back("node hierarchy contains a cycle at node " + std::to_string(nodeIndex));
        return;
    }
    states[nodeIndex] = 1;
    for (const auto childIndex : nodes[nodeIndex].children) {
        if (childIndex < nodes.size()) {
            validateNodeHierarchyVisit(nodes, childIndex, states, errors);
        }
    }
    states[nodeIndex] = 2;
}

void validateNodeHierarchy(
    const std::vector<GltfNodeDesc>& nodes,
    std::vector<std::string>& errors) {
    std::vector<unsigned char> states(nodes.size(), 0);
    for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
        validateNodeHierarchyVisit(nodes, nodeIndex, states, errors);
    }
}

void applyNodeTransform(GltfPrimitiveData& primitive, GltfMat4 transform) {
    for (auto& position : primitive.positions) {
        position = transformPoint(transform, position);
    }
    for (auto& normal : primitive.normals) {
        normal = normalizedOrFallback(transformVector(transform, normal), {0.0F, 1.0F, 0.0F});
    }
}

[[nodiscard]] std::optional<BufferViewDesc> readBufferView(
    const core::ConfigDocument& document,
    std::size_t index,
    std::vector<std::string>& errors) {
    const std::string prefix = "bufferViews." + std::to_string(index);
    const auto buffer = sizeValue(document, prefix + ".buffer");
    const auto byteLength = sizeValue(document, prefix + ".byteLength");

    if (!buffer.has_value() || !byteLength.has_value()) {
        errors.push_back("bufferView " + std::to_string(index) + " is missing buffer or byteLength");
        return std::nullopt;
    }

    BufferViewDesc desc{};
    desc.bufferIndex = *buffer;
    desc.byteLength = *byteLength;
    desc.byteOffset = sizeValue(document, prefix + ".byteOffset").value_or(0);
    desc.byteStride = sizeValue(document, prefix + ".byteStride").value_or(0);
    return desc;
}

[[nodiscard]] std::optional<AccessorDesc> readAccessor(
    const core::ConfigDocument& document,
    std::size_t index,
    std::vector<std::string>& errors) {
    const std::string prefix = "accessors." + std::to_string(index);
    const auto bufferView = sizeValue(document, prefix + ".bufferView");
    const auto componentType = document.intValue(prefix + ".componentType");
    const auto count = sizeValue(document, prefix + ".count");
    const auto type = document.stringValue(prefix + ".type");

    if (!bufferView.has_value() || !componentType.has_value() || !count.has_value() || !type.has_value()) {
        errors.push_back("accessor " + std::to_string(index) + " is missing bufferView, componentType, count, or type");
        return std::nullopt;
    }

    AccessorDesc desc{};
    desc.bufferViewIndex = *bufferView;
    desc.byteOffset = sizeValue(document, prefix + ".byteOffset").value_or(0);
    desc.componentType = *componentType;
    desc.count = *count;
    desc.type = *type;
    desc.normalized = document.boolOr(prefix + ".normalized", false);
    return desc;
}

[[nodiscard]] std::size_t componentByteSize(int componentType) {
    switch (componentType) {
    case kGltfComponentByte:
    case kGltfComponentUnsignedByte:
        return 1;
    case kGltfComponentShort:
    case kGltfComponentUnsignedShort:
        return 2;
    case kGltfComponentUnsignedInt:
    case kGltfComponentFloat:
        return 4;
    default:
        return 0;
    }
}

[[nodiscard]] std::size_t componentCountForType(std::string_view type) {
    if (type == "SCALAR") {
        return 1;
    }
    if (type == "VEC2") {
        return 2;
    }
    if (type == "VEC3") {
        return 3;
    }
    if (type == "VEC4") {
        return 4;
    }
    if (type == "MAT4") {
        return 16;
    }
    return 0;
}

[[nodiscard]] std::optional<std::size_t> accessorElementOffset(
    const AccessorDesc& accessor,
    const BufferViewDesc& view,
    std::size_t elementIndex,
    std::size_t componentCount,
    std::size_t componentSize,
    std::size_t binarySize,
    std::vector<std::string>& errors,
    std::string_view label) {
    const std::size_t packedStride = componentCount * componentSize;
    const std::size_t stride = view.byteStride == 0 ? packedStride : view.byteStride;
    if (stride < packedStride) {
        errors.push_back(std::string(label) + " has byteStride smaller than packed accessor size");
        return std::nullopt;
    }

    const std::size_t offset = view.byteOffset + accessor.byteOffset + (elementIndex * stride);
    if (offset + packedStride > binarySize) {
        errors.push_back(std::string(label) + " reads past binary buffer");
        return std::nullopt;
    }
    if ((offset - view.byteOffset) + packedStride > view.byteLength) {
        errors.push_back(std::string(label) + " reads past bufferView byteLength");
        return std::nullopt;
    }

    return offset;
}

bool readFloatVectors(
    const core::ConfigDocument& document,
    const GltfBuffers& buffers,
    std::size_t accessorIndex,
    std::string_view requiredType,
    std::vector<math::Vec3>& outValues,
    std::vector<std::string>& errors,
    std::string_view label) {
    const auto accessor = readAccessor(document, accessorIndex, errors);
    if (!accessor.has_value()) {
        return false;
    }
    if (accessor->componentType != kGltfComponentFloat || accessor->type != requiredType) {
        errors.push_back(std::string(label) + " must be FLOAT " + std::string(requiredType));
        return false;
    }

    const auto view = readBufferView(document, accessor->bufferViewIndex, errors);
    if (!view.has_value()) {
        return false;
    }
    if (view->bufferIndex >= buffers.size()) {
        errors.push_back(std::string(label) + " references missing buffer " + std::to_string(view->bufferIndex));
        return false;
    }
    const std::string_view binary = buffers[view->bufferIndex];

    const std::size_t componentCount = componentCountForType(accessor->type);
    const std::size_t componentSize = componentByteSize(accessor->componentType);
    outValues.clear();
    outValues.reserve(accessor->count);

    for (std::size_t index = 0; index < accessor->count; ++index) {
        const auto offset = accessorElementOffset(
            *accessor,
            *view,
            index,
            componentCount,
            componentSize,
            binary.size(),
            errors,
            label);
        if (!offset.has_value()) {
            return false;
        }

        outValues.push_back(math::Vec3{
            readF32(binary, *offset),
            readF32(binary, *offset + 4),
            componentCount >= 3 ? readF32(binary, *offset + 8) : 0.0F,
        });
    }

    return true;
}

bool readTexcoords(
    const core::ConfigDocument& document,
    const GltfBuffers& buffers,
    std::size_t accessorIndex,
    std::vector<math::Vec2>& outValues,
    std::vector<std::string>& errors) {
    const auto accessor = readAccessor(document, accessorIndex, errors);
    if (!accessor.has_value()) {
        return false;
    }
    if (accessor->componentType != kGltfComponentFloat || accessor->type != "VEC2") {
        errors.push_back("TEXCOORD_0 accessor must be FLOAT VEC2");
        return false;
    }

    const auto view = readBufferView(document, accessor->bufferViewIndex, errors);
    if (!view.has_value()) {
        return false;
    }
    if (view->bufferIndex >= buffers.size()) {
        errors.push_back("TEXCOORD_0 accessor references missing buffer " + std::to_string(view->bufferIndex));
        return false;
    }
    const std::string_view binary = buffers[view->bufferIndex];

    const std::size_t componentCount = componentCountForType(accessor->type);
    const std::size_t componentSize = componentByteSize(accessor->componentType);
    outValues.clear();
    outValues.reserve(accessor->count);

    for (std::size_t index = 0; index < accessor->count; ++index) {
        const auto offset = accessorElementOffset(
            *accessor,
            *view,
            index,
            componentCount,
            componentSize,
            binary.size(),
            errors,
            "TEXCOORD_0 accessor");
        if (!offset.has_value()) {
            return false;
        }

        outValues.push_back(math::Vec2{
            readF32(binary, *offset),
            readF32(binary, *offset + 4),
        });
    }

    return true;
}

bool readIndices(
    const core::ConfigDocument& document,
    const GltfBuffers& buffers,
    std::size_t accessorIndex,
    std::vector<std::uint32_t>& outIndices,
    std::vector<std::string>& errors) {
    const auto accessor = readAccessor(document, accessorIndex, errors);
    if (!accessor.has_value()) {
        return false;
    }
    if (accessor->type != "SCALAR") {
        errors.push_back("index accessor must be SCALAR");
        return false;
    }

    const auto componentSize = componentByteSize(accessor->componentType);
    if (componentSize == 0 ||
        (accessor->componentType != kGltfComponentUnsignedByte &&
         accessor->componentType != kGltfComponentUnsignedShort &&
         accessor->componentType != kGltfComponentUnsignedInt)) {
        errors.push_back("index accessor must use unsigned byte, unsigned short, or unsigned int components");
        return false;
    }

    const auto view = readBufferView(document, accessor->bufferViewIndex, errors);
    if (!view.has_value()) {
        return false;
    }
    if (view->bufferIndex >= buffers.size()) {
        errors.push_back("index accessor references missing buffer " + std::to_string(view->bufferIndex));
        return false;
    }
    const std::string_view binary = buffers[view->bufferIndex];

    outIndices.clear();
    outIndices.reserve(accessor->count);
    for (std::size_t index = 0; index < accessor->count; ++index) {
        const auto offset = accessorElementOffset(
            *accessor,
            *view,
            index,
            1,
            componentSize,
            binary.size(),
            errors,
            "index accessor");
        if (!offset.has_value()) {
            return false;
        }

        if (accessor->componentType == kGltfComponentUnsignedByte) {
            outIndices.push_back(static_cast<unsigned char>(binary[*offset]));
        } else if (accessor->componentType == kGltfComponentUnsignedShort) {
            outIndices.push_back(readU16(binary, *offset));
        } else {
            outIndices.push_back(readU32(binary, *offset));
        }
    }

    return true;
}

bool readFloatComponents(
    const core::ConfigDocument& document,
    const GltfBuffers& buffers,
    std::size_t accessorIndex,
    std::string_view requiredType,
    std::vector<std::array<float, 4>>& outValues,
    std::vector<std::string>& errors,
    std::string_view label) {
    const auto accessor = readAccessor(document, accessorIndex, errors);
    if (!accessor.has_value()) {
        return false;
    }
    if (accessor->componentType != kGltfComponentFloat || accessor->type != requiredType) {
        errors.push_back(std::string(label) + " must be FLOAT " + std::string(requiredType));
        return false;
    }
    const auto view = readBufferView(document, accessor->bufferViewIndex, errors);
    if (!view.has_value()) {
        return false;
    }
    if (view->bufferIndex >= buffers.size()) {
        errors.push_back(std::string(label) + " references missing buffer " + std::to_string(view->bufferIndex));
        return false;
    }

    const std::size_t componentCount = componentCountForType(requiredType);
    const std::string_view binary = buffers[view->bufferIndex];
    outValues.clear();
    outValues.reserve(accessor->count);
    for (std::size_t elementIndex = 0; elementIndex < accessor->count; ++elementIndex) {
        const auto offset = accessorElementOffset(
            *accessor,
            *view,
            elementIndex,
            componentCount,
            sizeof(float),
            binary.size(),
            errors,
            label);
        if (!offset.has_value()) {
            return false;
        }
        std::array<float, 4> value{};
        for (std::size_t component = 0; component < componentCount; ++component) {
            value[component] = readF32(binary, *offset + (component * sizeof(float)));
            if (!std::isfinite(value[component])) {
                errors.push_back(std::string(label) + " contains a non-finite value");
                return false;
            }
        }
        outValues.push_back(value);
    }
    return true;
}

bool readMatrices(
    const core::ConfigDocument& document,
    const GltfBuffers& buffers,
    std::size_t accessorIndex,
    std::vector<GltfMatrix4>& outMatrices,
    std::vector<std::string>& errors,
    std::string_view label) {
    const auto accessor = readAccessor(document, accessorIndex, errors);
    if (!accessor.has_value()) {
        return false;
    }
    if (accessor->componentType != kGltfComponentFloat || accessor->type != "MAT4") {
        errors.push_back(std::string(label) + " must be FLOAT MAT4");
        return false;
    }
    const auto view = readBufferView(document, accessor->bufferViewIndex, errors);
    if (!view.has_value()) {
        return false;
    }
    if (view->bufferIndex >= buffers.size()) {
        errors.push_back(std::string(label) + " references missing buffer " + std::to_string(view->bufferIndex));
        return false;
    }

    const std::string_view binary = buffers[view->bufferIndex];
    outMatrices.clear();
    outMatrices.reserve(accessor->count);
    for (std::size_t elementIndex = 0; elementIndex < accessor->count; ++elementIndex) {
        const auto offset = accessorElementOffset(
            *accessor,
            *view,
            elementIndex,
            16,
            sizeof(float),
            binary.size(),
            errors,
            label);
        if (!offset.has_value()) {
            return false;
        }
        GltfMatrix4 matrix{};
        for (std::size_t component = 0; component < 16; ++component) {
            matrix.values[component] = readF32(binary, *offset + (component * sizeof(float)));
            if (!std::isfinite(matrix.values[component])) {
                errors.push_back(std::string(label) + " contains a non-finite value");
                return false;
            }
        }
        outMatrices.push_back(matrix);
    }
    return true;
}

bool readJoints(
    const core::ConfigDocument& document,
    const GltfBuffers& buffers,
    std::size_t accessorIndex,
    std::vector<std::array<std::uint16_t, 4>>& outJoints,
    std::vector<std::string>& errors) {
    const auto accessor = readAccessor(document, accessorIndex, errors);
    if (!accessor.has_value()) {
        return false;
    }
    if (accessor->type != "VEC4" ||
        (accessor->componentType != kGltfComponentUnsignedByte &&
         accessor->componentType != kGltfComponentUnsignedShort)) {
        errors.push_back("JOINTS_0 accessor must be UNSIGNED_BYTE or UNSIGNED_SHORT VEC4");
        return false;
    }
    if (accessor->normalized) {
        errors.push_back("JOINTS_0 accessor must not be normalized");
        return false;
    }
    const auto view = readBufferView(document, accessor->bufferViewIndex, errors);
    if (!view.has_value()) {
        return false;
    }
    if (view->bufferIndex >= buffers.size()) {
        errors.push_back("JOINTS_0 accessor references missing buffer " + std::to_string(view->bufferIndex));
        return false;
    }

    const std::string_view binary = buffers[view->bufferIndex];
    const auto componentSize = componentByteSize(accessor->componentType);
    outJoints.clear();
    outJoints.reserve(accessor->count);
    for (std::size_t elementIndex = 0; elementIndex < accessor->count; ++elementIndex) {
        const auto offset = accessorElementOffset(
            *accessor,
            *view,
            elementIndex,
            4,
            componentSize,
            binary.size(),
            errors,
            "JOINTS_0 accessor");
        if (!offset.has_value()) {
            return false;
        }
        std::array<std::uint16_t, 4> value{};
        for (std::size_t component = 0; component < 4; ++component) {
            value[component] = accessor->componentType == kGltfComponentUnsignedByte
                ? static_cast<unsigned char>(binary[*offset + component])
                : readU16(binary, *offset + (component * sizeof(std::uint16_t)));
        }
        outJoints.push_back(value);
    }
    return true;
}

bool readWeights(
    const core::ConfigDocument& document,
    const GltfBuffers& buffers,
    std::size_t accessorIndex,
    std::vector<std::array<float, 4>>& outWeights,
    std::vector<std::string>& errors) {
    const auto accessor = readAccessor(document, accessorIndex, errors);
    if (!accessor.has_value()) {
        return false;
    }
    if (accessor->type != "VEC4" ||
        (accessor->componentType != kGltfComponentFloat &&
         accessor->componentType != kGltfComponentUnsignedByte &&
         accessor->componentType != kGltfComponentUnsignedShort)) {
        errors.push_back("WEIGHTS_0 accessor must be FLOAT, normalized UNSIGNED_BYTE, or normalized UNSIGNED_SHORT VEC4");
        return false;
    }
    if (accessor->componentType != kGltfComponentFloat && !accessor->normalized) {
        errors.push_back("integer WEIGHTS_0 accessor must be normalized");
        return false;
    }
    const auto view = readBufferView(document, accessor->bufferViewIndex, errors);
    if (!view.has_value()) {
        return false;
    }
    if (view->bufferIndex >= buffers.size()) {
        errors.push_back("WEIGHTS_0 accessor references missing buffer " + std::to_string(view->bufferIndex));
        return false;
    }

    const std::string_view binary = buffers[view->bufferIndex];
    const auto componentSize = componentByteSize(accessor->componentType);
    const float divisor = accessor->componentType == kGltfComponentUnsignedByte ? 255.0F : 65535.0F;
    outWeights.clear();
    outWeights.reserve(accessor->count);
    for (std::size_t elementIndex = 0; elementIndex < accessor->count; ++elementIndex) {
        const auto offset = accessorElementOffset(
            *accessor,
            *view,
            elementIndex,
            4,
            componentSize,
            binary.size(),
            errors,
            "WEIGHTS_0 accessor");
        if (!offset.has_value()) {
            return false;
        }

        std::array<float, 4> value{};
        float total = 0.0F;
        for (std::size_t component = 0; component < 4; ++component) {
            if (accessor->componentType == kGltfComponentFloat) {
                value[component] = readF32(binary, *offset + (component * sizeof(float)));
            } else if (accessor->componentType == kGltfComponentUnsignedByte) {
                value[component] = static_cast<unsigned char>(binary[*offset + component]) / divisor;
            } else {
                value[component] = readU16(binary, *offset + (component * sizeof(std::uint16_t))) / divisor;
            }
            if (!std::isfinite(value[component]) || value[component] < 0.0F) {
                errors.push_back("WEIGHTS_0 accessor contains an invalid weight");
                return false;
            }
            total += value[component];
        }
        if (total <= 0.000001F) {
            errors.push_back("WEIGHTS_0 accessor contains a vertex with zero total weight");
            return false;
        }
        for (auto& component : value) {
            component /= total;
        }
        outWeights.push_back(value);
    }
    return true;
}

[[nodiscard]] std::vector<GltfSkinData> readSkins(
    const core::ConfigDocument& document,
    const GltfBuffers& buffers,
    const std::vector<GltfNodeDesc>& nodes,
    std::vector<std::string>& errors) {
    std::vector<GltfSkinData> skins;
    const auto skinIndices = collectArrayIndices(document, "skins");
    skins.reserve(skinIndices.size());
    for (const auto skinIndex : skinIndices) {
        const std::string prefix = "skins." + std::to_string(skinIndex);
        GltfSkinData skin{};
        skin.skinIndex = skinIndex;
        skin.name = document.stringOr(prefix + ".name", "");
        if (const auto root = sizeValue(document, prefix + ".skeleton"); root.has_value()) {
            if (*root >= nodes.size()) {
                errors.push_back("skin " + std::to_string(skinIndex) + " references missing skeleton node " + std::to_string(*root));
            } else {
                skin.skeletonRootNodeIndex = static_cast<int>(*root);
            }
        }

        std::vector<std::size_t> jointNodeIndices;
        std::unordered_set<std::size_t> uniqueJoints;
        for (const auto jointArrayIndex : collectArrayIndices(document, prefix + ".joints")) {
            const auto jointNodeIndex = sizeValue(document, prefix + ".joints." + std::to_string(jointArrayIndex));
            if (!jointNodeIndex.has_value()) {
                errors.push_back("skin " + std::to_string(skinIndex) + " has an invalid joint node index");
                continue;
            }
            if (*jointNodeIndex >= nodes.size()) {
                errors.push_back("skin " + std::to_string(skinIndex) + " references missing joint node " + std::to_string(*jointNodeIndex));
                continue;
            }
            if (!uniqueJoints.insert(*jointNodeIndex).second) {
                errors.push_back("skin " + std::to_string(skinIndex) + " contains duplicate joint node " + std::to_string(*jointNodeIndex));
                continue;
            }
            jointNodeIndices.push_back(*jointNodeIndex);
        }
        if (jointNodeIndices.empty()) {
            errors.push_back("skin " + std::to_string(skinIndex) + " has no joints");
            skins.push_back(std::move(skin));
            continue;
        }

        std::vector<GltfMatrix4> inverseBindMatrices(jointNodeIndices.size());
        if (const auto accessorIndex = sizeValue(document, prefix + ".inverseBindMatrices"); accessorIndex.has_value()) {
            std::vector<GltfMatrix4> importedMatrices;
            if (readMatrices(
                    document,
                    buffers,
                    *accessorIndex,
                    importedMatrices,
                    errors,
                    "skin " + std::to_string(skinIndex) + " inverseBindMatrices")) {
                if (importedMatrices.size() != jointNodeIndices.size()) {
                    errors.push_back(
                        "skin " + std::to_string(skinIndex) + " has " + std::to_string(jointNodeIndices.size()) +
                        " joints but " + std::to_string(importedMatrices.size()) + " inverse bind matrices");
                } else {
                    inverseBindMatrices = std::move(importedMatrices);
                }
            }
        }

        std::unordered_map<std::size_t, std::size_t> jointByNode;
        for (std::size_t jointIndex = 0; jointIndex < jointNodeIndices.size(); ++jointIndex) {
            jointByNode.insert_or_assign(jointNodeIndices[jointIndex], jointIndex);
        }
        for (std::size_t jointIndex = 0; jointIndex < jointNodeIndices.size(); ++jointIndex) {
            const auto nodeIndex = jointNodeIndices[jointIndex];
            int parentJointIndex = -1;
            int parentNodeIndex = nodes[nodeIndex].parentNodeIndex;
            while (parentNodeIndex >= 0) {
                const auto found = jointByNode.find(static_cast<std::size_t>(parentNodeIndex));
                if (found != jointByNode.end()) {
                    parentJointIndex = static_cast<int>(found->second);
                    break;
                }
                parentNodeIndex = nodes[static_cast<std::size_t>(parentNodeIndex)].parentNodeIndex;
            }
            skin.joints.push_back(GltfJointData{
                nodeIndex,
                nodes[nodeIndex].name,
                parentJointIndex,
                nodes[nodeIndex].transform,
                inverseBindMatrices[jointIndex],
            });
        }
        skins.push_back(std::move(skin));
    }
    return skins;
}

[[nodiscard]] bool normalizeQuaternion(math::Quat& value) {
    const float lengthSquared =
        (value.x * value.x) + (value.y * value.y) + (value.z * value.z) + (value.w * value.w);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.000001F) {
        return false;
    }
    const float invLength = 1.0F / std::sqrt(lengthSquared);
    value.x *= invLength;
    value.y *= invLength;
    value.z *= invLength;
    value.w *= invLength;
    return true;
}

[[nodiscard]] std::optional<GltfAnimationInterpolation> animationInterpolation(
    std::string_view value) {
    if (value == "LINEAR") {
        return GltfAnimationInterpolation::Linear;
    }
    if (value == "STEP") {
        return GltfAnimationInterpolation::Step;
    }
    if (value == "CUBICSPLINE") {
        return GltfAnimationInterpolation::CubicSpline;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<GltfAnimationTargetPath> animationTargetPath(
    std::string_view value) {
    if (value == "translation") {
        return GltfAnimationTargetPath::Translation;
    }
    if (value == "rotation") {
        return GltfAnimationTargetPath::Rotation;
    }
    if (value == "scale") {
        return GltfAnimationTargetPath::Scale;
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<GltfAnimationClipData> readAnimations(
    const core::ConfigDocument& document,
    const GltfBuffers& buffers,
    const std::vector<GltfNodeDesc>& nodes,
    std::vector<std::string>& errors) {
    std::vector<GltfAnimationClipData> animations;
    for (const auto animationIndex : collectArrayIndices(document, "animations")) {
        const std::string prefix = "animations." + std::to_string(animationIndex);
        GltfAnimationClipData clip{};
        clip.animationIndex = animationIndex;
        clip.name = document.stringOr(prefix + ".name", "");
        std::unordered_set<std::string> channelTargets;

        for (const auto channelIndex : collectArrayIndices(document, prefix + ".channels")) {
            const std::string channelPrefix = prefix + ".channels." + std::to_string(channelIndex);
            const auto samplerIndex = sizeValue(document, channelPrefix + ".sampler");
            const auto targetNodeIndex = sizeValue(document, channelPrefix + ".target.node");
            const auto targetPathText = document.stringValue(channelPrefix + ".target.path");
            if (!samplerIndex.has_value() || !targetNodeIndex.has_value() || !targetPathText.has_value()) {
                errors.push_back("animation " + std::to_string(animationIndex) + " channel " + std::to_string(channelIndex) + " is missing sampler, target node, or target path");
                continue;
            }
            if (*targetNodeIndex >= nodes.size()) {
                errors.push_back("animation " + std::to_string(animationIndex) + " channel " + std::to_string(channelIndex) + " targets missing node " + std::to_string(*targetNodeIndex));
                continue;
            }
            const auto path = animationTargetPath(*targetPathText);
            if (!path.has_value()) {
                errors.push_back("animation " + std::to_string(animationIndex) + " channel " + std::to_string(channelIndex) + " has unsupported target path '" + *targetPathText + "'");
                continue;
            }

            const std::string samplerPrefix = prefix + ".samplers." + std::to_string(*samplerIndex);
            const auto inputAccessor = sizeValue(document, samplerPrefix + ".input");
            const auto outputAccessor = sizeValue(document, samplerPrefix + ".output");
            const auto interpolationText = document.stringOr(samplerPrefix + ".interpolation", "LINEAR");
            const auto interpolation = animationInterpolation(interpolationText);
            if (!inputAccessor.has_value() || !outputAccessor.has_value()) {
                errors.push_back("animation " + std::to_string(animationIndex) + " channel " + std::to_string(channelIndex) + " references missing sampler " + std::to_string(*samplerIndex));
                continue;
            }
            if (!interpolation.has_value()) {
                errors.push_back("animation " + std::to_string(animationIndex) + " sampler " + std::to_string(*samplerIndex) + " has unsupported interpolation '" + interpolationText + "'");
                continue;
            }

            std::vector<std::array<float, 4>> inputValues;
            if (!readFloatComponents(
                    document,
                    buffers,
                    *inputAccessor,
                    "SCALAR",
                    inputValues,
                    errors,
                    "animation " + std::to_string(animationIndex) + " sampler input")) {
                continue;
            }
            if (inputValues.empty()) {
                errors.push_back("animation " + std::to_string(animationIndex) + " sampler " + std::to_string(*samplerIndex) + " has no keyframe times");
                continue;
            }
            bool validTimes = true;
            for (std::size_t keyIndex = 0; keyIndex < inputValues.size(); ++keyIndex) {
                const float time = inputValues[keyIndex][0];
                if (time < 0.0F || (keyIndex > 0 && time <= inputValues[keyIndex - 1][0])) {
                    errors.push_back("animation " + std::to_string(animationIndex) + " sampler " + std::to_string(*samplerIndex) + " keyframe times must be non-negative and strictly increasing");
                    validTimes = false;
                    break;
                }
            }
            if (!validTimes) {
                continue;
            }

            const bool isRotation = *path == GltfAnimationTargetPath::Rotation;
            std::vector<std::array<float, 4>> outputValues;
            if (!readFloatComponents(
                    document,
                    buffers,
                    *outputAccessor,
                    isRotation ? "VEC4" : "VEC3",
                    outputValues,
                    errors,
                    "animation " + std::to_string(animationIndex) + " sampler output")) {
                continue;
            }
            const std::size_t valuesPerKey = *interpolation == GltfAnimationInterpolation::CubicSpline ? 3U : 1U;
            if (outputValues.size() != inputValues.size() * valuesPerKey) {
                errors.push_back(
                    "animation " + std::to_string(animationIndex) + " sampler " + std::to_string(*samplerIndex) +
                    " output count " + std::to_string(outputValues.size()) + " does not match " +
                    std::to_string(inputValues.size()) + " input keyframes");
                continue;
            }

            const std::string targetKey = std::to_string(*targetNodeIndex) + ":" + *targetPathText;
            if (!channelTargets.insert(targetKey).second) {
                errors.push_back("animation " + std::to_string(animationIndex) + " has duplicate channels for node " + std::to_string(*targetNodeIndex) + " path " + *targetPathText);
                continue;
            }

            GltfAnimationChannelData channel{};
            channel.samplerIndex = *samplerIndex;
            channel.targetNodeIndex = *targetNodeIndex;
            channel.targetPath = *path;
            channel.interpolation = *interpolation;
            for (std::size_t keyIndex = 0; keyIndex < inputValues.size(); ++keyIndex) {
                const std::size_t valueIndex = keyIndex * valuesPerKey;
                const std::size_t mainIndex = valueIndex + (valuesPerKey == 3 ? 1U : 0U);
                if (isRotation) {
                    GltfQuatKeyframe key{};
                    key.timeSeconds = inputValues[keyIndex][0];
                    key.value = math::Quat{
                        outputValues[mainIndex][0], outputValues[mainIndex][1],
                        outputValues[mainIndex][2], outputValues[mainIndex][3],
                    };
                    if (!normalizeQuaternion(key.value)) {
                        errors.push_back("animation " + std::to_string(animationIndex) + " contains a zero-length rotation keyframe");
                        channel.rotationKeyframes.clear();
                        break;
                    }
                    if (valuesPerKey == 3) {
                        key.inTangent = math::Quat{
                            outputValues[valueIndex][0], outputValues[valueIndex][1],
                            outputValues[valueIndex][2], outputValues[valueIndex][3],
                        };
                        key.outTangent = math::Quat{
                            outputValues[valueIndex + 2][0], outputValues[valueIndex + 2][1],
                            outputValues[valueIndex + 2][2], outputValues[valueIndex + 2][3],
                        };
                    }
                    channel.rotationKeyframes.push_back(key);
                } else {
                    GltfVec3Keyframe key{};
                    key.timeSeconds = inputValues[keyIndex][0];
                    key.value = math::Vec3{
                        outputValues[mainIndex][0], outputValues[mainIndex][1], outputValues[mainIndex][2],
                    };
                    if (valuesPerKey == 3) {
                        key.inTangent = math::Vec3{
                            outputValues[valueIndex][0], outputValues[valueIndex][1], outputValues[valueIndex][2],
                        };
                        key.outTangent = math::Vec3{
                            outputValues[valueIndex + 2][0], outputValues[valueIndex + 2][1], outputValues[valueIndex + 2][2],
                        };
                    }
                    channel.vectorKeyframes.push_back(key);
                }
            }
            if ((isRotation && channel.rotationKeyframes.size() != inputValues.size()) ||
                (!isRotation && channel.vectorKeyframes.size() != inputValues.size())) {
                continue;
            }
            clip.durationSeconds = std::max(clip.durationSeconds, inputValues.back()[0]);
            clip.channels.push_back(std::move(channel));
        }
        if (clip.channels.empty()) {
            errors.push_back("animation " + std::to_string(animationIndex) + " has no valid channels");
        }
        animations.push_back(std::move(clip));
    }
    return animations;
}

} // namespace

std::size_t GltfMeshData::vertexCount() const {
    std::size_t total = 0;
    for (const auto& primitive : primitives) {
        total += primitive.positions.size();
    }
    return total;
}

std::size_t GltfMeshData::indexCount() const {
    std::size_t total = 0;
    for (const auto& primitive : primitives) {
        total += primitive.indices.size();
    }
    return total;
}

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
    core::ConfigDocument document;
    const auto parseResult = parseDocument(json, document);
    if (!parseResult.ok()) {
        return parseResult;
    }

    GltfPayload payload{};
    payload.container = GltfContainerKind::Text;
    payload.json = json;
    return fillSceneInfoFromPayload(payload, document, outInfo);
}

GltfSceneInfoLoadResult loadGltfSceneInfo(
    const std::filesystem::path& path,
    GltfSceneInfo& outInfo) {
    GltfPayload payload;
    const auto payloadResult = loadPayload(path, payload);
    if (!payloadResult.ok()) {
        return payloadResult;
    }

    core::ConfigDocument document;
    const auto parseResult = parseDocument(payload.json, document);
    if (!parseResult.ok()) {
        return parseResult;
    }

    return fillSceneInfoFromPayload(payload, document, outInfo);
}

GltfMeshDataLoadResult loadGltfMeshData(
    const std::filesystem::path& path,
    GltfMeshData& outMeshData) {
    GltfPayload payload;
    const auto payloadResult = loadPayload(path, payload);
    if (!payloadResult.ok()) {
        return toMeshResult(payloadResult);
    }
    core::ConfigDocument document;
    const auto parseResult = parseDocument(payload.json, document);
    if (!parseResult.ok()) {
        return toMeshResult(parseResult);
    }

    GltfSceneInfo sceneInfo;
    const auto sceneInfoResult = fillSceneInfoFromPayload(payload, document, sceneInfo);
    if (!sceneInfoResult.ok()) {
        return toMeshResult(sceneInfoResult);
    }

    GltfMeshData meshData{};
    meshData.path = path;
    meshData.sceneInfo = std::move(sceneInfo);
    meshData.materials = readMaterials(document);
    std::vector<std::string> errors;
    GltfBuffers buffers;
    (void)loadBuffers(payload, document, buffers, errors);
    const auto nodes = readNodes(document, errors);
    validateNodeHierarchy(nodes, errors);
    meshData.nodes = publicNodes(nodes);
    const auto skins = readSkins(document, buffers, nodes, errors);
    const auto transformsByMesh = meshNodeTransforms(document, nodes, meshData.nodeMarkers, errors);

    std::unordered_map<std::size_t, const GltfSkinData*> skinsByIndex;
    for (const auto& skin : skins) {
        skinsByIndex.insert_or_assign(skin.skinIndex, &skin);
    }
    const auto meshIndices = collectArrayIndices(document, "meshes");
    const std::unordered_set<std::size_t> validMeshIndices(meshIndices.begin(), meshIndices.end());
    for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
        const auto& node = nodes[nodeIndex];
        if (node.meshIndex.has_value() && !validMeshIndices.contains(*node.meshIndex)) {
            errors.push_back("node " + std::to_string(nodeIndex) + " references missing mesh " + std::to_string(*node.meshIndex));
        }
        if (node.skinIndex.has_value() && !skinsByIndex.contains(*node.skinIndex)) {
            errors.push_back("node " + std::to_string(nodeIndex) + " references missing skin " + std::to_string(*node.skinIndex));
        }
    }

    for (const auto meshIndex : meshIndices) {
        const std::string primitivePrefix = "meshes." + std::to_string(meshIndex) + ".primitives";
        for (const auto primitiveIndex : collectArrayIndices(document, primitivePrefix)) {
            const std::string base = primitivePrefix + "." + std::to_string(primitiveIndex);
            const auto positionAccessor = sizeValue(document, base + ".attributes.POSITION");
            if (!positionAccessor.has_value()) {
                errors.push_back("primitive " + std::to_string(meshIndex) + "." +
                    std::to_string(primitiveIndex) + " is missing POSITION");
                continue;
            }

            GltfPrimitiveData primitive{};
            primitive.meshIndex = meshIndex;
            primitive.primitiveIndex = primitiveIndex;
            primitive.materialIndex = document.intOr(base + ".material", -1);

            if (!readFloatVectors(
                    document,
                    buffers,
                    *positionAccessor,
                    "VEC3",
                    primitive.positions,
                    errors,
                    "POSITION accessor")) {
                continue;
            }

            if (const auto normalAccessor = sizeValue(document, base + ".attributes.NORMAL");
                normalAccessor.has_value()) {
                (void)readFloatVectors(
                    document,
                    buffers,
                    *normalAccessor,
                    "VEC3",
                    primitive.normals,
                    errors,
                    "NORMAL accessor");
            }

            if (const auto texcoordAccessor = sizeValue(document, base + ".attributes.TEXCOORD_0");
                texcoordAccessor.has_value()) {
                (void)readTexcoords(document, buffers, *texcoordAccessor, primitive.texcoords, errors);
            }

            const auto jointsAccessor = sizeValue(document, base + ".attributes.JOINTS_0");
            const auto weightsAccessor = sizeValue(document, base + ".attributes.WEIGHTS_0");
            if (jointsAccessor.has_value() != weightsAccessor.has_value()) {
                errors.push_back(
                    "primitive " + std::to_string(meshIndex) + "." + std::to_string(primitiveIndex) +
                    " must provide JOINTS_0 and WEIGHTS_0 together");
                continue;
            }
            if (jointsAccessor.has_value()) {
                if (!readJoints(document, buffers, *jointsAccessor, primitive.joints, errors) ||
                    !readWeights(document, buffers, *weightsAccessor, primitive.weights, errors)) {
                    continue;
                }
            }

            if (const auto indexAccessor = sizeValue(document, base + ".indices");
                indexAccessor.has_value()) {
                if (!readIndices(document, buffers, *indexAccessor, primitive.indices, errors)) {
                    continue;
                }
            } else {
                primitive.indices.reserve(primitive.positions.size());
                for (std::size_t vertexIndex = 0; vertexIndex < primitive.positions.size(); ++vertexIndex) {
                    primitive.indices.push_back(static_cast<std::uint32_t>(vertexIndex));
                }
            }

            if ((!primitive.normals.empty() && primitive.normals.size() != primitive.positions.size()) ||
                (!primitive.texcoords.empty() && primitive.texcoords.size() != primitive.positions.size()) ||
                (!primitive.joints.empty() && primitive.joints.size() != primitive.positions.size()) ||
                (!primitive.weights.empty() && primitive.weights.size() != primitive.positions.size())) {
                errors.push_back(
                    "primitive " + std::to_string(meshIndex) + "." + std::to_string(primitiveIndex) +
                    " vertex attribute counts do not match POSITION count");
                continue;
            }
            if (std::ranges::any_of(primitive.indices, [&](std::uint32_t index) {
                    return index >= primitive.positions.size();
                })) {
                errors.push_back(
                    "primitive " + std::to_string(meshIndex) + "." + std::to_string(primitiveIndex) +
                    " has an index outside its POSITION accessor");
                continue;
            }

            const auto transformIt = transformsByMesh.find(meshIndex);
            if (transformIt == transformsByMesh.end() || transformIt->second.empty()) {
                meshData.primitives.push_back(std::move(primitive));
                continue;
            }

            for (const auto& instance : transformIt->second) {
                auto transformedPrimitive = primitive;
                transformedPrimitive.nodeIndex = static_cast<int>(instance.nodeIndex);
                transformedPrimitive.skinIndex = instance.skinIndex;
                if (instance.skinIndex >= 0) {
                    const auto skinIt = skinsByIndex.find(static_cast<std::size_t>(instance.skinIndex));
                    if (skinIt == skinsByIndex.end()) {
                        continue;
                    }
                    if (transformedPrimitive.joints.empty()) {
                        errors.push_back(
                            "node " + std::to_string(instance.nodeIndex) + " uses skin " +
                            std::to_string(instance.skinIndex) + " but mesh primitive has no JOINTS_0/WEIGHTS_0");
                        continue;
                    }
                    const auto jointCount = skinIt->second->joints.size();
                    bool validJointIndices = true;
                    for (const auto& vertexJoints : transformedPrimitive.joints) {
                        for (const auto jointIndex : vertexJoints) {
                            if (jointIndex >= jointCount) {
                                errors.push_back(
                                    "primitive " + std::to_string(meshIndex) + "." + std::to_string(primitiveIndex) +
                                    " JOINTS_0 index " + std::to_string(jointIndex) + " exceeds skin " +
                                    std::to_string(instance.skinIndex) + " joint count " + std::to_string(jointCount));
                                validJointIndices = false;
                                break;
                            }
                        }
                        if (!validJointIndices) {
                            break;
                        }
                    }
                    if (!validJointIndices) {
                        continue;
                    }
                } else {
                    applyNodeTransform(transformedPrimitive, instance.worldTransform);
                    transformedPrimitive.nodeTransformBaked = true;
                }
                meshData.primitives.push_back(std::move(transformedPrimitive));
            }
        }
    }

    if (!errors.empty()) {
        return GltfMeshDataLoadResult{std::move(errors)};
    }
    if (meshData.primitives.empty()) {
        return meshError("glTF mesh import produced no primitives: " + path.string());
    }

    if (!skins.empty() || meshData.sceneInfo.animationCount > 0U) {
        auto animationData = std::make_shared<GltfAnimationData>();
        animationData->path = path;
        animationData->sceneInfo = meshData.sceneInfo;
        animationData->nodes = publicNodes(nodes);
        animationData->skins = skins;
        animationData->animations = readAnimations(document, buffers, nodes, errors);
        if (!errors.empty()) {
            return GltfMeshDataLoadResult{std::move(errors)};
        }
        meshData.animationData = std::move(animationData);
    }

    outMeshData = std::move(meshData);
    return {};
}

GltfAnimationDataLoadResult loadGltfAnimationData(
    const std::filesystem::path& path,
    GltfAnimationData& outAnimationData) {
    GltfPayload payload;
    const auto payloadResult = loadPayload(path, payload);
    if (!payloadResult.ok()) {
        return GltfAnimationDataLoadResult{payloadResult.errors};
    }

    core::ConfigDocument document;
    const auto parseResult = parseDocument(payload.json, document);
    if (!parseResult.ok()) {
        return GltfAnimationDataLoadResult{parseResult.errors};
    }

    GltfSceneInfo sceneInfo;
    const auto sceneInfoResult = fillSceneInfoFromPayload(payload, document, sceneInfo);
    if (!sceneInfoResult.ok()) {
        return GltfAnimationDataLoadResult{sceneInfoResult.errors};
    }

    std::vector<std::string> errors;
    GltfBuffers buffers;
    (void)loadBuffers(payload, document, buffers, errors);
    const auto nodes = readNodes(document, errors);
    validateNodeHierarchy(nodes, errors);
    auto skins = readSkins(document, buffers, nodes, errors);
    auto animations = readAnimations(document, buffers, nodes, errors);

    std::unordered_set<std::size_t> skinIndices;
    for (const auto& skin : skins) {
        skinIndices.insert(skin.skinIndex);
    }
    const auto meshIndices = collectArrayIndices(document, "meshes");
    const std::unordered_set<std::size_t> validMeshIndices(meshIndices.begin(), meshIndices.end());
    for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
        if (nodes[nodeIndex].meshIndex.has_value() && !validMeshIndices.contains(*nodes[nodeIndex].meshIndex)) {
            errors.push_back("node " + std::to_string(nodeIndex) + " references missing mesh " + std::to_string(*nodes[nodeIndex].meshIndex));
        }
        if (nodes[nodeIndex].skinIndex.has_value() && !skinIndices.contains(*nodes[nodeIndex].skinIndex)) {
            errors.push_back("node " + std::to_string(nodeIndex) + " references missing skin " + std::to_string(*nodes[nodeIndex].skinIndex));
        }
    }

    if (!errors.empty()) {
        return GltfAnimationDataLoadResult{std::move(errors)};
    }

    GltfAnimationData animationData{};
    animationData.path = path;
    animationData.sceneInfo = std::move(sceneInfo);
    animationData.nodes = publicNodes(nodes);
    animationData.skins = std::move(skins);
    animationData.animations = std::move(animations);
    outAnimationData = std::move(animationData);
    return {};
}

} // namespace novacore::assets
