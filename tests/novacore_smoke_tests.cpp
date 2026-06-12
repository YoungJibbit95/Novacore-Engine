#include "novacore/Novacore.hpp"

#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (condition) {
        return;
    }

    ++failures;
    std::cerr << "[fail] " << message << '\n';
}

void testWorldLifetime() {
    novacore::ecs::World world;

    const auto entity = world.createEntity();
    expect(world.isAlive(entity), "created entity is alive");

    world.addComponent(entity, novacore::ecs::NameComponent{"smoke"});
    expect(world.getComponent<novacore::ecs::NameComponent>(entity) != nullptr, "component can be read");

    world.destroyEntity(entity);
    expect(!world.isAlive(entity), "destroyed entity is not alive");
    expect(world.aliveCount() == 0, "alive count returns to zero");
    expect(world.getComponent<novacore::ecs::NameComponent>(entity) == nullptr, "destroy removes components");
}

void testFixedStepAccumulator() {
    novacore::core::FixedStepAccumulator accumulator({60.0, 0.25});
    accumulator.advance(1.0 / 30.0);

    int steps = 0;
    while (accumulator.shouldStep()) {
        accumulator.consumeStep();
        ++steps;
    }

    expect(steps == 2, "1/30s advances exactly two 60Hz ticks");
    expect(accumulator.consumedSteps() == 2, "consumed step counter matches");
    expect(accumulator.alpha() < 0.001, "alpha is near zero after exact fixed steps");
}

void testLoopbackChannel() {
    novacore::net::LoopbackChannel loopback;
    novacore::net::Packet packet{};
    packet.sequence = novacore::net::PacketSequence{7};
    packet.payload = {1, 2, 3, 4};

    loopback.sendToServer(packet);

    novacore::net::Packet received{};
    expect(loopback.tryReceiveForServer(received), "server receives loopback packet");
    expect(received.sequence == novacore::net::PacketSequence{7}, "packet sequence survives loopback");
    expect(received.payload.size() == 4, "packet payload survives loopback");
}

void testPacketBitStream() {
    novacore::net::PacketWriter writer;
    writer.writeU8(7);
    writer.writeU16(513);
    writer.writeU32(0xAABBCCDDU);
    writer.writeU64(0x0102030405060708ULL);
    writer.writeFloat(12.5F);
    writer.writeBytes("ok");

    const auto bytes = writer.finish();
    novacore::net::PacketReader reader(bytes);

    std::uint8_t u8 = 0;
    std::uint16_t u16 = 0;
    std::uint32_t u32 = 0;
    std::uint64_t u64 = 0;
    float value = 0.0F;

    expect(reader.readU8(u8) && u8 == 7, "bitstream reads u8");
    expect(reader.readU16(u16) && u16 == 513, "bitstream reads u16 little endian");
    expect(reader.readU32(u32) && u32 == 0xAABBCCDDU, "bitstream reads u32");
    expect(reader.readU64(u64) && u64 == 0x0102030405060708ULL, "bitstream reads u64");
    expect(reader.readFloat(value) && std::abs(value - 12.5F) < 0.001F, "bitstream reads float");
    const auto text = reader.readBytes(2);
    expect(text.has_value() && *text == "ok", "bitstream reads bytes");
    expect(reader.consumed(), "bitstream consumes payload");
    expect(!reader.readU8(u8), "bitstream rejects overread");
}

void testHeadlessRelativeMouseFallback() {
    novacore::platform::Window window;
    expect(!window.relativeMouseMode(), "relative mouse starts disabled");
    expect(!window.setRelativeMouseMode(true), "headless relative mouse cannot enable without window");
    expect(!window.relativeMouseMode(), "headless relative mouse remains disabled");
}

