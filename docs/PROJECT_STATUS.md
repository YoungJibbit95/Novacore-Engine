# NovaCore Project Status

## Overview

NovaCore is a modular C++23 engine targeting high-performance multiplayer FPS games.

The current milestone establishes the engine foundation, rendering bootstrap, asset pipeline, networking primitives, and testing infrastructure required for later gameplay development.

The project emphasizes deterministic simulation, explicit resource ownership, and clean separation between engine systems.

---

# Current Foundation

## Core Runtime

Implemented:

* Public engine API
* Shared engine library
* Fixed-step application runtime
* Time management
* Logging
* Configuration system
* Lightweight math library
* Headless execution support

The runtime can execute independently of rendering and serves both client and dedicated server builds.

---

## Entity Component System

Implemented:

* Stable `EntityId`
* Generation-based lifetime validation
* Component attachment and removal
* Basic component storage
* Transform foundation

Planned:

* Packed SoA storage
* Hierarchical transforms
* Dirty propagation
* Deferred destruction
* Replication metadata
* ECS scheduler

---

## Platform Layer

Implemented:

* SDL3 window management
* Event polling
* Input snapshots
* Relative mouse mode
* Headless fallback

Current input handling supports FPS-style camera capture and action mapping.

---

## Renderer

Implemented:

* SDL debug renderer
* Vulkan runtime probing
* Vulkan instance creation
* Surface creation
* Physical device selection
* Logical device creation
* Queue acquisition
* Swapchain
* Image views
* Render pass
* Depth image, memory, and image view
* Framebuffers
* Command buffers
* Synchronization primitives
* Debug graphics pipeline
* Debug triangle rendering
* World box graphics pipeline
* World line graphics pipeline
* World mesh graphics pipeline
* UI rect graphics pipeline
* UI line graphics pipeline
* Bitmap debug text through UI rect primitives
* Camera push constants
* Per-frame world lighting push constants
* Depth-tested 3D box rendering
* Depth-tested 3D debug line rendering
* Device-local vertex buffers
* Device-local index buffers
* Staging uploads through one-time command buffers
* Indexed GLB mesh draw submission
* Renderer-owned `MeshResourceHandle` registry
* Vulkan mesh upload queue
* GPU mesh residency stats
* Deferred GPU mesh destruction after frames-in-flight retire
* Resize/out-of-date aware Vulkan swapchain recreation
* Backend frame stats for submitted/skipped frames, swapchain size/readiness, recreate count, and last world draw counts
* Backend frame stats for last UI rect/line/text command counts
* GLSL → SPIR-V compilation

Current renderer status:

* Vulkan initialization is operational.
* Basic graphics submission functions correctly.
* 3D world primitive submission is operational for early greybox visibility.
* 3D debug line submission is operational for in-world aim rays, surface normals, and future reconciliation probes.
* Imported CPU GLB mesh data can be registered as renderer-owned resources, uploaded to GPU memory, and drawn through the 3D camera/depth path.
* Frame submissions now carry stable mesh-resource handles instead of raw CPU mesh pointers.
* Registered mesh resources are tracked as pending, resident, failed, or deferred-destroy.
* Swapchain acquire/present out-of-date states recreate dependent Vulkan resources instead of leaving the client in a stale presentation path.
* Renderer clients can query backend frame stats for in-game debug UI and smoke-test diagnostics, including whether Vulkan UI primitives are actually being submitted.
* Vulkan-required launch profiles can disable silent SDL debug fallback.
* Debug rendering remains available as a fallback path.

Not yet implemented:

* Frustum culling
* Material binding
* Directional/IBL lighting beyond compact per-frame sun-direction and ambient controls
* Texture/material resource residency
* Background resource upload budgets beyond the current mesh upload queue

---

## Asset Pipeline

Implemented:

* Asset manifests
* Asset registry
* Asset records
* Asset lookup
* Dependency metadata
* Streaming request queue
* glTF metadata parsing
* GLB scene inspection
* CPU-side mesh extraction
* Stable mesh handles
* Renderer handoff through `MeshResourceHandle`
* First mesh resource registration, lookup, release, upload, and deferred-destroy lifecycle

Current pipeline can register extracted mesh data with the renderer, queue Vulkan uploads, draw resident resources, and release GPU buffers through deferred destruction.

Future work includes:

* Asynchronous decoding
* Cross-zone reference counting
* Texture residency tracking
* Streaming eviction policies
* Background loading

---

## Physics And Character Movement

Implemented:

* `novacore::physics::PhysicsWorld`
* Static AABB collider registration and lookup
* Movement bounds clamping
* Character position resolution for early FPS controller work
* Surface tagging for floor, wall, ramp, cover, slide, ledge, wall-run, and trigger surfaces
* Floor snap with configurable snap-down distance
* Low step-up resolution
* Ramp height sampling and ramp normals
* Blocking ledge, wall, and cover side resolution
* Wall-run surface contact reporting
* Wall normal and wall tangent reporting for gameplay movement systems
* Wall proximity probing around the character footprint
* Mantle probing for cover/ledge tops, including approach distance, obstacle point, target foot position, surface normal, and clearance validation
* Standable ledge-top resolution after a successful mantle target snap

Current physics status:

* The first engine-owned character-controller foundation is operational.
* The system is intentionally lightweight and deterministic for early FPS movement tests.
* Nemisis consumes this layer for greybox collision, wall-run panel detection, mantle candidate detection, slide-ramp classification, and gameplay debug telemetry.
* This is not a full rigid-body engine yet. The next physics step is capsule sweeps, richer depenetration ordering, moving platforms, and server-side replay validation.

