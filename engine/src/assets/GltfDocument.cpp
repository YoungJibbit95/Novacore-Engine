#include "novacore/assets/GltfDocument.hpp"

#include "novacore/io/FileSystem.hpp"

#include <algorithm>
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
    std::optional<std::size_t> meshIndex;
    std::vector<std::size_t> children;
    GltfMat4 localTransform{};
};

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

[[nodiscard]] std::vector<GltfNodeDesc> readNodes(const core::ConfigDocument& document) {
    const auto nodeIndices = collectArrayIndices(document, "nodes");
    std::vector<GltfNodeDesc> nodes;
    if (nodeIndices.empty()) {
        return nodes;
    }

    const auto maxNodeIndex = *std::ranges::max_element(nodeIndices);
    nodes.resize(maxNodeIndex + 1U);

    for (const auto nodeIndex : nodeIndices) {
        const std::string prefix = "nodes." + std::to_string(nodeIndex);
        if (const auto meshIndex = sizeValue(document, prefix + ".mesh"); meshIndex.has_value()) {
            nodes[nodeIndex].meshIndex = *meshIndex;
        }
        for (const auto childArrayIndex : collectArrayIndices(document, prefix + ".children")) {
            if (const auto childNodeIndex = sizeValue(document, prefix + ".children." + std::to_string(childArrayIndex));
                childNodeIndex.has_value()) {
                nodes[nodeIndex].children.push_back(*childNodeIndex);
            }
        }
        nodes[nodeIndex].localTransform = readNodeTransform(document, prefix);
    }

    return nodes;
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
    std::unordered_map<std::size_t, std::vector<GltfMat4>>& meshTransforms,
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
    if (node.meshIndex.has_value()) {
        meshTransforms[*node.meshIndex].push_back(worldTransform);
    }

    for (const auto childIndex : node.children) {
        collectMeshNodeTransforms(nodes, childIndex, worldTransform, activeStack, meshTransforms, errors);
    }
    activeStack[nodeIndex] = false;
}

[[nodiscard]] std::unordered_map<std::size_t, std::vector<GltfMat4>> meshNodeTransforms(
    const core::ConfigDocument& document,
    std::vector<std::string>& errors) {
    std::unordered_map<std::size_t, std::vector<GltfMat4>> transforms;
    const auto nodes = readNodes(document);
    if (nodes.empty()) {
        return transforms;
    }

    std::vector<bool> activeStack(nodes.size(), false);
    for (const auto rootIndex : sceneRootNodes(document, nodes)) {
        collectMeshNodeTransforms(nodes, rootIndex, identityMatrix(), activeStack, transforms, errors);
    }
    return transforms;
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
    if (view.bufferIndex != 0) {
        errors.push_back(std::string(label) + " references unsupported non-zero buffer index");
        return std::nullopt;
    }

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
    std::string_view binary,
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
    std::string_view binary,
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
    std::string_view binary,
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
    if (payload.container != GltfContainerKind::BinaryGlb) {
        return meshError("CPU mesh import currently requires a binary GLB file: " + path.string());
    }
    if (payload.binary.empty()) {
        return meshError("GLB file has no binary buffer chunk: " + path.string());
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
    std::vector<std::string> errors;
    const auto transformsByMesh = meshNodeTransforms(document, errors);

    for (const auto meshIndex : collectArrayIndices(document, "meshes")) {
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
                    payload.binary,
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
                    payload.binary,
                    *normalAccessor,
                    "VEC3",
                    primitive.normals,
                    errors,
                    "NORMAL accessor");
            }

            if (const auto texcoordAccessor = sizeValue(document, base + ".attributes.TEXCOORD_0");
                texcoordAccessor.has_value()) {
                (void)readTexcoords(document, payload.binary, *texcoordAccessor, primitive.texcoords, errors);
            }

            if (const auto indexAccessor = sizeValue(document, base + ".indices");
                indexAccessor.has_value()) {
                if (!readIndices(document, payload.binary, *indexAccessor, primitive.indices, errors)) {
                    continue;
                }
            } else {
                primitive.indices.reserve(primitive.positions.size());
                for (std::size_t vertexIndex = 0; vertexIndex < primitive.positions.size(); ++vertexIndex) {
                    primitive.indices.push_back(static_cast<std::uint32_t>(vertexIndex));
                }
            }

            const auto transformIt = transformsByMesh.find(meshIndex);
            if (transformIt == transformsByMesh.end() || transformIt->second.empty()) {
                meshData.primitives.push_back(std::move(primitive));
                continue;
            }

            for (const auto& transform : transformIt->second) {
                auto transformedPrimitive = primitive;
                applyNodeTransform(transformedPrimitive, transform);
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

    outMeshData = std::move(meshData);
    return {};
}

} // namespace novacore::assets