void testInputActions() {
    novacore::platform::InputActionMap actionMap;
    novacore::platform::InputControl jumpKey{
        novacore::platform::InputControlKind::KeyboardKey,
        32,
    };

    actionMap.bind(novacore::platform::InputBinding{"jump", jumpKey, 1.0F, 0.5F});

    novacore::platform::InputSnapshot snapshot;
    snapshot.setButton(jumpKey, true, novacore::platform::InputDeviceKind::KeyboardMouse);
    actionMap.update(snapshot);

    const auto jump = actionMap.stateOrDefault("jump");
    expect(jump.down, "bound button drives action down state");
    expect(jump.pressed, "first down frame reports pressed");

    actionMap.update(snapshot);
    expect(!actionMap.stateOrDefault("jump").pressed, "held button does not repeat pressed");

    snapshot.setButton(jumpKey, false, novacore::platform::InputDeviceKind::KeyboardMouse);
    actionMap.update(snapshot);
    expect(actionMap.stateOrDefault("jump").released, "button release reports released");
}

void testTransientMouseAxis() {
    novacore::platform::InputControl mouseX{
        novacore::platform::InputControlKind::MouseAxis,
        0,
    };

    novacore::platform::InputSnapshot snapshot;
    snapshot.addAxisDelta(mouseX, 4.0F, novacore::platform::InputDeviceKind::KeyboardMouse);
    snapshot.addAxisDelta(mouseX, 2.0F, novacore::platform::InputDeviceKind::KeyboardMouse);
    expect(snapshot.axisValue(mouseX) == 6.0F, "mouse axis deltas accumulate in frame");

    snapshot.beginFrame();
    expect(snapshot.axisValue(mouseX) == 0.0F, "mouse axis resets between frames");
}

void testInputDeviceActivity() {
    novacore::platform::InputSystem input;

    expect(input.lastActiveDevice() == novacore::platform::InputDeviceKind::KeyboardMouse, "keyboard/mouse is default input device");
    input.noteControllerActivity();
    expect(input.lastActiveDevice() == novacore::platform::InputDeviceKind::Controller, "controller activity updates active device");
    input.noteKeyboardMouseActivity();
    expect(input.lastActiveDevice() == novacore::platform::InputDeviceKind::KeyboardMouse, "keyboard/mouse activity restores active device");
}

void testFileChangeTracker() {
    const auto path = std::filesystem::temp_directory_path() / "novacore_smoke_file.txt";
    {
        std::ofstream file(path);
        file << "first";
    }

    novacore::io::FileChangeTracker tracker;
    tracker.track(path);
    expect(tracker.trackedCount() == 1, "file tracker records tracked file");
    expect(tracker.pollChanges().empty(), "unchanged file does not emit change");

    {
        std::ofstream file(path, std::ios::app);
        file << "second";
    }

    expect(!tracker.pollChanges().empty(), "modified file emits change");
    std::filesystem::remove(path);
}

void testConfigDocument() {
    constexpr std::string_view json = R"({
        "movement": {
            "sprint_speed": 7.4,
            "enabled": true
        },
        "weapons": [
            { "id": "ar_01", "magazine_size": 30 }
        ]
    })";

    novacore::core::ConfigDocument document;
    const auto result = novacore::core::parseJsonConfig(json, document);

    expect(result.ok(), "json config parses successfully");
    expect(document.numberOr("movement.sprint_speed", 0.0) > 7.3, "nested number can be read");
    expect(document.boolOr("movement.enabled", false), "nested bool can be read");
    expect(document.stringOr("weapons.0.id", "") == "ar_01", "array object string can be read");
    expect(document.intOr("weapons.0.magazine_size", 0) == 30, "array object int can be read");
}

