# NovaCore Engine

A ground-up C++23 Vulkan-first FPS engine. Fast, efficient, and built from the ground up for modern graphics APIs. Engine for Project Nemisis, a fps game planned to release in 2027.

## Features

- **Modern C++23** - Latest C++ standard features
- **Vulkan-first** - Direct GPU control via Vulkan
- **Custom ECS** - Entity Component System for efficient entity management
- **Dedicated Server** - Built-in dedicated server support
- **Cross-platform** - Windows and Linux first, macOS via MoltenVK
- **SDL3 Integration** - Modern window and input handling
- **Asset Pipeline Backbone** - Manifest, registry, and streaming request queue foundation
- **No Bloat** - Minimal dependencies, maximum control

## Project Structure

- `engine/` - Core engine library
- `server/` - Dedicated server executable
- `configs/` - Engine/server runtime defaults
- `tests/` - Dependency-free engine smoke tests
- `shaders/` - Vulkan shader files
- `tools/` - Development tools
- `docs/` - Engine documentation

## Build Requirements

- C++23 compatible compiler (MSVC, Clang, GCC)
- CMake 3.27+
- Ninja
- Vulkan SDK
- SDL3 (optional, for window/input support)

## Build Instructions

### With full dependencies (Vulkan, SDL3, vcpkg):

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
```

### Without external dependencies:

```powershell
cmake --preset local-debug-no-deps
cmake --build --preset local-debug-no-deps
ctest --test-dir build/local-debug-no-deps
```

Current foundation primitives include fixed-step timing, math types, input action mapping, loopback net packets, and polling-based file change tracking for future hot reload.

IDE-friendly presets are available for Ninja and Visual Studio 2022. See `docs/14_IDE_AND_TOOLCHAIN_RUNBOOK.md`.

Asset pipeline foundation includes JSON manifests, asset registry lookup, streamable tags, dependencies, and priority streaming requests. See `docs/12_ASSET_PIPELINE_STREAMING.md`.

## Documentation

See `docs/` for detailed architecture and implementation guides.

## License

See LICENSE file for details.
