# NovaCore Project Status

## Current Foundation

- Public engine API under `engine/include/novacore`.
- CMake target `Novacore::Engine`.
- Optional dedicated server target.
- Core fixed-step application runtime.
- Lightweight math primitives.
- Custom ECS with entity lifetime and component storage.
- SDL3/headless platform window foundation.
- Vulkan/null renderer placeholder.
- Loopback net packet foundation.
- Engine-owned server defaults in `configs/net`.
- Dependency-free smoke test target.

## Added In Latest Block

- `ConfigDocument` for parsed dotted-key config values.
- Lightweight JSON config parser for objects, arrays, strings, numbers, booleans, and nulls.
- `ConfigRegistry` for watched JSON config files.
- Config reload events wired through polling file snapshots.
- Smoke coverage for config parsing and registry reload.

## Next Engine Blocks

- Replace placeholder renderer with real Vulkan instance/surface/swapchain.
- Add typed config schemas for renderer, server, input, and network.
- Add packet bitstream writer/reader for real netcode.
- Add movement validation helpers for Nemisis.
