# 14 IDE and Toolchain Runbook

## Goal

NovaCore should be buildable and debuggable from any modern CMake-aware IDE without restructuring the repository.

Developers should be able to clone the repository, select an appropriate preset, configure the project, and begin development with minimal manual setup.

The build system should remain reproducible across machines and operating systems.

## Supported Development Environments

Officially supported IDEs:

* Visual Studio 2022
* Visual Studio Code with CMake Tools
* CLion
* Rider C++
* Qt Creator

All environments should consume the same root `CMakeLists.txt` and, where supported, the same `CMakePresets.json`.

## Build Philosophy

The repository should support three development modes:

### Foundation Build

No external rendering dependencies.

Purpose:

* Engine development
* Smoke tests
* ECS
* Networking
* Configuration
* Server work

### Development Build

Automatically acquires common dependencies when practical.

Purpose:

* SDL debugging
* Editor development
* Local gameplay iteration

### Full Graphics Build

Enables Vulkan backend and graphics tooling.

Purpose:

* Renderer development
* GPU debugging
* Performance profiling
* Shader development

## CMake Presets

Presets should clearly communicate intent.

Recommended categories:

* Minimal development
* Windows MSVC
* Windows Ninja
* Windows + vcpkg
* Linux Clang
* macOS (future)
* Continuous integration

Preset names should remain stable to avoid breaking documentation and automation.

## Repository Structure

Expected layout:

```text
/
├── engine/
├── game/
├── server/
├── tools/
├── assets/
├── shaders/
├── docs/
├── tests/
├── CMakeLists.txt
└── CMakePresets.json
```

Every target should build from the repository root.

## Dependency Policy

Dependencies should be categorized as:

### Required

* C++ compiler
* CMake
* Build system
* Standard library

### Optional

* SDL3
* Vulkan SDK
* vcpkg
* glslc
* DXC
* Graphics debugging tools

Optional dependencies should disable related features gracefully rather than preventing compilation.

## Vulkan Environment

Renderer development requires:

* Vulkan loader
* Compatible graphics driver
* Vulkan SDK
* Shader compiler
* Vulkan headers and libraries

The engine should dynamically detect Vulkan availability at runtime and expose diagnostics when unavailable.

Successful compilation should not require Vulkan if the renderer is disabled.

## Shader Compilation

Development builds compile shaders during configuration or build.

Production builds should use cooked shader artifacts.

Shader failures must report:

* Source file
* Compiler diagnostics
* Include chain
* Compilation stage

Build systems should fail clearly instead of silently skipping invalid shaders.

## Local Development Workflow

Typical workflow:

```text
Clone Repository
        ↓
Select Preset
        ↓
Configure
        ↓
Build
        ↓
Run Smoke Tests
        ↓
Launch Game or Server
```

The same sequence should work regardless of IDE.

## Continuous Integration

CI should validate:

* Configure
* Compile
* Smoke tests
* Unit tests
* Shader compilation
* Headless server build

CI failures should block integration until resolved.

## Runtime Feature Detection

At startup NovaCore should detect and report:

* Active renderer backend
* Graphics adapter
* Vulkan availability
* Shader support
* Driver information
* CPU capabilities
* Build configuration

These diagnostics should aid troubleshooting without requiring recompilation.

## Debugging Support

Development builds should expose:

* Validation layers (when available)
* Debug names for GPU resources
* Logging categories
* Assertions
* GPU timing
* CPU profiling
* Memory diagnostics

Release builds should minimize overhead while preserving actionable crash information.

## Cross-Platform Expectations

All supported platforms should share:

* Build structure
* Asset layout
* CMake configuration
* Test suite
* Documentation

Platform-specific logic should remain isolated behind engine abstractions.

## Acceptance Criteria

The toolchain is considered production-ready for its milestone when:

* The repository configures successfully from the root directory.
* All documented presets build successfully.
* Engine, game, and dedicated server targets compile independently.
* Smoke tests execute without graphics dependencies.
* Vulkan support enables automatically when available and degrades gracefully when absent.
* Shader compilation failures provide clear diagnostics.
* New developers can build the project using documented steps without modifying repository structure.
* CI reproduces the documented local build process.
