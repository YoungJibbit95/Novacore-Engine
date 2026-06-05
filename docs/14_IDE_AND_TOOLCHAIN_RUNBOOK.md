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

Install these for future full Vulkan/dependency work:

- Visual Studio 2022 Build Tools with Desktop development with C++.
- CMake 3.27+.
- Ninja.
- vcpkg.
- Vulkan SDK.

Then:

```powershell
cmake --preset windows-ninja-vcpkg-debug
cmake --build --preset windows-ninja-vcpkg-debug
```

Or without Ninja:

```powershell
cmake --preset windows-msvc-vcpkg-debug
cmake --build --preset windows-msvc-vcpkg-debug
```

## Current Environment Blocker

In the current Codex shell:

- CMake is available.
- Ninja is not in PATH.
- MSVC `cl` is not in PATH.
- `g++` is not in PATH.
- Visual Studio Build Tools are not visible to CMake in this shell.

So configure currently stops before compiling. The code is structured for IDE/toolchain pickup, but this machine still needs a build tool and compiler exposed to PATH or opened through a Visual Studio Developer shell.