void testConfigRegistry() {
    const auto path = std::filesystem::temp_directory_path() / "novacore_smoke_config.json";
    {
        std::ofstream file(path);
        file << R"({ "value": 1 })";
    }

    novacore::core::ConfigRegistry registry;
    const auto initial = registry.watchJson("smoke", path);
    expect(initial.loaded, "registry loads watched json file");
    expect(registry.watchedCount() == 1, "registry tracks watched config");

    const auto* document = registry.find("smoke");
    expect(document != nullptr, "watched config is available");
    expect(document != nullptr && document->intOr("value", 0) == 1, "watched config value can be read");

    {
        std::ofstream file(path);
        file << R"({ "value": 22 })";
    }

    const auto events = registry.pollReloads();
    expect(!events.empty(), "registry emits reload event after file change");
    document = registry.find("smoke");
    expect(document != nullptr && document->intOr("value", 0) == 22, "registry reload updates document");
    std::filesystem::remove(path);
}

void testAssetManifestAndRegistry() {
    const auto path = std::filesystem::temp_directory_path() / "novacore_smoke_assets.json";
    {
        std::ofstream file(path);
        file << R"({
            "manifest": {
                "name": "smoke_assets",
                "root": "assets"
            },
            "assets": [
                {
                    "id": "mesh_crate_01",
                    "kind": "mesh",
                    "source": "assets/source/blender/props/mesh_crate_01.blend",
                    "cooked": "assets/export/gltf/props/mesh_crate_01.glb",
                    "streamable": true,
                    "priority": 75,
                    "estimated_bytes": 4096,
                    "tags": ["prop", "smoke"],
                    "dependencies": ["mat_crate_01"]
                },
                {
                    "id": "mat_crate_01",
                    "kind": "material",
                    "source": "assets/materials/mat_crate_01.json",
                    "tags": ["material"]
                }
            ]
        })";
    }

    novacore::assets::AssetManifest manifest;
    const auto loadResult = novacore::assets::loadAssetManifestFromJson(path, manifest);
    expect(loadResult.ok(), "asset manifest loads from json");
    expect(manifest.name() == "smoke_assets", "asset manifest keeps name");
    expect(manifest.recordCount() == 2, "asset manifest keeps records");

    const auto* crate = manifest.find("mesh_crate_01");
    expect(crate != nullptr, "asset manifest finds mesh");
    expect(crate != nullptr && crate->kind == novacore::assets::AssetKind::Mesh, "asset kind parses");
    expect(crate != nullptr && crate->streamable, "asset streamable flag parses");
    expect(crate != nullptr && crate->dependencies.size() == 1, "asset dependencies parse");

    novacore::assets::AssetRegistry registry;
    registry.mountManifest(manifest);
    expect(registry.assetCount() == 2, "asset registry mounts manifest");
    expect(registry.contains("mat_crate_01"), "asset registry finds dependency asset");
    expect(registry.idsForTag("smoke").size() == 1, "asset registry filters by tag");
    expect(registry.streamableAssetIds().size() == 1, "asset registry filters streamable assets");

    std::filesystem::remove(path);
}

void testAssetStreamer() {
    novacore::assets::AssetStreamer streamer;
    streamer.request("low_priority", 1, 10);
    streamer.request("high_priority", 90, 11);
    streamer.request("low_priority", 20, 12);

    expect(streamer.pendingCount() == 2, "asset streamer coalesces duplicate requests");
    const auto selected = streamer.popBudgeted(1);
    expect(selected.size() == 1, "asset streamer respects request budget");
    expect(selected[0].id == "high_priority", "asset streamer pops highest priority first");
    expect(streamer.pendingCount() == 1, "asset streamer keeps unpopped requests");

    novacore::assets::AssetStreamingZone zone{};
    zone.name = "smoke_zone";
    zone.preloadAssets = {"zone_a", "zone_b"};
    streamer.requestZone(zone, 20);
    expect(streamer.pendingCount() == 3, "asset streamer accepts zone preloads");
}

void appendU32(std::string& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<char>(value & 0xFFU));
    bytes.push_back(static_cast<char>((value >> 8U) & 0xFFU));
    bytes.push_back(static_cast<char>((value >> 16U) & 0xFFU));
    bytes.push_back(static_cast<char>((value >> 24U) & 0xFFU));
}

void appendU16(std::string& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<char>(value & 0xFFU));
    bytes.push_back(static_cast<char>((value >> 8U) & 0xFFU));
}

