#include "../engine/src/assets/ImageDecoder.hpp"
#include "../engine/src/render/MaterialTextureMath.hpp"
#include "novacore/assets/GltfAnimationBridge.hpp"
#include "novacore/assets/GltfDocument.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>

namespace {
namespace fs = std::filesystem;
namespace assets = novacore::assets;
int failures = 0;
void expect(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "[fail] " << message << '\n';
    }
}
std::vector<std::uint8_t> bytes(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
void write(const fs::path& path, std::span<const std::uint8_t> data) {
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}
void append(std::vector<std::uint8_t>& target, const auto& value) {
    const auto* begin = reinterpret_cast<const std::uint8_t*>(&value);
    target.insert(target.end(), begin, begin + sizeof(value));
}
void replace(std::string& text, std::string_view from, std::string_view to) {
    const auto at = text.find(from);
    expect(at != std::string::npos, "fixture mutation locates its intended field");
    if (at != std::string::npos) {
        text.replace(at, from.size(), to);
    }
}
void writeJson(const fs::path& path, const std::string& json) { std::ofstream(path) << json; }

void testDecode(const std::vector<std::uint8_t>& png, const std::vector<std::uint8_t>& jpeg) {
    const auto decoded = assets::detail::decodeGltfImage(png, "image/png");
    const std::vector<std::uint8_t> expected{255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 64, 255, 255, 255, 0};
    expect(decoded.ok() && decoded.width == 2 && decoded.height == 2 && decoded.pixels == expected,
           "PNG RGBA colors, straight alpha and top-left row order survive without a gamma/flip/premultiply repair");
    const auto lossy = assets::detail::decodeGltfImage(jpeg, "image/jpeg");
    expect(lossy.ok() && lossy.width == 2 && lossy.height == 2, "JPEG uses the maintained decoder");
    if (lossy.ok()) {
        expect(std::abs(int(lossy.pixels[0]) - 80) <= 3 && std::abs(int(lossy.pixels[1]) - 120) <= 3 &&
                   std::abs(int(lossy.pixels[2]) - 160) <= 3 && lossy.pixels[3] == 255,
               "JPEG channels preserve authored values within compression tolerance");
    }
    expect(!assets::detail::decodeGltfImage(png, "image/jpeg").ok(), "MIME/signature mismatch is rejected");
    expect(!assets::detail::decodeGltfImage({}, "image/png").ok(), "empty image input is rejected");
    const std::array<std::uint8_t, 6> gif{'G', 'I', 'F', '8', '9', 'a'};
    expect(!assets::detail::decodeGltfImage(gif, "image/gif").ok(), "unsupported formats are explicitly rejected");
    auto oversized = png;
    oversized[16] = 0;
    oversized[17] = 0;
    oversized[18] = 64;
    oversized[19] = 0;
    expect(!assets::detail::decodeGltfImage(oversized, "image/png").ok(),
           "oversized dimensions are rejected before allocation");
    expect(!assets::detail::decodeGltfImage(std::span(png).first(16), "image/png").ok(),
           "truncated PNG cannot produce partial pixels");
}

void testMaterialUniformsAndBudget(const std::vector<std::uint8_t>& png) {
    namespace detail = novacore::render::detail;
    expect(!novacore::render::RenderFrameInfo{}.materialTexturesEnabled, "material texture upload remains opt-in");
    assets::GltfMaterialData source{};
    source.baseColorTextureIndex = 0;
    source.baseColorTexCoord = 1;
    source.normalScale = 0.6F;
    source.emissiveFactor = {0.1F, 0.2F, 0.3F};
    source.emissiveStrength = 2;
    const auto uniforms = detail::materialTextureUniforms(source);
    expect(uniforms && uniforms->uvSets[0] == 1 && uniforms->uvSets[1] == 0 &&
               std::abs(uniforms->emissiveNormalScale[1] - 0.4F) < 0.00001F &&
               uniforms->emissiveNormalScale[3] == 0.6F && uniforms->mapEnabled[0] == 0,
           "GPU metadata preserves selected UV, emissive strength and normal scale without claiming a map is bound");
    source.alphaMode = "BLEND";
    expect(!detail::materialTextureUniforms(source), "BLEND requires explicit fallback in this opaque slice");
    source.alphaMode = "MASK";
    expect(!detail::materialTextureUniforms(source), "MASK requires alpha-aware depth/caster follow-up");
    source.alphaMode = "OPAQUE";
    source.textureTransforms[0].enabled = true;
    expect(!detail::materialTextureUniforms(source), "retained texture transforms require explicit renderer fallback");
    source.textureTransforms[0].enabled = false;
    source.normalScale = std::numeric_limits<float>::infinity();
    expect(!detail::materialTextureUniforms(source), "invalid synthetic material data cannot enter a UBO");
    detail::MaterialImageBudget budget{100};
    expect(budget.reserve(60) && !budget.reserve(50) && budget.used == 60 && budget.rejected == 1,
           "image budget rejects an allocation before exceeding the real resident byte limit");
    expect(budget.release(60) && budget.used == 0 && budget.reserve(100),
           "failed upload/deferred retirement rollback makes reserved capacity available again");
    expect(!budget.release(101) && budget.used == 100, "invalid retirement cannot underflow the resident byte counter");
    expect(budget.release(100) && !budget.reserve(std::numeric_limits<std::uint64_t>::max()) && budget.used == 0,
           "allocation-size overflow is rejected without corrupting the budget");
    auto first = std::make_shared<const std::vector<std::uint8_t>>(png);
    auto second = std::make_shared<const std::vector<std::uint8_t>>(png);
    auto changed = png;
    changed.back() ^= 1;
    auto third = std::make_shared<const std::vector<std::uint8_t>>(changed);
    expect(first != second && detail::sameImageContent(first, second) &&
               detail::imageContentHash(*first) == detail::imageContentHash(*second) &&
               !detail::sameImageContent(first, third),
           "distinct body/arm payload objects deduplicate by actual content, with byte equality after the hash");
}

std::string fixture(std::string_view image, std::size_t binarySize, std::size_t pngSize, bool externalBuffer = true) {
    std::ostringstream out;
    out << R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":)" << binarySize;
    if (externalBuffer) {
        out << R"(,"uri":"geometry.bin")";
    }
    out << R"(}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},
      {"buffer":0,"byteOffset":36,"byteLength":24},
      {"buffer":0,"byteOffset":60,"byteLength":12},
      {"buffer":0,"byteOffset":72,"byteLength":6},
      {"buffer":0,"byteOffset":80,"byteLength":)"
        << pngSize << R"(}],
      "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
      {"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"},
      {"bufferView":2,"componentType":5123,"normalized":true,"count":3,"type":"VEC2"},
      {"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],
      "meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1,"TEXCOORD_1":2},"indices":3,"material":0}]}],
      "images":[)"
        << image << R"(],"textures":[{"source":0,"sampler":0}],
      "samplers":[{"magFilter":9728,"minFilter":9987,"wrapS":33071,"wrapT":33648}],
      "materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0,"texCoord":1},
      "metallicRoughnessTexture":{"index":0,"texCoord":0}},
      "normalTexture":{"index":0,"texCoord":1,"scale":0.6},"emissiveTexture":{"index":0,"texCoord":1},
      "extensions":{"KHR_materials_emissive_strength":{"emissiveStrength":2}},"alphaMode":"OPAQUE"}]})";
    return out.str();
}

