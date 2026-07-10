#include "novacore/assets/GltfDocument.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "[fail] " << message << '\n';
    }
}

void appendU32(std::string& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<char>(value & 0xFFU));
    bytes.push_back(static_cast<char>((value >> 8U) & 0xFFU));
    bytes.push_back(static_cast<char>((value >> 16U) & 0xFFU));
    bytes.push_back(static_cast<char>((value >> 24U) & 0xFFU));
}

void appendF32(std::string& bytes, float value) {
    char raw[sizeof(float)]{};
    std::memcpy(raw, &value, sizeof(float));
    bytes.append(raw, sizeof(float));
}

void appendMatrix(std::string& bytes, float translationX) {
    for (std::size_t index = 0; index < 16; ++index) {
        appendF32(bytes, index == 0 || index == 5 || index == 10 || index == 15
                ? 1.0F
                : (index == 12 ? translationX : 0.0F));
    }
}

std::string makeAnimationBinary(std::uint8_t firstJoint = 0) {
    std::string bytes;

    appendF32(bytes, 0.0F);
    appendF32(bytes, 0.0F);
    appendF32(bytes, 0.0F);
    appendF32(bytes, 1.0F);
    appendF32(bytes, 0.0F);
    appendF32(bytes, 0.0F);
    appendF32(bytes, 0.0F);
    appendF32(bytes, 1.0F);
    appendF32(bytes, 0.0F);

    bytes.append({static_cast<char>(firstJoint), 1, 0, 0});
    bytes.append({0, 1, 0, 0});
    bytes.append({1, 0, 0, 0});

    for (std::size_t vertex = 0; vertex < 3; ++vertex) {
        appendF32(bytes, vertex == 2 ? 0.25F : 0.75F);
        appendF32(bytes, vertex == 2 ? 0.75F : 0.25F);
        appendF32(bytes, 0.0F);
        appendF32(bytes, 0.0F);
    }

    appendMatrix(bytes, 0.0F);
    appendMatrix(bytes, -1.0F);

    appendF32(bytes, 0.0F);
    appendF32(bytes, 1.0F);

    appendF32(bytes, 0.0F);
    appendF32(bytes, 0.0F);
    appendF32(bytes, 0.0F);
    appendF32(bytes, 0.0F);
    appendF32(bytes, 2.0F);
    appendF32(bytes, 0.0F);

    appendF32(bytes, 0.0F);
    appendF32(bytes, 0.0F);
    appendF32(bytes, 0.0F);
    appendF32(bytes, 1.0F);
    appendF32(bytes, 0.0F);
    appendF32(bytes, 0.70710677F);
    appendF32(bytes, 0.0F);
    appendF32(bytes, 0.70710677F);

    appendF32(bytes, 1.0F);
    appendF32(bytes, 1.0F);
    appendF32(bytes, 1.0F);
    appendF32(bytes, 2.0F);
    appendF32(bytes, 2.0F);
    appendF32(bytes, 2.0F);

    return bytes;
}

