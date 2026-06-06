# 14 IDE And Toolchain Runbook

## Goal

NovaCore should be openable in common CMake-aware IDEs without reshaping the repo.

Supported IDE paths:

- Visual Studio 2022: open folder and select `windows-vs2022-no-deps` or `windows-msvc-debug`.
- Visual Studio Code: install CMake Tools, open folder, select a CMake preset.
- CLion: open folder, import CMake presets.
- Rider C++ / Qt Creator: open the root `CMakeLists.txt` or CMake presets where supported.

## Presets

`local-debug-no-deps`:

- Generator: Ninja.
- External deps disabled.
- Best for smoke tests and engine foundation work.

`windows-vs2022-no-deps`:

- Generator: Visual Studio 17 2022.
- External deps disabled.
- Best when Ninja is missing but Visual Studio Build Tools are installed.

`windows-msvc-debug`:

- Generator: Visual Studio 17 2022.
- Does not require Ninja.
- Does not require `VCPKG_ROOT`.
- Fetches and statically builds SDL3 automatically when no installed SDL3 package is found.
- Best default for VSCode/Visual Studio on Windows.

`windows-ninja-vcpkg-debug`:

- Generator: Ninja.
- Uses vcpkg toolchain.
- Intended for SDL3/Vulkan dependency work once vcpkg, Ninja, and Vulkan SDK are installed.

`windows-msvc-vcpkg-debug`:

- Generator: Visual Studio 17 2022.
- Uses vcpkg toolchain.
- Does not require Ninja.
- Intended for visible SDL debug renderer and future Vulkan work in VSCode/Visual Studio.

`linux-clang-debug`:

- Generator: Ninja.
- Uses clang++ and vcpkg.

## Minimal Local Build

```powershell
cmake --preset windows-vs2022-no-deps
cmake --build --preset windows-vs2022-no-deps
ctest --test-dir build/windows-vs2022-no-deps -C Debug
```

Or with Ninja:

```powershell
cmake --preset local-debug-no-deps
cmake --build --preset local-debug-no-deps
ctest --test-dir build/local-debug-no-deps
```

## Full Rendering Build

For the current SDL debug renderer, the default visible Windows dev build can fetch SDL3 automatically:

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
```

Current Vulkan state:

- Vulkan runtime/driver is installed on the local machine.
- Vulkan SDK is installed at `F:\VulkanSDK\1.4.350.0`.
- NovaCore can detect the runtime/device through its SDK-free dynamic probe.
- CMake finds Vulkan headers/libs and `glslc` when `VULKAN_SDK` and `PATH` are set for the active shell/IDE.
- The opt-in compiled Vulkan backend now creates a swapchain and debug triangle pipeline.

Required for full Vulkan/dependency work:

- Visual Studio 2022 Build Tools with Desktop development with C++.
- CMake 3.27+.
- Ninja.
- vcpkg.
- Vulkan SDK, with `VULKAN_SDK` set and `Bin`, `Include`, and `Lib` visible to the active shell/IDE.

Then:

```powershell
$env:VULKAN_SDK = "F:\VulkanSDK\1.4.350.0"
$env:PATH = "$env:VULKAN_SDK\Bin;$env:PATH"
cmake --preset windows-ninja-vcpkg-debug
cmake --build --preset windows-ninja-vcpkg-debug
```

Or without Ninja:

```powershell
cmake --preset windows-msvc-vcpkg-debug
cmake --build --preset windows-msvc-vcpkg-debug
```

## Current Environment Notes

In the current Codex shell:

- CMake is available.
- CLion's bundled MinGW/Ninja can build the current debug tree when explicitly added to PATH.
- SDL3 is fetched automatically for visible dev builds.
- Vulkan runtime is present and probed successfully.
- Vulkan SDK is visible when `VULKAN_SDK=F:\VulkanSDK\1.4.350.0` is exported in the shell.
- `cmake-build-codex-vulkan` has been verified with shader compilation, `novacore_smoke_tests`, and the compiled Vulkan backend path through Nemisis.

For real Vulkan backend work, reconfigure from a shell or IDE that sees `VULKAN_SDK`; otherwise NovaCore will still compile but fall back to SDL debug/null rendering.