void testImport(const fs::path& directory, const std::vector<std::uint8_t>& png) {
    std::vector<std::uint8_t> binary;
    for (const float value :
         std::array{0.F, 0.F, 0.F, 1.F, 0.F, 0.F, 0.F, 1.F, 0.F, .125F, .25F, .75F, .125F, .25F, .875F}) {
        append(binary, value);
    }
    for (const std::uint16_t value : {0, 0, 65535, 0, 0, 65535, 0, 1, 2}) {
        append(binary, value);
    }
    binary.resize(80);
    binary.insert(binary.end(), png.begin(), png.end());
    write(directory / "geometry.bin", binary);
    write(directory / "checker.png", png);
    auto json = fixture(R"({"uri":"checker.png","mimeType":"image/png"})", binary.size(), png.size());
    const auto path = directory / "fixture.gltf";
    writeJson(path, json);
    assets::GltfMeshData mesh;
    auto result = assets::loadGltfMeshData(path, mesh);
    if (!result.ok()) {
        for (const auto& error : result.errors) {
            std::cerr << error << '\n';
        }
    }
    expect(result.ok(), "external PNG and normalized UV1 form a complete valid glTF material fixture");
    if (result.ok()) {
        expect(mesh.images.size() == 1 && mesh.images[0].encodedBytes && *mesh.images[0].encodedBytes == png,
               "external encoded image survives the generic CPU asset boundary without decoding");
        expect(mesh.textures[0].imageIndex == 0 && mesh.textures[0].samplerIndex == 0 &&
                   mesh.samplers[0].wrapS == 33071 && mesh.samplers[0].wrapT == 33648 &&
                   mesh.samplers[0].minFilter == 9987,
               "image/texture/sampler linkage and authored filter/wrap are retained");
        const auto& material = mesh.materials[0];
        expect(material.baseColorTexCoord == 1 && material.metallicRoughnessTexCoord == 0 &&
                   material.normalTexCoord == 1 && material.emissiveTexCoord == 1 &&
                   std::abs(material.normalScale - .6F) < .00001F && material.emissiveStrength == 2,
               "independent texture UV selection, normal scale and emissive strength survive import");
        const auto& primitive = mesh.primitives[0];
        expect(primitive.texcoords[1].x == .75F && primitive.texcoords1[1].x == 1 && primitive.texcoords1[1].y == 0,
               "UV1 is distinct from UV0 and normalized integer values decode correctly");
        auto subset = mesh;
        subset.primitives[0].positions[0].x += 1;
        expect(subset.images[0].encodedBytes == mesh.images[0].encodedBytes &&
                   subset.primitives[0].texcoords1.size() == 3,
               "mesh subsets share immutable image payloads and retain the secondary UV set");
        auto skinSource = mesh;
        auto& skinPrimitive = skinSource.primitives[0];
        skinPrimitive.skinIndex = 0;
        skinPrimitive.normals.assign(3, {0, 0, 1});
        skinPrimitive.joints.assign(3, {0, 0, 0, 0});
        skinPrimitive.weights.assign(3, {1, 0, 0, 0});
        assets::GltfAnimationAsset animation{};
        animation.skeleton.joints.push_back({"root", -1, {}});
        animation.inverseBindMatrices.push_back({});
        novacore::animation::GlobalPose pose{{novacore::animation::Mat4{}}};
        assets::GltfMeshData animated;
        expect(assets::skinGltfMesh(skinSource, animation, pose, animated).ok() && animated.images.size() == 1 &&
                   animated.images[0].encodedBytes == mesh.images[0].encodedBytes,
               "initial real CPU skinning shares encoded image data with the source");
        pose.joints[0].at(0, 3) = 2;
        expect(assets::skinGltfMesh(skinSource, animation, pose, animated).ok() && animated.primitives.size() == 1 &&
                   animated.images.size() == 1 && animated.primitives[0].positions[0].x == 2 &&
                   animated.primitives[0].texcoords1[1].x == 1 &&
                   animated.images[0].encodedBytes == mesh.images[0].encodedBytes && animated.textures.size() == 1,
               "reused animated vertex output retains immutable image/sampler/UV1 metadata across frames");
    }

    const auto importFails = [&](std::string changed, const char* message) {
        writeJson(path, changed);
        assets::GltfMeshData invalid;
        expect(!assets::loadGltfMeshData(path, invalid).ok(), message);
    };
    auto changed = json;
    replace(changed, "\"texCoord\":1", "\"texCoord\":2");
    importFails(changed, "unsupported requested UV set is rejected explicitly");
    changed = json;
    replace(changed, "\"index\":0", "\"index\":9");
    importFails(changed, "dangling material texture reference is rejected");
    changed = json;
    replace(changed, "\"wrapS\":33071", "\"wrapS\":7");
    importFails(changed, "unsupported sampler values are rejected instead of silently changed");
    changed = json;
    replace(changed, "\"source\":0", "\"source\":9");
    importFails(changed, "a dangling texture image cannot pass the CPU asset boundary");
    changed = json;
    replace(changed, "\"componentType\":5123,\"normalized\":true,\"count\":3,\"type\":\"VEC2\"",
            "\"componentType\":5123,\"normalized\":true,\"count\":2,\"type\":\"VEC2\"");
    importFails(changed, "UV1 vertex-count mismatch is rejected before upload");
    changed = json;
    replace(changed, "\"texCoord\":1",
            "\"texCoord\":1,\"extensions\":{\"KHR_texture_transform\":{\"rotation\":1,\"offset\":[0.2,0.3],\"scale\":["
            "2,3],\"texCoord\":0}}");
    writeJson(path, changed);
    expect(assets::loadGltfMeshData(path, mesh).ok() && mesh.materials[0].textureTransforms[0].enabled &&
               mesh.materials[0].textureTransforms[0].rotation == 1 &&
               mesh.materials[0].textureTransforms[0].offset.x == .2F &&
               mesh.materials[0].textureTransforms[0].scale.y == 3 &&
               mesh.materials[0].textureTransforms[0].texCoord == 0 &&
               !novacore::render::detail::materialTextureUniforms(mesh.materials[0]),
           "optional texture transforms survive CPU import with exact metadata and explicit renderer fallback");
    changed = json;
    replace(changed, "\"alphaMode\":\"OPAQUE\"", "\"alphaMode\":\"BLEND\"");
    writeJson(path, changed);
    expect(assets::loadGltfMeshData(path, mesh).ok() && mesh.materials[0].alphaMode == "BLEND",
           "BLEND metadata is retained for explicit renderer fallback rather than renamed opaque");

    write(directory / "checker space.png", png);
    changed = json;
    replace(changed, "checker.png", "checker%20space.png");
    writeJson(path, changed);
    expect(assets::loadGltfMeshData(path, mesh).ok() && *mesh.images[0].encodedBytes == png,
           "external image URIs use the existing percent-decoded file path contract");
    changed = json;
    replace(changed, "checker.png", "checker%GG.png");
    importFails(changed, "malformed external image URI escapes report an import error");

    constexpr auto dataImage =
        R"({"uri":"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAGUlEQVR4nAXBAQEAAAjDIKI9ms0niOTQKg9AGAe6dfu8vgAAAABJRU5ErkJggg=="})";
    writeJson(path, fixture(dataImage, binary.size(), png.size()));
    expect(assets::loadGltfMeshData(path, mesh).ok() && mesh.images[0].mimeType == "image/png" &&
               *mesh.images[0].encodedBytes == png,
           "data URI images retain their exact PNG payload and inferred MIME");

    auto glbJson = fixture(R"({"bufferView":4,"mimeType":"image/png"})", binary.size(), png.size(), false);
    while (glbJson.size() % 4) {
        glbJson += ' ';
    }
    while (binary.size() % 4) {
        binary.push_back(0);
    }
    std::vector<std::uint8_t> glb;
    for (const std::uint32_t value : {0x46546c67U, 2U, static_cast<std::uint32_t>(28 + glbJson.size() + binary.size()),
                                      static_cast<std::uint32_t>(glbJson.size()), 0x4e4f534aU}) {
        append(glb, value);
    }
    glb.insert(glb.end(), glbJson.begin(), glbJson.end());
    append(glb, static_cast<std::uint32_t>(binary.size()));
    append(glb, 0x004e4942U);
    glb.insert(glb.end(), binary.begin(), binary.end());
    write(directory / "fixture.glb", glb);
    expect(assets::loadGltfMeshData(directory / "fixture.glb", mesh).ok() && mesh.images[0].bufferViewIndex == 4 &&
               *mesh.images[0].encodedBytes == png,
           "GLB bufferView images resolve with their original MIME/source and exact encoded bytes");
}
} // namespace

int main() {
    const auto fixtureDirectory = fs::path(__FILE__).parent_path() / "fixtures/materials";
    const auto png = bytes(fixtureDirectory / "checker.png");
    const auto jpeg = bytes(fixtureDirectory / "solid.jpg");
    expect(!png.empty() && !jpeg.empty(), "owned deterministic image fixtures are available");
    if (png.empty() || jpeg.empty()) {
        return 1;
    }
    testDecode(png, jpeg);
    testMaterialUniformsAndBudget(png);
    const auto directory =
        fs::temp_directory_path() /
        ("novacore-materials-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(directory);
    testImport(directory, png);
    for (const auto* name : {"geometry.bin", "checker.png", "checker space.png", "fixture.gltf", "fixture.glb"}) {
        fs::remove(directory / name);
    }
    fs::remove(directory);
    return failures == 0 ? 0 : 1;
}