void appendF32(std::string& bytes, float value) {
    char raw[sizeof(float)]{};
    std::memcpy(raw, &value, sizeof(float));
    bytes.append(raw, sizeof(float));
}

void appendPaddedChunk(std::string& bytes, std::string chunk, std::uint32_t chunkType, char padding) {
    while ((chunk.size() % 4U) != 0U) {
        chunk.push_back(padding);
    }
    appendU32(bytes, static_cast<std::uint32_t>(chunk.size()));
    appendU32(bytes, chunkType);
    bytes += chunk;
}

void writeTinyGlb(const std::filesystem::path& path) {
    constexpr std::uint32_t kGlbMagic = 0x46546C67U;
    constexpr std::uint32_t kGlbJsonChunk = 0x4E4F534AU;
    constexpr std::uint32_t kGlbBinaryChunk = 0x004E4942U;

    const std::string json = R"({
        "asset": { "version": "2.0" },
        "scene": 0,
        "scenes": [{ "nodes": [0] }],
        "nodes": [{ "mesh": 0 }],
        "meshes": [{ "primitives": [{ "attributes": { "POSITION": 0 }, "indices": 1, "material": 0 }] }],
        "materials": [{ "name": "mat_smoke" }],
        "buffers": [{ "byteLength": 42 }],
        "bufferViews": [
            { "buffer": 0, "byteOffset": 0, "byteLength": 36 },
            { "buffer": 0, "byteOffset": 36, "byteLength": 6 }
        ],
        "accessors": [
            { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3" },
            { "bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR" }
        ]
    })";
    std::string bin;
    appendF32(bin, 0.0F);
    appendF32(bin, 0.0F);
    appendF32(bin, 0.0F);
    appendF32(bin, 1.0F);
    appendF32(bin, 0.0F);
    appendF32(bin, 0.0F);
    appendF32(bin, 0.0F);
    appendF32(bin, 1.0F);
    appendF32(bin, 0.0F);
    appendU16(bin, 0);
    appendU16(bin, 1);
    appendU16(bin, 2);

    std::string chunks;
    appendPaddedChunk(chunks, json, kGlbJsonChunk, ' ');
    appendPaddedChunk(chunks, bin, kGlbBinaryChunk, '\0');

    std::string glb;
    appendU32(glb, kGlbMagic);
    appendU32(glb, 2);
    appendU32(glb, static_cast<std::uint32_t>(12U + chunks.size()));
    glb += chunks;

    std::ofstream file(path, std::ios::binary);
    file.write(glb.data(), static_cast<std::streamsize>(glb.size()));
}