---

## Networking

Implemented:

* Loopback transport
* Packet primitives
* Packet serialization
* Packet deserialization
* Simulation tick types
* Packet sequencing foundation

Current networking supports deterministic local communication suitable for early multiplayer development.

Future milestones include:

* UDP transport
* Snapshot replication
* Prediction
* Reconciliation
* Delta compression
* Lag compensation

---

## Dedicated Server

Implemented:

* Shared engine runtime
* Dedicated executable
* Headless operation
* Server defaults
* Shared simulation architecture

Dedicated and listen server paths are intended to execute identical gameplay logic.

---

## Testing

Implemented:

* Smoke tests
* Packet serialization tests
* Entity lifetime tests
* Fixed-step tests
* Asset manifest tests
* Mesh extraction tests
* Character controller surface tests
* Vulkan runtime probe tests
* Relative mouse tests

Future additions:

* Replay tests
* Prediction validation
* Movement verification
* Performance benchmarks
* Automated gameplay tests

---

# Current Milestone Assessment

The project has completed most foundational work required before true gameplay implementation.

The renderer, asset system, ECS, networking primitives, and testing framework now provide sufficient infrastructure for vertical feature development.

The next objective should be visible gameplay rather than additional engine scaffolding.

---

# Recommended Development Priority

## Priority 1 — GPU Mesh Rendering

Implement:

* Renderer-owned mesh handles
* Upload queues
* Deferred destruction
* Descriptor-backed object constants
* Material resource binding

Target:

Promote the current synchronous GLB mesh upload path into a durable resource manager.

---

## Priority 2 — Character Controller

Implement:

* Capsule sweeps
* Sliding
* Step-up
* Step-down
* Ground detection
* Gravity
* Sprinting
* Jumping
* Air control

Target:

Create responsive FPS movement independent of networking.

---

## Priority 3 — Greybox World

Implement:

* Static level loading
* Collision geometry
* Camera controls
* Debug overlays

Target:

Walk through a simple playable environment.

---

## Priority 4 — Asset Residency

Implement:

* GPU uploads
* Streaming queues
* Reference counting
* Deferred destruction
* Residency states

Target:

Prepare the engine for larger worlds and dynamic loading.

---

## Priority 5 — Networking

Build on the existing packet foundation by adding:

* UDP sockets
* Client commands
* Snapshot generation
* Prediction
* Reconciliation
* Interpolation

Target:

Synchronize multiple players in a shared world.

---

## Priority 6 — Combat

Implement:

* Weapon firing
* Hitscan validation
* Damage
* Ammo
* Reloading
* Server authority

Target:

Complete the first playable multiplayer interaction.

---

# Latest Readiness Note

Vulkan now draws the same UI/debug primitive stream that the game feeds through `UiCanvas`: screen-space rectangles, lines, and small bitmap debug text. This closes the early grey-screen menu gap where the Vulkan backend cleared/presented correctly but did not yet consume game-side menu primitives. Backend frame stats now expose last UI rect/line/text counts so client UIs can diagnose missing HUD/menu submissions without dropping into the debugger.

Validation:

* `cmake --build --preset windows-msvc-debug --config Debug`
* `ctest --test-dir build\windows-msvc-debug -C Debug --output-on-failure`
* Nemisis runtime smoke verified the new Vulkan UI pipelines with live Main Menu and Dev Range UI submissions.

---

# Previous Readiness Note

GPU mesh rendering has moved past the "next milestone" state. NovaCore now owns mesh-resource handles, queues Vulkan mesh uploads, reports pending/resident/failed/deferred stats, defers GPU buffer destruction after frames-in-flight retire, recreates swapchain-dependent resources after out-of-date presentation states, and exposes backend frame stats to game debug UIs. The next renderer-heavy work is validation/debug labels, resize stress coverage, lighting/material fallback controls, and stronger resource streaming policy.

Physics has also moved from planning into the first usable engine layer. NovaCore now exposes a deterministic static-collider `PhysicsWorld` and character resolve/probe API with ramp, ledge, cover, slide, wall-run, and mantle surface metadata. Nemisis uses it immediately in the Dev Range for visible wall-run panels, mantle ledge targeting, and richer KCC debug feedback.

---

# Current Readiness

| Area                  | Status                 |
| --------------------- | ---------------------- |
| Core Runtime          | ✔ Mature foundation    |
| ECS                   | ✔ Foundation complete  |
| Platform              | ✔ Ready                |
| Vulkan Initialization | ✔ Ready                |
| GPU Mesh Rendering    | ✔ Ready                |
| Asset Registry        | ✔ Ready                |
| Streaming             | ◑ Skeleton complete    |
| Networking            | ◑ Foundation complete  |
| Prediction            | ✖ Planned              |
| Dedicated Server      | ✔ Foundation complete  |
| Character Controller  | Foundation complete     |
| Physics               | Foundation complete     |
| Gameplay              | ✖ Planned              |
| Testing               | ✔ Strong foundation    |

---

# Immediate Recommendation

The highest-value next milestone is now:

**"Turn the static-collider KCC foundation into a richer FPS physics layer: capsule sweeps, moving-platform contacts, server replay validation, and renderer-visible debug probes."**

GPU mesh rendering, Vulkan UI primitives, mantle probes, and the first character-controller foundation are now in place. The engine can keep building toward visible gameplay while tightening determinism and diagnostics under the hood.
