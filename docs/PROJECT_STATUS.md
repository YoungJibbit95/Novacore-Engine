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
- Vulkan/null renderer placeholder plus SDL debug renderer for immediate visual tests.
- Debug render primitives support rectangles and 5x7 bitmap text.
- Asset manifest, registry, and streaming request queue backbone under `novacore::assets`.
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
- Smoke coverage now checks manifest loading, registry filters, streaming request behavior, packet bitstreams, and headless relative-mouse fallback.

## Next Engine Blocks

- Replace placeholder renderer with real Vulkan instance/surface/swapchain while keeping SDL debug draw for early tools.
- Add typed config schemas for renderer, server, input, and network.
- Add UDP transport socket backend on top of the packet primitives.
- Add movement validation helpers for Nemisis.
- Add first glTF metadata/import shim and renderer mesh-handle placeholders.
