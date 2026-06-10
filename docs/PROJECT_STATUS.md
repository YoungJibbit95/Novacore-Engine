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
* Framebuffers
* Command buffers
* Synchronization primitives
* Debug graphics pipeline
* Debug triangle rendering
* GLSL → SPIR-V compilation

Current renderer status:

* Vulkan initialization is operational.
* Basic graphics submission functions correctly.
* Debug rendering remains available as a fallback path.

Not yet implemented:

* GPU mesh uploads
* Camera matrices
* Indexed mesh rendering
* Depth buffering
* Frustum culling
* Material binding
* Lighting
* Resource streaming

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

Current pipeline stops at CPU asset preparation.

Future work includes:

* Asynchronous decoding
* GPU upload
* Residency tracking
* Reference counting
* Streaming eviction
* Background loading

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

* Device-local vertex buffers
* Index buffers
* Staging uploads
* Camera matrices
* Uniform buffers
* Mesh draw submission

Target:

Render imported GLB geometry inside a movable 3D camera.

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

# Current Readiness

| Area                  | Status                 |
| --------------------- | ---------------------- |
| Core Runtime          | ✔ Mature foundation    |
| ECS                   | ✔ Foundation complete  |
| Platform              | ✔ Ready                |
| Vulkan Initialization | ✔ Ready                |
| GPU Mesh Rendering    | ◑ Next major milestone |
| Asset Registry        | ✔ Ready                |
| Streaming             | ◑ Skeleton complete    |
| Networking            | ◑ Foundation complete  |
| Prediction            | ✖ Planned              |
| Dedicated Server      | ✔ Foundation complete  |
| Character Controller  | ✖ Planned              |
| Physics               | ✖ Planned              |
| Gameplay              | ✖ Planned              |
| Testing               | ✔ Strong foundation    |

---

# Immediate Recommendation

The highest-value next milestone is:

**“Load a GLB mesh, upload it to GPU memory, render it with a movable FPS camera and depth testing, then implement a capsule-based character controller inside that greybox scene.”**

Once that milestone is complete, the existing ECS, asset pipeline, server architecture, and networking foundation can begin supporting a genuinely playable FPS rather than continuing to expand infrastructure in isolation.
