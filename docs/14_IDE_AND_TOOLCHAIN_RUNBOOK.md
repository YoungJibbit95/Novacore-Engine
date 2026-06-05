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

- Generator: Ninja.
- Uses vcpkg toolchain.
- Intended for SDL3/Vulkan work once vcpkg and Vulkan SDK are installed.

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

Install:

- Visual Studio 2022 Build Tools with Desktop development with C++.
- CMake 3.27+.
- Ninja.
- vcpkg.
- Vulkan SDK.

Then:

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
```

## Current Environment Blocker

In the current Codex shell:

- CMake is available.
- Ninja is not in PATH.
- MSVC `cl` is not in PATH.
- `g++` is not in PATH.

So configure currently stops before compiling. The code is structured for IDE/toolchain pickup, but this machine still needs a build tool and compiler exposed to PATH or opened through a Visual Studio Developer shell.
