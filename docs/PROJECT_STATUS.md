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
- Loopback net packet foundation.
- Engine-owned server defaults in `configs/net`.
- Dependency-free smoke test target.

## Added In Latest Block

- Added controller activity tracking to `InputSystem`.
- Added persistent `Window::inputSnapshot()` state.
- SDL key, mouse, gamepad button, and gamepad axis events now update the window input snapshot.
- Added `InputControlKind::MouseAxis` and per-frame transient axis reset.
- SDL mouse motion now accumulates into mouse X/Y axis deltas each frame.
- Smoke coverage now checks keyboard/mouse and controller active-device transitions.
- Smoke coverage now checks transient mouse-axis accumulation/reset behavior.

## Next Engine Blocks

- Add raw mouse mode and cursor capture helpers for FPS camera control.
- Replace placeholder renderer with real Vulkan instance/surface/swapchain.
- Add typed config schemas for renderer, server, input, and network.
- Add packet bitstream writer/reader for real netcode.
- Add movement validation helpers for Nemisis.