void testGltfMetadataAndMeshCatalog() {
    const auto path = std::filesystem::temp_directory_path() / "novacore_smoke_mesh.metadata.json";
    {
        std::ofstream file(path);
        file << R"({
            "id": "mesh_crate_01",
            "source": "assets/source/blender/props/mesh_crate_01.blend",
            "export": "assets/export/gltf/props/mesh_crate_01.glb",
            "category": "prop",
            "scale_meters": true,
            "runtime_up_axis": "Y",
            "gameplay_forward_axis": "+Z",
            "sockets": ["socket_root"],
            "collision": "col_mesh_crate_01",
            "lods": ["mesh_crate_01_lod0"],
            "license": "original_project_asset",
            "generated_by": "smoke"
        })";
    }

    novacore::assets::AssetRecord meshRecord{};
    meshRecord.id = "mesh_crate_01";
    meshRecord.kind = novacore::assets::AssetKind::Mesh;
    meshRecord.sourcePath = "assets/source/blender/props/mesh_crate_01.blend";
    meshRecord.cookedPath = "assets/export/gltf/props/mesh_crate_01.glb";
    meshRecord.estimatedBytes = 4096;
    meshRecord.tags = {"prop", "smoke"};

    novacore::assets::GltfAssetMetadata metadata;
    const auto metadataResult = novacore::assets::loadGltfAssetMetadataFromJson(path, metadata);
    expect(metadataResult.ok(), "glTF metadata loads from json");
    expect(metadata.id == meshRecord.id, "glTF metadata keeps asset id");
    expect(metadata.sockets.size() == 1, "glTF metadata parses sockets");
    expect(novacore::assets::metadataPathForCookedAsset(meshRecord).generic_string() ==
               "assets/export/gltf/props/mesh_crate_01.metadata.json",
           "metadata path is derived from cooked glTF path");
    expect(novacore::assets::validateGltfAssetMetadata(meshRecord, metadata).empty(), "glTF metadata validates against record");

    const auto glbPath = std::filesystem::temp_directory_path() / "novacore_smoke_mesh.glb";
    writeTinyGlb(glbPath);

    novacore::assets::GltfSceneInfo sceneInfo;
    const auto sceneInfoResult = novacore::assets::loadGltfSceneInfo(glbPath, sceneInfo);
    expect(sceneInfoResult.ok(), "glb scene info loads");
    expect(sceneInfo.container == novacore::assets::GltfContainerKind::BinaryGlb, "glb scene info records container kind");
    expect(sceneInfo.meshCount == 1, "glb scene info counts meshes");
    expect(sceneInfo.nodeCount == 1, "glb scene info counts nodes");
    expect(sceneInfo.materialCount == 1, "glb scene info counts materials");
    expect(sceneInfo.accessorCount == 2, "glb scene info counts accessors");
    expect(sceneInfo.bufferViewCount == 2, "glb scene info counts buffer views");
    expect(sceneInfo.binaryBytes == 44, "glb scene info records padded binary chunk bytes");

    novacore::assets::GltfMeshData meshData;
    const auto meshDataResult = novacore::assets::loadGltfMeshData(glbPath, meshData);
    expect(meshDataResult.ok(), "glb mesh data imports");
    expect(meshData.primitiveCount() == 1, "glb mesh data counts primitives");
    expect(meshData.vertexCount() == 3, "glb mesh data counts vertices");
    expect(meshData.indexCount() == 3, "glb mesh data counts indices");
    expect(
        !meshData.primitives.empty() && meshData.primitives[0].positions.size() > 1 &&
            meshData.primitives[0].positions[1].x == 1.0F,
        "glb mesh data reads position floats");
    expect(
        !meshData.primitives.empty() && meshData.primitives[0].indices.size() > 2 &&
            meshData.primitives[0].indices[2] == 2,
        "glb mesh data reads unsigned short indices");

    novacore::render::MeshCatalog meshes;
    const auto meshHandle = meshes.registerImportedGltfAsset(meshRecord, metadata, meshData);
    expect(meshHandle.isValid(), "mesh catalog creates mesh handle");
    expect(meshes.meshCount() == 1, "mesh catalog counts registered mesh");
    const auto* mesh = meshes.find(meshHandle);
    expect(mesh != nullptr && mesh->assetId == meshRecord.id, "mesh handle resolves to asset source");
    expect(mesh != nullptr && mesh->metadata.has_value(), "mesh source keeps metadata");
    expect(mesh != nullptr && mesh->sceneInfo.has_value(), "mesh source keeps glb scene info");
    expect(mesh != nullptr && mesh->meshData.has_value(), "mesh source keeps glb mesh data");
    expect(meshes.findByAssetId(meshRecord.id) == mesh, "mesh catalog resolves by asset id");

    novacore::assets::AssetRecord sceneRecord{};
    sceneRecord.id = "scene_arena_01";
    sceneRecord.kind = novacore::assets::AssetKind::Scene;
    sceneRecord.cookedPath = "assets/export/gltf/environments/scene_arena_01.glb";
    expect(meshes.registerAsset(sceneRecord).isValid(), "mesh catalog accepts glTF scene records");

    novacore::assets::AssetRecord materialRecord{};
    materialRecord.id = "mat_crate_01";
    materialRecord.kind = novacore::assets::AssetKind::Material;
    materialRecord.cookedPath = "assets/materials/mat_crate_01.json";
    expect(!novacore::render::isRenderableAssetRecord(materialRecord), "material is not renderable as mesh");
    expect(!meshes.registerAsset(materialRecord).isValid(), "mesh catalog rejects non-renderable records");

    std::filesystem::remove(path);
    std::filesystem::remove(glbPath);
}

