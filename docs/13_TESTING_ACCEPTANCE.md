# 13 Testing and Acceptance

NovaCore tests protect reusable engine behavior. Nemisis owns game acceptance.
This document distinguishes executed CPU coverage from requirements that still
need dedicated tests, a usable graphics environment, or game-side validation.
The function-level inventory and cleanup decisions are in
[TEST_AUDIT.md](recovery/TEST_AUDIT.md).

## Ownership

| NovaCore contracts | Nemisis acceptance |
| --- | --- |
| Fixed-step runtime, entity/component lifetime, configuration and input edges | Movement feel, coyote/jump-buffer tuning, camera response |
| Configurable collision/KCC mechanisms, support motion and mantle geometry | Ability activation, movement-mode policy and authored traversal |
| Strict packets, sequences, snapshot encoding and generic reconciliation plans | Game commands, combat authority, player snapshots and correction policy |
| Standards-correct glTF/GLB data and generic mesh/skeleton/animation behavior | Canonical cooked content, required game sockets, weapon mounts and artistic poses |
| GPU resource ownership, draw submission and headless runtime | TTK, pellet distribution, recoil, ammo/reload/swap, damage/falloff |
| Generic sessions, ticks, snapshots and queries | Objectives, score, teams, spawn safety, respawn and complete-match flow |

Weapon validation, hit outcomes and damage application are game policy even when
implemented using engine raycasts, history or packet primitives. Engine fixture
names such as `weapons.ar_01` are generic parser input, not balance acceptance.
See the [Nemisis recovery milestone](https://github.com/YoungJibbit95/Nemisis/blob/main/docs/recovery/ROADMAP.md)
for the two-player match criteria. Do not expand NovaCore to satisfy an obsolete
engine-completeness or debug-dashboard checklist.

## Current automated coverage

`NOVACORE_BUILD_TESTS=ON` registers five CTest suites, each calling its test
functions unconditionally. They require no SDL3 or Vulkan development dependency.

| Suite | Verified CPU scope |
| --- | --- |
| `novacore_smoke_tests` | Entity lifetime, fixed-step accumulation, loopback, bounded sequence/interpolation buffers, bitstream primitives, input edges, file/config reload, manifest/registry/streaming, static glTF transforms and metadata, mesh handles/dynamic updates, material validation, KCC basics and sandbox orchestration |
| `novacore_animation_runtime_tests` | Quaternion/TRS math, hierarchy and sockets, validation, clip sampling, blending/masks, root motion, crossfades and layers |
| `novacore_gltf_animation_import_tests` | GLB skin/joint/channel/material import, mesh-local skinned vertices, inverse binds, external text-glTF buffers and malformed references |
| `novacore_netcode_tests` | Packet envelopes/checksums/bounds, sequence rollover/ACK windows, canonical snapshots/deltas/baselines, fractional interpolation, bounded extrapolation, reconciliation plans and tick estimation |
| `novacore_physics_kcc_tests` | Deterministic contact manifolds, rounded sweeps, step clearance, slopes, moving supports/jump inheritance, landing/snap replay and configurable coyote/jump-buffer mechanisms |

The runtime probe checks loader/summary behavior; it does not create a drawable
Vulkan backend. Swapchain extent validation checks a CPU recreation decision,
not an actual resize. CPU mesh-resource counters do not prove GPU residency or
deferred GPU destruction. Animation runtime tests and imported skin tests protect
different boundaries and must not be collapsed merely because they share terms.

## Reproducing the dependency-light baseline

Use one configure/build/test preset name throughout:

```text
cmake --preset ci-headless-linux
cmake --build --preset ci-headless-linux --parallel 2
ctest --preset ci-headless-linux
```

Use `ci-headless-windows` for Windows/MSVC or `ci-headless` for local Ninja.
The CI workflow builds the engine, dedicated server, sandbox and all five suites
on Ubuntu/Clang and Windows/MSVC. It explicitly does not claim graphics coverage.
Record the actual compiler, revision, commands and results in the recovery log.
After an engine runtime/API change, build and test Nemisis against that exact
revision; a documentation/test-only change does not require moving the game pin.

## Requirements and current coverage gaps

Retain these reusable requirements; do not describe them as verified merely
because a similarly named smoke passes:

* ECS generation/stale-ID rejection, deferred destruction and ordinary ECS
  transform propagation need explicit coverage beyond the current lifetime test.
* Configuration reload errors must preserve a useful diagnostic and valid state;
  current registry coverage demonstrates successful reload only.
* Protocol unit cases reject malformed data, duplicate/stale ACKs and bad
  baselines. They do not simulate an impaired transport or a two-client match.
* A reconciliation plan is not an executed command replay, smooth presentation
  correction, or recovery from real packet loss. Validate those consumer seams
  when the product uses them.
* KCC tests preserve reusable collision/support/snap mechanisms and deterministic
  replay. Nemisis separately owns movement tuning and ability acceptance.
* Asset tests preserve hierarchy/TRS, buffer/index validation, joint data and
  stable handles. Cook determinism, unload behavior and real pipeline integration
  require their own evidence. Zone-preload queue population is already tested.
* Headless sandbox scenarios exercise ticks and orchestration. Building the server
  target alone does not establish live session/disconnect or match behavior.

Add coverage when a real boundary or regression requires it. Do not introduce a
new test framework, generic fixture layer, performance gate or telemetry panel
merely to fill this list.

## Graphics validation boundary

With a suitable SDK, windowing environment and usable GPU, validate device and
swapchain creation/recreation, indexed upload/draw, depth, world/UI submission,
dynamic mesh updates, synchronized deferred destruction, resize/minimize and
shutdown. Preserve explicit resource ownership and useful failure diagnostics.
An unavailable backend is environment-blocked, not a successful graphics test.

Keep null/headless behavior separately testable. KCC grounding, steps, slopes,
contacts and mantle geometry are physics contracts, not renderer acceptance.
Performance claims need a recorded workload and environment; CPU counters alone
do not establish GPU frame time, memory residency or visual correctness.

## Regression and deletion policy

Preserve fixed-step, lifetime, packet, glTF, animation, GPU-lifecycle and collision
contracts. For a serious bug, add a focused behavioral regression when coverage
is missing. Refactoring should preserve behavior through existing coverage.

Before deleting or substantially changing a test, record its old invariant,
why it is obsolete/redundant, remaining coverage and removal risk in TEST_AUDIT.
Keep uncertain tests until their protection is understood. Repeated setup or
similar subsystem names alone do not establish redundant behavior.

Acceptance requires relevant builds/tests to pass, known failures and unexecuted
checks to be explicit, and documentation to match actual behavior. Never disable
failures or add silent fallbacks merely to obtain a green result.
