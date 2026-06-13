# 01 Engine Architecture

## Architecture Goal

NovaCore is a modular C++23 engine designed around deterministic simulation, clean subsystem boundaries, and long-term maintainability. Every subsystem must be independently testable and must be able to execute in client, dedicated server, or tooling environments without hidden dependencies.

The engine is designed for a modern multiplayer FPS and intentionally avoids assumptions inherited from existing commercial engines. Rendering, gameplay, networking, physics, and asset management are separate layers communicating through explicit interfaces.

NovaCore's long-term target is to be a production-grade foundation for an AAA-quality FPS. This does not mean copying the complexity of existing engines blindly; it means choosing architecture that can scale to high-end rendering, animation, physics, networking, UI, asset streaming, profiling, and tooling without a foundational rewrite.

## Core Design Principles

* Deterministic fixed-step simulation.
* Data-oriented subsystem design where practical.
* Engine code never depends on game-specific logic.
* Rendering is presentation only and never owns gameplay state.
* Physics is authoritative for movement and collision.
* Assets are referenced through stable handles rather than raw pointers.
* CPU-side and GPU-side resources are treated as separate lifetimes.
* Every subsystem documents its ownership, update frequency, and threading guarantees.
* Engine features are prioritized by real game value for Nemisis while remaining reusable and game-agnostic.
* Debug/profiling visibility is part of the architecture, not an afterthought.
* Temporary debug paths must not become permanent substitutes for production renderer, UI, font, material, animation, physics, or networking systems.

## Top-Level Modules

### Core

Logging, assertions, timing, configuration, allocators, fixed-step accumulator, threading primitives, future job system, startup and shutdown sequencing.

### Math

Vectors, matrices, quaternions, transforms, geometry helpers, interpolation utilities, SIMD optimizations where beneficial.

### IO

Filesystem abstraction, binary readers, configuration loading, asynchronous reads, file watching, hot reload notifications.

### Platform

Window creation, SDL integration, input devices, monitor handling, timing, dynamic libraries, platform abstraction.

### Render

Vulkan backend, resource management, swapchain handling, descriptor management, pipeline cache, frame graph, command scheduling, GPU uploads, UI composition and debug rendering.

### ECS

Entity allocation, component storage, system scheduling, serialization, hierarchy management, replication metadata and save/load support.

### Physics

Collision world, character controller, broadphase queries, raycasts, sweeps, trigger volumes and deterministic fixed-step updates.

Current foundation:

* `novacore::physics::PhysicsWorld` owns static collider definitions and movement bounds.
* `StaticCollider` carries gameplay surface metadata such as floor, wall, ramp, slide, ledge, cover, wall-run, and trigger.
* `CharacterQuery` and `CharacterResolveResult` provide the first deterministic character resolution API for game-side movement systems.
* `CharacterSweepQuery` and `CharacterSweepResult` provide the first swept displacement API for preventing high-speed character tunneling and reporting sweep diagnostics to game/debug/server validation layers.
* Wall probes expose normal, tangent, distance, and collider id so games can build wall-run and mantle behavior without hard-coding greybox geometry.

### Net

Reliable/unreliable transport, snapshot replication, prediction support, reconciliation, bandwidth accounting and serialization.

### Audio

Event-driven playback, mixer, streaming sources, spatial audio and backend abstraction.

### Game

Weapons, players, inventory, gameplay rules, AI, utility systems and match logic.

### Tools

Asset cooking, editor integration, import pipeline, reporting and diagnostics.

## Runtime Execution Model

Simulation executes independently of rendering.

Each frame follows this order:

1. Poll operating system events.
2. Update raw device state.
3. Build input actions.
4. Accumulate elapsed time.
5. Execute zero or more fixed simulation ticks.
6. Update asynchronous streaming tasks.
7. Build render data from current world state.
8. Execute rendering.
9. Present the completed frame.
10. Perform deferred destruction and housekeeping.

The renderer may execute at variable frequency while simulation always executes at a fixed timestep.

## Fixed Simulation Tick

Each fixed tick performs work in the following order:

1. Consume buffered player commands.
2. Execute network pre-processing.
3. Update gameplay systems.
4. Update character controllers.
5. Execute physics simulation.
6. Resolve triggers and interactions.
7. Execute replication and snapshot generation.
8. Publish authoritative transforms.

No rendering code may modify simulation state.

## ECS Model

Entities are lightweight identifiers.

Components contain data only.

Systems own behavior and iterate over matching component sets.

Transforms propagate through parent-child hierarchies using dirty flags so unchanged branches are never recomputed.

Large component pools should prefer contiguous memory layouts to improve cache efficiency.

## Asset Pipeline

Runtime asset flow is strictly separated:

Disk Asset
→ Importer
→ CPU Representation
→ Validation
→ GPU Upload Queue
→ Device Resources
→ Runtime Handle

GPU resources are never accessed directly from importer code.

Asynchronous loading is encouraged whenever dependencies permit.

## Thread Ownership

Main Thread:

* Platform events
* ECS updates
* Gameplay
* Physics
* Render submission

Worker Threads:

* Asset decoding
* Compression
* Background loading
* Cooking
* Future job execution

GPU:

* Rendering
* Compute workloads
* Transfer operations

No gameplay code may access GPU objects directly.

## Configuration Model

Configuration is divided into:

* Boot configuration
* User configuration
* Gameplay tuning
* Developer overrides

Gameplay configuration supports hot reload while platform-critical settings require restart unless explicitly documented.

## Logging

Every message contains:

* Timestamp
* Severity
* Category
* Thread identifier

Subsystem categories include:

* core
* platform
* render
* ecs
* physics
* net
* audio
* game
* tools

## Memory Strategy

Early milestones use standard containers.

Future milestones introduce:

* Frame allocator
* Linear allocators
* Resource arenas
* Pool allocators
* Job-local scratch memory

Transient render allocations should never survive a frame boundary.

## Job System

Future worker scheduling should execute independent tasks such as:

* Asset decompression
* Visibility preparation
* Animation evaluation
* Navigation updates
* Background cooking

Tasks must declare dependencies explicitly to avoid hidden synchronization.

## Acceptance Criteria

The architecture is complete when:

* Client and dedicated server share the same engine library.
* Simulation executes correctly without any renderer present.
* Rendering can be disabled without affecting gameplay.
* Assets load asynchronously through stable handles.
* Fixed-step gameplay remains deterministic regardless of render framerate.
* New subsystems can be added without changing process ownership or module boundaries.