novacore::assets::GltfMeshData makeSmokeMeshData(std::string_view pathSuffix) {
    novacore::assets::GltfPrimitiveData primitive{};
    primitive.meshIndex = 0;
    primitive.primitiveIndex = 0;
    primitive.materialIndex = 0;
    primitive.positions = {
        {-0.5F, 0.0F, 0.0F},
        {0.5F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
    };
    primitive.normals = {
        {0.0F, 0.0F, 1.0F},
        {0.0F, 0.0F, 1.0F},
        {0.0F, 0.0F, 1.0F},
    };
    primitive.texcoords = {
        {0.0F, 0.0F},
        {1.0F, 0.0F},
        {0.5F, 1.0F},
    };
    primitive.indices = {0, 1, 2};

    novacore::assets::GltfMeshData meshData{};
    meshData.path = std::filesystem::path("assets/export/gltf/smoke") / pathSuffix;
    meshData.sceneInfo.path = meshData.path;
    meshData.sceneInfo.container = novacore::assets::GltfContainerKind::BinaryGlb;
    meshData.sceneInfo.meshCount = 1;
    meshData.sceneInfo.nodeCount = 1;
    meshData.sceneInfo.materialCount = 1;
    meshData.sceneInfo.accessorCount = 3;
    meshData.sceneInfo.bufferViewCount = 3;
    meshData.primitives.push_back(std::move(primitive));
    return meshData;
}

void testRendererMeshResourceRegistry() {
    novacore::render::Renderer renderer;

    const auto initialFrameStats = renderer.backendFrameStats();
    expect(!initialFrameStats.swapchainReady, "renderer backend frame stats start with no swapchain");
    expect(initialFrameStats.submittedFrames == 0, "renderer backend frame stats start with zero submitted frames");
    expect(initialFrameStats.swapchainRecreateCount == 0, "renderer backend frame stats start with zero recreates");

    auto triangle = makeSmokeMeshData("triangle.glb");
    const auto invalid = renderer.registerMeshResource("bad_empty_mesh", novacore::assets::GltfMeshData{});
    expect(!invalid.isValid(), "renderer mesh resources reject empty mesh data");

    const auto first = renderer.registerMeshResource("smoke_triangle", triangle);
    expect(first.isValid(), "renderer mesh resource creates a valid handle");
    expect(renderer.findMeshResource("smoke_triangle") == first, "renderer mesh resource resolves by asset id");

    auto stats = renderer.meshResourceStats();
    expect(stats.registeredResources == 1, "mesh resource stats count registered resource");
    expect(stats.totalPrimitives == 1, "mesh resource stats count primitives");
    expect(stats.totalVertices == 3, "mesh resource stats count vertices");
    expect(stats.totalIndices == 3, "mesh resource stats count indices");
    expect(stats.pendingUploadResources == 0, "mesh resource stats are CPU-only before renderer create");
    expect(stats.residentResources == 0, "mesh resource stats have no resident GPU resources before renderer create");

    const auto duplicate = renderer.registerMeshResource("smoke_triangle", triangle);
    expect(duplicate == first, "duplicate mesh resource registration returns existing handle");
    stats = renderer.meshResourceStats();
    expect(stats.registeredResources == 1, "duplicate mesh resource registration does not grow registry");

    auto secondMesh = makeSmokeMeshData("second.glb");
    secondMesh.primitives.front().positions.push_back({0.0F, -1.0F, 0.0F});
    secondMesh.primitives.front().normals.push_back({0.0F, 0.0F, 1.0F});
    secondMesh.primitives.front().texcoords.push_back({0.5F, -1.0F});
    secondMesh.primitives.front().indices = {0, 1, 2, 0, 2, 3};
    const auto second = renderer.registerMeshResource("smoke_quad", secondMesh);
    expect(second.isValid(), "renderer mesh resource accepts second mesh");
    expect(second != first, "renderer mesh resource handles are unique per asset");

    stats = renderer.meshResourceStats();
    expect(stats.registeredResources == 2, "mesh resource stats count two registered resources");
    expect(stats.totalPrimitives == 2, "mesh resource stats aggregate primitive counts");
    expect(stats.totalVertices == 7, "mesh resource stats aggregate vertex counts");
    expect(stats.totalIndices == 9, "mesh resource stats aggregate index counts");

    renderer.releaseMeshResource(first);
    expect(!renderer.findMeshResource("smoke_triangle").isValid(), "released mesh resource no longer resolves by asset id");
    expect(renderer.findMeshResource("smoke_quad") == second, "unreleased mesh resource survives neighbor release");

    stats = renderer.meshResourceStats();
    expect(stats.registeredResources == 1, "mesh resource stats drop released resource");
    expect(stats.totalVertices == 4, "mesh resource stats keep remaining vertex count");
    expect(stats.totalIndices == 6, "mesh resource stats keep remaining index count");

    const auto reused = renderer.registerMeshResource("smoke_triangle_reloaded", triangle);
    expect(reused.isValid(), "mesh resource can reuse a released slot");
    expect(reused.index == first.index, "mesh resource registry reuses freed slot indices");
    expect(reused.generation != first.generation, "mesh resource registry bumps generation on reused slot");
    expect(renderer.findMeshResource("smoke_triangle_reloaded") == reused, "reloaded mesh resolves by new asset id");

    renderer.releaseMeshResource(second);
    renderer.releaseMeshResource(reused);
    stats = renderer.meshResourceStats();
    expect(stats.registeredResources == 0, "mesh resource registry releases all resources cleanly");
    expect(stats.totalVertices == 0, "mesh resource registry clears vertex stats after full release");
    expect(stats.totalIndices == 0, "mesh resource registry clears index stats after full release");
}

void testPhysicsCharacterControllerSurfaces() {
    novacore::physics::PhysicsWorld world;
    world.setBounds({12.0F, 6.0F, 12.0F});
    world.addStaticCollider(novacore::physics::StaticCollider{
        "low_step",
        novacore::physics::SurfaceKind::Cover,
        {0.0F, 0.18F, 2.0F},
        {1.0F, 0.18F, 1.0F},
        true,
        novacore::physics::RampDirection::None,
        0.36F,
    });
    world.addStaticCollider(novacore::physics::StaticCollider{
        "slide_ramp",
        novacore::physics::SurfaceKind::Ramp,
        {3.0F, 0.45F, 0.0F},
        {1.2F, 0.45F, 2.0F},
        true,
        novacore::physics::RampDirection::PositiveZ,
    });
    world.addStaticCollider(novacore::physics::StaticCollider{
        "left_wallrun_panel",
        novacore::physics::SurfaceKind::WallRun,
        {-4.0F, 1.3F, 0.0F},
        {0.15F, 1.3F, 2.5F},
        true,
    });
    world.addStaticCollider(novacore::physics::StaticCollider{
        "mid_ledge",
        novacore::physics::SurfaceKind::Ledge,
        {6.0F, 0.65F, 0.0F},
        {1.2F, 0.65F, 1.2F},
        true,
    });

    expect(world.colliderCount() == 4, "physics world stores static colliders");
    expect(world.findStaticCollider("left_wallrun_panel") != nullptr, "physics world finds static collider by id");
    expect(std::string_view(novacore::physics::surfaceKindName(novacore::physics::SurfaceKind::WallRun)) == "wall_run",
           "physics surface names are stable for debug UI");

    const auto open = world.resolveCharacter(novacore::physics::CharacterQuery{{0.0F, 0.0F, -3.0F}, 0.42F, 1.80F});
    expect(open.grounded, "physics character grounds on floor");
    expect(open.groundColliderId == "floor_main", "physics character reports default floor id");
    expect(!open.blocked, "open physics lane has no blocking correction");

    const auto step = world.resolveCharacter(novacore::physics::CharacterQuery{{0.0F, 0.0F, 2.0F}, 0.42F, 1.80F});
    expect(step.grounded, "physics character grounds on low step");
    expect(step.stepped, "physics character reports step-up");
    expect(step.groundColliderId == "low_step", "physics character reports stepped collider");
    expect(step.position.y > 0.34F && step.position.y < 0.38F, "physics step resolves top height");

    const auto ramp = world.resolveCharacter(novacore::physics::CharacterQuery{{3.0F, 0.28F, 0.0F}, 0.42F, 1.80F});
    expect(ramp.grounded, "physics character grounds on ramp");
    expect(ramp.onRamp, "physics character reports ramp state");
    expect(ramp.groundColliderId == "slide_ramp", "physics character reports ramp collider");
    expect(ramp.groundNormal.y > 0.90F, "physics ramp normal is walkable");
    expect(ramp.groundNormal.z < 0.0F, "positive-z physics ramp normal leans against slope");

    const auto probe = world.probeWall(novacore::physics::WallProbe{{-3.48F, 0.5F, 0.0F}, 0.42F, 1.80F, 0.35F});
    expect(probe.hit, "physics wall probe detects nearby wallrun panel");
    expect(probe.kind == novacore::physics::SurfaceKind::WallRun, "physics wall probe preserves wallrun surface kind");
    expect(probe.colliderId == "left_wallrun_panel", "physics wall probe reports collider id");
    expect(probe.tangent.lengthSquared() > 0.5F, "physics wall probe produces runnable tangent");

    const auto wallContact = world.resolveCharacter(novacore::physics::CharacterQuery{{-4.0F, 0.0F, 0.0F}, 0.42F, 1.80F});
    expect(wallContact.blocked, "physics character is pushed out of wallrun panel");
    expect(wallContact.nearWallRunSurface, "physics character records wallrun-capable contact");
    expect(wallContact.wallColliderId == "left_wallrun_panel", "physics character reports wallrun contact id");

    const auto ledge = world.resolveCharacter(novacore::physics::CharacterQuery{{6.0F, 0.0F, 0.0F}, 0.42F, 1.80F});
    expect(ledge.blocked, "physics ledge blocks when above step height");
    expect(!ledge.stepped, "physics ledge does not count as step-up");
    expect(ledge.lastColliderId == "mid_ledge", "physics ledge reports collider id");
}

void testVulkanRuntimeProbeIsStable() {
    const auto info = novacore::render::probeVulkanRuntime();
    const auto summary = novacore::render::vulkanRuntimeSummary(info);

    expect(!summary.empty(), "vulkan runtime probe returns a summary");
    if (info.loaderAvailable) {
        expect(info.instanceApiVersion > 0, "vulkan runtime probe reports instance version when loader is available");
    }
}

} // namespace

int main() {
    testWorldLifetime();
    testFixedStepAccumulator();
    testLoopbackChannel();
    testPacketBitStream();
    testHeadlessRelativeMouseFallback();
    testInputActions();
    testTransientMouseAxis();
    testInputDeviceActivity();
    testFileChangeTracker();
    testConfigDocument();
    testConfigRegistry();
    testAssetManifestAndRegistry();
    testAssetStreamer();
    testGltfMetadataAndMeshCatalog();
    testRendererMeshResourceRegistry();
    testPhysicsCharacterControllerSurfaces();
    testVulkanRuntimeProbeIsStable();

    if (failures > 0) {
        std::cerr << failures << " NovaCore smoke test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "NovaCore smoke tests passed\n";
    return EXIT_SUCCESS;
}
