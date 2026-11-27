#include "novacore/render/Renderer.hpp"

#include <array>
#include <iostream>
#include <memory>
#include <vector>

using namespace novacore;

int main() {
    platform::Window window;
    render::Renderer renderer;
    if (!window.create({"Surface inspection fixture", 640, 360, true}) ||
        !renderer.create(window, {.preferVulkan = true, .requireVulkan = true})) return 1;

    assets::GltfMeshData source;
    auto& triangle = source.primitives.emplace_back();
    triangle.materialIndex = 0;
    triangle.positions = {{0.20F, -0.65F, 3.0F}, {1.65F, -0.65F, 3.0F}, {0.92F, 0.75F, 3.0F}};
    triangle.normals.assign(3, {0, 1, 0});
    triangle.texcoords = {{0, 0}, {1, 0}, {0.5F, 1}};
    triangle.indices = {0, 1, 2};
    auto& material = source.materials.emplace_back();
    material.baseColorTextureIndex = 0;
    material.emissiveFactor = {1.0F, 0.10F, 0.15F};
    source.textures.emplace_back().imageIndex = 0;
    auto& png = source.images.emplace_back();
    png.mimeType = "image/png";
    // Original 1x1 opaque sRGB texel (64,128,224).
    png.encodedBytes = std::make_shared<const std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{
        137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,
        0,0,0,13,73,68,65,84,120,156,99,112,104,120,240,31,0,5,68,2,160,94,247,108,101,0,0,0,0,
        73,69,78,68,174,66,96,130});

    const auto mesh = renderer.registerMeshResource("inspection-triangle", source);
    if (!mesh.isValid()) return 2;
    render::RenderFrameInfo frame;
    frame.camera3D.enabled = true;
    frame.camera3D.position = {};
    frame.camera3D.verticalFovDegrees = 60;
    frame.worldBoxes.push_back({{-0.95F, 0, 3.0F}, {0.54F, 0.56F, 0.20F}, {0.8F, 0.22F, 0.04F, 1}});
    auto& draw = frame.worldMeshes.emplace_back();
    draw.mesh = mesh;
    draw.color = {1, 1, 1, 1};
    frame.debugRects.push_back({12, 12, 32, 16, {1, 0, 1, 1}});
    using enum render::RenderDebugView;
    constexpr std::array phases{Lit, Lit, WorldNormals, BaseColor, Lit, BaseColor, WorldNormals, Lit};
    for (std::size_t index = 0; index < phases.size() * 4; ++index) {
        const auto phase = index / 4;
        frame.debugView = phases[phase];
        frame.materialTexturesEnabled = phase != 5 && phase != 6;
        frame.directionalShadows.enabled = frame.materialTexturesEnabled;
        renderer.beginFrame(frame);
        renderer.endFrame();
        if (index % 4 == 3) {
            const auto stats = renderer.backendFrameStats();
            std::cout << index << ' ' << static_cast<int>(frame.debugView) << ' '
                      << stats.lastMaterialTextureDrawCount << ' ' << stats.lastWorldBoxCount << '\n';
            if (!stats.swapchainReady || !stats.lastWorldBoxCount || !stats.lastWorldMeshCount ||
                (frame.materialTexturesEnabled && !stats.lastMaterialTextureDrawCount) ||
                (frame.directionalShadows.enabled && !stats.directionalShadowsReady)) return 3;
        }
    }
}
