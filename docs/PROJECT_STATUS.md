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
* GLSL → SPIR-V compilation

Current renderer status:

* Vulkan initialization is operational.
* Basic graphics submission functions correctly.
* 3D world primitive submission is operational for early greybox visibility.
* 3D debug line submission is operational for in-world aim rays, surface normals, and future reconciliation probes.
* Imported CPU GLB mesh data can be registered as renderer-owned resources, uploaded to GPU memory, and drawn through the 3D camera/depth path.
* Frame submissions now carry stable mesh-resource handles instead of raw CPU mesh pointers.
* Registered mesh resources are tracked as pending, resident, failed, or deferred-destroy.
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

GPU mesh rendering has moved past the "next milestone" state. NovaCore now owns mesh-resource handles, queues Vulkan mesh uploads, reports pending/resident/failed/deferred stats, and defers GPU buffer destruction after frames-in-flight retire. The next renderer-heavy work is resize-safe swapchain recreation, validation/debug labels, lighting/material fallback controls, and stronger resource streaming policy.

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
