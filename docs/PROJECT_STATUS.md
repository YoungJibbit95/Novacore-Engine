# NovaCore Project Status

## Current Foundation

- Public engine API under `engine/include/novacore`.
- CMake target `Novacore::Engine`.
- Optional dedicated server target.
- Core fixed-step application runtime.
- Lightweight math primitives.
- Custom ECS with entity lifetime and component storage.
- SDL3/headless platform window foundation.
- SDL3 can be consumed from an installed package or fetched automatically for visible dev builds.
- Window event polling now maintains a persistent `InputSnapshot` for action maps.
- Mouse movement is exposed as transient per-frame mouse axes.
- Opt-in Vulkan backend with SDL surface creation, physical-device selection, swapchain, render pass, framebuffers, sync objects, and a first graphics pipeline.
- SDK-free Vulkan runtime probing can detect loader version and physical devices even when the compiled Vulkan backend is not selected.
- CMake compiles the first Vulkan debug shaders to SPIR-V through `glslc` when the Vulkan SDK is visible.
- SDL debug renderer remains available for immediate menu/debug UI work.
- Debug render primitives support rectangles, lines, and 5x7 bitmap text.
- Asset manifest, registry, and streaming request queue backbone under `novacore::assets`.
- glTF asset metadata loading and validation is available through `novacore::assets::GltfAssetMetadata`.
- CPU-side glTF/GLB scene info loading is available through `novacore::assets::GltfSceneInfo`.
- CPU-side GLB mesh extraction is available through `novacore::assets::GltfMeshData`.
- Renderer mesh-handle placeholders are available through `novacore::render::MeshCatalog`.
- Loopback net packet foundation.
- Engine-owned packet bitstream writer/reader for deterministic little-endian command and ack payloads.
- Window relative mouse mode helper for FPS camera capture, with safe headless fallback.
- Engine-owned server defaults in `configs/net`.
- Dependency-free smoke test target.

## Added In Latest Block

- Added `AssetRecord`, `AssetManifest`, `AssetRegistry`, and `AssetStreamer`.
- Asset manifests now load from JSON through the existing NovaCore config parser.
- Asset registry supports lookup by id, tags, and streamable state.
- Asset streaming requests coalesce duplicate ids and pop by priority/age.
- Added Visual Studio 2022 no-deps CMake preset for IDE-friendly local bootstrapping.
- Application delegates can report headless mode so the runtime avoids tight busy-yield loops in fallback/server-style runs.
- `windows-msvc-debug` now uses the Visual Studio generator and no longer depends on Ninja or an unset `VCPKG_ROOT`.
- `windows-ninja-vcpkg-debug` keeps the old full-dependency Ninja/vcpkg path as an explicit preset.
- Renderer now creates an SDL debug backend when SDL3 is available, with clear, debug rectangles, and bitmap debug text.
- Window title updates are exposed through `Window::setTitle`.
- Added `PacketWriter`/`PacketReader` to make game protocol messages use engine-owned binary serialization.
- Added `Window::setRelativeMouseMode` and `Window::relativeMouseMode` for raw-mouse-style FPS look capture through SDL3.
- Added a CMake FetchContent fallback for SDL3 so normal dev builds no longer silently become headless when vcpkg is missing.
- Updated SDL3 key handling to use SDL3 uppercase keycode names.
- Added SDL debug line primitives so games can draw world maps, aim rays, and range helpers before the real mesh renderer is ready.
- Added glTF sidecar metadata parsing, cooked metadata path derivation, and asset-record validation.
- Added renderable asset validation plus `MeshHandle`/`MeshCatalog` registration for mesh and scene glTF records.
- Added GLB v2/text glTF scene info loading for mesh, node, material, accessor, buffer view, buffer, image, animation, skin, JSON, and BIN chunk counts.
- Added first CPU GLB mesh extraction for primitive positions, optional normals/UVs, unsigned indices, byte offsets, and byte strides.
- `MeshCatalog` can now register imported glTF assets with sidecar metadata, scene info, and CPU mesh data.
- Added SDK-free Vulkan runtime/device probing through dynamic loader entry points; current local smoke runs detect Vulkan 1.4.350 on an NVIDIA GeForce RTX 3070 Ti.
- Renderer startup now logs Vulkan runtime availability separately from the active SDL debug/null render backend.
- Added MinGW runtime DLL copying for NovaCore server/test executables so direct local launches do not require PATH surgery.
- Smoke coverage now checks manifest loading, registry filters, streaming request behavior, packet bitstreams, mesh catalog handles, glTF metadata, GLB scene-info loading, GLB triangle mesh extraction, Vulkan runtime probing, and headless relative-mouse fallback.
- Added `VulkanBackend` with instance/surface/device/queue/swapchain setup, image views, render pass, framebuffers, command buffers, semaphores, fences, clear, present, and a debug triangle draw.
- Added `shaders/vulkan/debug_triangle.vert` and `.frag`; CMake compiles them with `F:\VulkanSDK\1.4.350.0\Bin\glslc.exe` in the current local build.
- Renderer creation can now prefer the Vulkan backend while keeping SDL debug rendering as the default visible tool path.

## Next Engine Blocks

- Add typed config schemas for renderer, server, input, and network.
- Add UDP transport socket backend on top of the packet primitives.
- Add movement validation helpers for Nemisis.
- Add swapchain resize/recreate and validation-layer debug names.
- Add GPU upload/resource ownership for extracted mesh data.
- Add renderer mesh draw submission on top of `MeshCatalog` handles.
- Add depth buffer and camera matrix path for the first in-world greybox.
