# 13 Testing Acceptance

## Testing Goal

The project needs tests that protect feel and networking, not only code correctness. Movement, recoil, and replication must be measurable.

## Build Tests

Targets:

- Windows MSVC.
- Linux Clang.
- macOS later.

Checks:

- Configure.
- Build engine.
- Build game.
- Build server.
- Shader compile.

## Unit Tests

Initial unit areas:

- Entity ID generation/liveness.
- Component attach/remove.
- Fixed timestep accumulator.
- Config parsing.
- Packet sequence math.
- Input curve mapping.

The first dependency-free test target is `novacore_smoke_tests`. It validates entity lifetime, fixed-step accumulation, loopback packet flow, packet bitstream roundtrips, headless relative-mouse fallback, input action state transitions, file change tracking, config parsing, config registry reloads, glTF metadata loading, GLB scene-info loading, GLB triangle mesh extraction, Vulkan runtime probe stability, and mesh-handle registration.

Debug renderer smoke coverage remains runtime-oriented for now. The SDL debug backend currently accepts clear frames, rectangles, lines, and bitmap text so Nemisis can build early visual test surfaces while Vulkan mesh rendering is still coming online.

## Movement Replay Tests

Movement tests record:

- Input command stream.
- Start state.
- Expected final state.
- Tolerance.

Scenarios:

- Sprint.
- Slide.
- Slide jump.
- Air control.
- Mantle.
- Controller analog movement.
- Advanced tech timing.

## Netcode Tests

Scenarios:

- Latency.
- Jitter.
- Packet loss.
- Packet reorder.
- Prediction correction.
- Snapshot interpolation.
- Lag compensated hit.
- Packet primitive roundtrip.
- Overread rejection.

## Gameplay Tests

Scenarios:

- AR TTK.
- SMG close-range TTK.
- Shotgun pellet distribution.
- Sidearm fallback.
- Reload timing.
- Objective scoring.
- Spawn safety.

## Renderer Tests

Scenarios:

- Swapchain create/recreate.
- Clear frame.
- Mesh render.
- Mesh handle registration.
- glTF metadata validation.
- GLB/text glTF scene-info validation.
- GLB CPU primitive, vertex, and index extraction.
- SDK-free Vulkan runtime probe stability.
- Missing material fallback.
- Shader compile failure diagnostics.
- Frame timing output.

## Acceptance Dashboard

The future debug UI should show:

- FPS.
- CPU frame time.
- GPU frame time.
- Server tick time.
- RTT.
- Packet loss.
- Reconciliation error.
- Entity count.
- Draw call count.

## Acceptance

The first implementation is accepted when:

- Docs exist and match the agreed criteria.
- Project skeleton is clean.
- Game/server targets exist.
- Engine has core loop, ECS, input, renderer stub, net skeleton.
- Missing external C++ tools are documented.







