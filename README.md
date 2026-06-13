# NovaCore Engine

<p align="center">
  <img src="https://img.shields.io/github/last-commit/YoungJibbit95/Novacore-Engine?style=for-the-badge&color=8b5cf6" />
  <img src="https://img.shields.io/github/repo-size/YoungJibbit95/Novacore-Engine?style=for-the-badge&color=6366f1" />
  <img src="https://img.shields.io/github/issues/YoungJibbit95/Novacore-Engine?style=for-the-badge&color=ec4899" />
  <img src="https://komarev.com/ghpvc/?username=YoungJibbit95-Novacore-Engine&label=repo%20views&color=8b5cf6&style=for-the-badge" />
</p>

NovaCore is a modern **C++23**, **Vulkan-first** game engine focused on deterministic simulation, high-performance rendering, and server-authoritative multiplayer.

The engine is being developed as the reusable technology foundation for **Project Nemisis**, but is intentionally designed to remain independent of any game-specific content or gameplay logic.

## Design Goals

NovaCore is built around a few core principles:

* Modern explicit graphics APIs (Vulkan-first)
* Deterministic fixed-step simulation
* Server-authoritative multiplayer architecture
* Data-oriented Entity Component System
* Clean separation between engine and game code
* Modular subsystems with minimal dependencies
* Headless dedicated server support
* Asynchronous asset pipeline and streaming
* Long-term maintainability over short-term convenience

## Current Status

The project currently includes a functional engine foundation featuring:

* C++23 runtime and modular architecture
* Fixed-step application loop
* Lightweight math library
* Custom ECS with stable entity lifetime
* SDL3 platform layer and input system
* Relative mouse support for FPS controls
* Vulkan runtime detection
* Vulkan instance, device, swapchain, render pass, synchronization, and debug triangle pipeline
* SDL debug renderer for immediate tooling and UI work
* Asset manifests and registry
* glTF metadata parsing
* GLB scene inspection
* CPU-side mesh extraction
* Mesh handle registration
* Packet serialization primitives
* Loopback networking foundation
* Dedicated server framework
* Dependency-light smoke tests

The renderer is currently transitioning from bootstrap infrastructure toward full GPU mesh rendering.

## Planned Near-Term Milestones

The next major engine goals are:

* GPU upload pipeline
* Device-local vertex and index buffers
* Mesh rendering through `MeshCatalog`
* Camera matrices and depth buffering
* Swapchain resize and recreation
* First navigable greybox world
* Capsule-based FPS character controller
* UDP networking backend
* Client prediction and reconciliation
* Snapshot replication
* Asset residency and streaming

## Repository Structure

```text
engine/     Core engine implementation
server/     Dedicated server executable
configs/    Engine runtime defaults
tests/      Smoke and validation tests
shaders/    Vulkan shader sources
tools/      Future editor and asset tooling
docs/       Architecture and subsystem documentation
```

NovaCore intentionally contains **engine technology only**.

Game-specific assets, gameplay tuning, weapons, movement configuration, UI, and match rules belong in the consuming game project.

## Key Technologies

* Modern C++23
* Vulkan
* SDL3
* CMake
* Custom ECS
* Headless dedicated server support
* JSON-based configuration
* glTF / GLB asset pipeline
* Deterministic packet serialization
* Fixed-step simulation

## Building

### Visual Studio (recommended)

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
```

### Visual Studio + vcpkg

```powershell
cmake --preset windows-msvc-vcpkg-debug
cmake --build --preset windows-msvc-vcpkg-debug
```

### Ninja + vcpkg

```powershell
cmake --preset windows-ninja-vcpkg-debug
cmake --build --preset windows-ninja-vcpkg-debug
```

### Minimal dependency build

```powershell
cmake --preset local-debug-no-deps
cmake --build --preset local-debug-no-deps
ctest --test-dir build/local-debug-no-deps
```

## Requirements

Minimum development requirements:

* C++23 compiler
* CMake 3.27+
* Visual Studio 2022 or Clang/GCC
* Vulkan SDK (for Vulkan development)
* SDL3 (automatically fetched for supported presets where applicable)

## Testing

NovaCore includes a dependency-light smoke test suite covering core engine functionality, including:

* Entity lifetime
* Fixed-step timing
* Configuration loading
* Packet serialization
* Asset manifests
* Asset registry
* GLB metadata and mesh extraction
* Mesh handle registration
* Vulkan runtime probing
* Platform input behavior

Additional replay, networking, renderer, and gameplay validation will expand alongside engine development.

## Documentation

The `docs/` directory contains detailed technical documentation for:

* Repository architecture
* Engine architecture
* Vulkan renderer
* ECS
* Networking
* Server runtime
* Asset pipeline
* Testing strategy
* Toolchain and build workflow

The `PROJECT_STATUS.md` document tracks implemented systems and upcoming milestones.

## Philosophy

NovaCore prioritizes correctness, explicit ownership, and maintainability over unnecessary abstraction.

Engine systems are designed to be independently testable, reusable across projects, and capable of running in both client and headless server environments.

## License

See the `LICENSE` file for licensing information.