std::string makeAnimationJson(std::string_view bufferUri = {}) {
    const std::string uri = bufferUri.empty() ? "" : ", \"uri\": \"" + std::string(bufferUri) + "\"";
    return R"({
        "asset": { "version": "2.0" },
        "scene": 0,
        "scenes": [{ "nodes": [0] }],
        "nodes": [
            { "name": "root", "translation": [1.0, 2.0, 3.0], "children": [1] },
            { "name": "spine", "rotation": [0.0, 0.0, 0.0, 1.0], "children": [2] },
            { "name": "body", "mesh": 0, "skin": 0, "scale": [1.0, 2.0, 1.0] }
        ],
        "meshes": [{
            "primitives": [{
                "attributes": { "POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2 },
                "material": 0
            }]
        }],
        "materials": [{
            "name": "operator_blue",
            "pbrMetallicRoughness": {
                "baseColorFactor": [0.12, 0.34, 0.78, 0.9],
                "metallicFactor": 0.4,
                "roughnessFactor": 0.62,
                "baseColorTexture": { "index": 2 }
            },
            "emissiveFactor": [0.01, 0.04, 0.08],
            "doubleSided": true
        }],
        "skins": [{
            "name": "humanoid",
            "skeleton": 0,
            "joints": [0, 1],
            "inverseBindMatrices": 3
        }],
        "animations": [{
            "name": "move",
            "samplers": [
                { "input": 4, "output": 5, "interpolation": "LINEAR" },
                { "input": 4, "output": 6, "interpolation": "STEP" },
                { "input": 4, "output": 7 }
            ],
            "channels": [
                { "sampler": 0, "target": { "node": 0, "path": "translation" } },
                { "sampler": 1, "target": { "node": 1, "path": "rotation" } },
                { "sampler": 2, "target": { "node": 2, "path": "scale" } }
            ]
        }],
        "buffers": [{ "byteLength": 312)" + uri + R"( }],
        "bufferViews": [
            { "buffer": 0, "byteOffset": 0, "byteLength": 36 },
            { "buffer": 0, "byteOffset": 36, "byteLength": 12 },
            { "buffer": 0, "byteOffset": 48, "byteLength": 48 },
            { "buffer": 0, "byteOffset": 96, "byteLength": 128 },
            { "buffer": 0, "byteOffset": 224, "byteLength": 8 },
            { "buffer": 0, "byteOffset": 232, "byteLength": 24 },
            { "buffer": 0, "byteOffset": 256, "byteLength": 32 },
            { "buffer": 0, "byteOffset": 288, "byteLength": 24 }
        ],
        "accessors": [
            { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3" },
            { "bufferView": 1, "componentType": 5121, "count": 3, "type": "VEC4" },
            { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4" },
            { "bufferView": 3, "componentType": 5126, "count": 2, "type": "MAT4" },
            { "bufferView": 4, "componentType": 5126, "count": 2, "type": "SCALAR" },
            { "bufferView": 5, "componentType": 5126, "count": 2, "type": "VEC3" },
            { "bufferView": 6, "componentType": 5126, "count": 2, "type": "VEC4" },
            { "bufferView": 7, "componentType": 5126, "count": 2, "type": "VEC3" }
        ]
    })";
}

void appendChunk(std::string& chunks, std::string chunk, std::uint32_t type, char padding) {
    while (chunk.size() % 4U != 0U) {
        chunk.push_back(padding);
    }
    appendU32(chunks, static_cast<std::uint32_t>(chunk.size()));
    appendU32(chunks, type);
    chunks += chunk;
}

void writeGlb(const std::filesystem::path& path, const std::string& json, const std::string& binary) {
    std::string chunks;
    appendChunk(chunks, json, 0x4E4F534AU, ' ');
    appendChunk(chunks, binary, 0x004E4942U, '\0');

    std::string glb;
    appendU32(glb, 0x46546C67U);
    appendU32(glb, 2);
    appendU32(glb, static_cast<std::uint32_t>(12U + chunks.size()));
    glb += chunks;

    std::ofstream file(path, std::ios::binary);
    file.write(glb.data(), static_cast<std::streamsize>(glb.size()));
}

bool containsError(const std::vector<std::string>& errors, std::string_view text) {
    for (const auto& error : errors) {
        if (error.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void testGlbSkeletonAnimationAndSkinningAttributes(const std::filesystem::path& directory) {
    const auto path = directory / "animated.glb";
    writeGlb(path, makeAnimationJson(), makeAnimationBinary());

    novacore::assets::GltfAnimationData animationData;
    const auto animationResult = novacore::assets::loadGltfAnimationData(path, animationData);
    expect(animationResult.ok(), "GLB skeleton and animation import succeeds");
    expect(animationData.nodes.size() == 3, "all nodes are imported");
    if (animationData.nodes.size() == 3) {
        expect(animationData.nodes[1].parentNodeIndex == 0, "node parent hierarchy is imported");
        expect(animationData.nodes[2].parentNodeIndex == 1, "nested node parent hierarchy is imported");
        expect(animationData.nodes[0].localTransform.translation.x == 1.0F, "node translation is imported");
        expect(animationData.nodes[2].localTransform.scale.y == 2.0F, "node scale is imported");
        expect(animationData.nodes[2].meshIndex == 0 && animationData.nodes[2].skinIndex == 0,
            "mesh node keeps its skin mapping");
    }
    expect(animationData.skins.size() == 1, "skin is imported");
    if (!animationData.skins.empty()) {
        const auto& skin = animationData.skins[0];
        expect(skin.skeletonRootNodeIndex == 0, "skin skeleton root is imported");
        expect(skin.joints.size() == 2, "skin joints are imported");
        if (skin.joints.size() == 2) {
            expect(skin.joints[1].parentJointIndex == 0, "joint parent is resolved");
            expect(skin.joints[1].inverseBindMatrix.values[12] == -1.0F,
                "inverse bind matrix remains glTF column-major");
        }
    }
    expect(animationData.animations.size() == 1, "animation clip is imported");
    if (!animationData.animations.empty()) {
        const auto& clip = animationData.animations[0];
        expect(clip.channels.size() == 3, "translation, rotation, and scale channels are imported");
        expect(std::abs(clip.durationSeconds - 1.0F) < 0.0001F, "animation duration is computed");
        if (clip.channels.size() == 3) {
            expect(clip.channels[0].vectorKeyframes[1].value.y == 2.0F, "translation keyframes are decoded");
            expect(clip.channels[1].rotationKeyframes.size() == 2, "rotation keyframes are decoded");
            expect(clip.channels[1].interpolation == novacore::assets::GltfAnimationInterpolation::Step,
                "sampler interpolation is imported");
            expect(clip.channels[2].vectorKeyframes[1].value.x == 2.0F, "scale keyframes are decoded");
        }
    }

    novacore::assets::GltfMeshData meshData;
    const auto meshResult = novacore::assets::loadGltfMeshData(path, meshData);
    expect(meshResult.ok(), "skinned GLB mesh import succeeds");
    expect(meshData.nodes.size() == 3, "mesh data exposes complete node hierarchy");
    expect(meshData.materials.size() == 1, "mesh data imports material definitions");
    if (!meshData.materials.empty()) {
        const auto& material = meshData.materials.front();
        expect(material.name == "operator_blue", "material name is imported");
        expect(std::abs(material.baseColorFactor[2] - 0.78F) < 0.0001F,
            "material base-color factor is imported");
        expect(material.baseColorTextureIndex == 2, "material texture binding index is imported");
        expect(material.doubleSided, "material double-sided flag is imported");
    }
    expect(meshData.primitives.size() == 1, "skinned mesh instance is imported");
    if (!meshData.primitives.empty()) {
        const auto& primitive = meshData.primitives[0];
        expect(primitive.nodeIndex == 2 && primitive.skinIndex == 0, "primitive keeps mesh-node to skin mapping");
        expect(!primitive.nodeTransformBaked, "skinned primitive remains in mesh-local space");
        expect(primitive.joints.size() == 3 && primitive.weights.size() == 3,
            "JOINTS_0 and WEIGHTS_0 are imported per vertex");
        if (!primitive.weights.empty()) {
            const float sum = primitive.weights[0][0] + primitive.weights[0][1] +
                primitive.weights[0][2] + primitive.weights[0][3];
            expect(std::abs(sum - 1.0F) < 0.0001F, "vertex weights are normalized");
        }
    }
}

void testTextGltfExternalBuffer(const std::filesystem::path& directory) {
    const auto gltfPath = directory / "animated_external.gltf";
    const auto bufferPath = directory / "animation data.bin";
    {
        const auto binary = makeAnimationBinary();
        std::ofstream file(bufferPath, std::ios::binary);
        file.write(binary.data(), static_cast<std::streamsize>(binary.size()));
    }
    {
        std::ofstream file(gltfPath);
        file << makeAnimationJson("animation%20data.bin");
    }

    novacore::assets::GltfAnimationData animationData;
    const auto result = novacore::assets::loadGltfAnimationData(gltfPath, animationData);
    expect(result.ok(), "text glTF resolves and imports its external buffer");
    expect(animationData.sceneInfo.container == novacore::assets::GltfContainerKind::Text,
        "text glTF keeps its container kind");
    expect(!animationData.animations.empty() && animationData.animations[0].channels.size() == 3,
        "text glTF imports animation channels");

    novacore::assets::GltfMeshData meshData;
    expect(novacore::assets::loadGltfMeshData(gltfPath, meshData).ok(),
        "text glTF mesh attributes import from external buffer");
}

void testDiagnostics(const std::filesystem::path& directory) {
    const auto badJointPath = directory / "bad_joint.glb";
    writeGlb(badJointPath, makeAnimationJson(), makeAnimationBinary(2));
    novacore::assets::GltfMeshData meshData;
    const auto badJointResult = novacore::assets::loadGltfMeshData(badJointPath, meshData);
    expect(!badJointResult.ok(), "out-of-range JOINTS_0 index is rejected");
    expect(containsError(badJointResult.errors, "exceeds skin 0 joint count 2"),
        "JOINTS_0 diagnostic identifies skin and joint count");

    const auto badReferencesPath = directory / "bad_references.gltf";
    {
        std::ofstream file(badReferencesPath);
        file << R"({
            "asset": { "version": "2.0" },
            "nodes": [{ "name": "root" }],
            "skins": [{ "joints": [9] }],
            "animations": [{
                "samplers": [{ "input": 0, "output": 1 }],
                "channels": [{ "sampler": 0, "target": { "node": 8, "path": "translation" } }]
            }]
        })";
    }
    novacore::assets::GltfAnimationData animationData;
    const auto badReferencesResult = novacore::assets::loadGltfAnimationData(badReferencesPath, animationData);
    expect(!badReferencesResult.ok(), "invalid skin and animation node references are rejected");
    expect(containsError(badReferencesResult.errors, "missing joint node 9"),
        "skin diagnostic identifies missing joint node");
    expect(containsError(badReferencesResult.errors, "targets missing node 8"),
        "animation diagnostic identifies missing target node");
}

} // namespace

int main() {
    const auto directory = std::filesystem::temp_directory_path() / "novacore_gltf_animation_import_tests";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    testGlbSkeletonAnimationAndSkinningAttributes(directory);
    testTextGltfExternalBuffer(directory);
    testDiagnostics(directory);

    std::filesystem::remove_all(directory);
    if (failures == 0) {
        std::cout << "gltf animation import tests passed\n";
        return 0;
    }
    std::cerr << failures << " gltf animation import test(s) failed\n";
    return 1;
}
