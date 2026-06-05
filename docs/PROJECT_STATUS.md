# NovaCore Project Status

## Current Foundation

- Public engine API under `engine/include/novacore`.
- CMake target `Novacore::Engine`.
- Optional dedicated server target.
- Core fixed-step application runtime.
- Lightweight math primitives.
- Custom ECS with entity lifetime and component storage.
- SDL3/headless platform window foundation.
- Window event polling now maintains a persistent `InputSnapshot` for action maps.
- Mouse movement is exposed as transient per-frame mouse axes.
- Vulkan/null renderer placeholder.
- Asset manifest, registry, and streaming request queue backbone under `novacore::assets`.
- Loopback net packet foundation.
- Engine-owned server defaults in `configs/net`.
- Dependency-free smoke test target.

## Added In Latest Block

- Added `AssetRecord`, `AssetManifest`, `AssetRegistry`, and `AssetStreamer`.
- Asset manifests now load from JSON through the existing NovaCore config parser.
- Asset registry supports lookup by id, tags, and streamable state.
- Asset streaming requests coalesce duplicate ids and pop by priority/age.
- Added Visual Studio 2022 no-deps CMake preset for IDE-friendly local bootstrapping.
- Smoke coverage now checks manifest loading, registry filters, and streaming request behavior.

## Next Engine Blocks

- Add raw mouse mode and cursor capture helpers for FPS camera control.
- Replace placeholder renderer with real Vulkan instance/surface/swapchain.
- Add typed config schemas for renderer, server, input, and network.
- Add packet bitstream writer/reader for real netcode.
- Add movement validation helpers for Nemisis.
- Add first glTF metadata/import shim and renderer mesh-handle placeholders.
