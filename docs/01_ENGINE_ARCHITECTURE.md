# 01 Engine Architecture

## Architecture Goal

The engine is a modular C++23 runtime shared by the game client and server. It must support a modern FPS without inheriting assumptions from an existing engine. The first rule is simple: systems must be independently testable and able to run in either client, server, or tools contexts.

## Top-Level Modules

- `Core`: logging, asserts, time, config, deterministic tick loop, memory hooks, jobs.
- `Platform`: window, input, filesystem, dynamic libraries, OS timing.
- `Render`: Vulkan backend, render graph, resources, frame scheduling, UI composition.
- `ECS`: entities, components, systems, lifetime, serialization, replication metadata.
- `Physics`: custom character controller, collision queries, triggers.
- `Net`: transport, channels, reliability, snapshots, prediction helpers.
- `Audio`: miniaudio backend and engine-level event/mixer layer.
- `Game`: player, weapons, utility, modes, match rules.
- `Tools`: C# pipeline/editor tools and command line asset cooking.

## Process Layout

Client process:

- Owns window, renderer, audio, local input, prediction, interpolation, UI.
- Can host a listen server by creating a server world in-process.
- Runs client presentation at variable render rate.
- Sends fixed-rate input commands to server.

Dedicated server process:

- Owns authoritative world state.
- Runs fixed simulation at 60Hz.
- Has no renderer or audio.
- Accepts LAN/direct-connect clients first.

Tools process:

- Uses C#/.NET where iteration speed matters.
- Imports/cooks assets.
- Generates data reports.
- Eventually drives editor workflows.

## Runtime Loop

The engine loop separates fixed update from variable rendering:

1. Poll platform events.
2. Collect input for current frame.
3. Accumulate elapsed time.
4. Run zero or more fixed ticks at 60Hz.
5. Run client prediction and game systems that need fixed time.
6. Render one frame if a renderer exists.
7. Sleep/yield when appropriate.

Server builds run only fixed ticks. Client builds run both fixed and variable work.

## Build Model

The root project contains:

- `novacore_engine`: static or shared engine library.
- `novacore_server`: dedicated server executable.

The engine must not depend on game-specific code. Game systems may depend on engine modules.

## Config Model

Configuration is split into:

- Boot config: renderer backend, window mode, network ports.
- User config: input binds, sensitivity, display settings.
- Gameplay data: movement, weapons, utility, modes.

The final engine should hot-reload gameplay data but not silently hot-reload boot-critical platform settings.

## Logging and Diagnostics

Logging categories:

- `core`
- `platform`
- `render`
- `ecs`
- `net`
- `game`
- `tools`

All logs include severity and category. Server logs must be readable without a GUI.

## Memory and Jobs

M0/M1 uses standard containers. Later phases add:

- Frame allocator for transient render/game memory.
- Linear allocators for network packet serialization.
- Job system with worker threads.
- Lock-free or low-lock queues only where measured.

## Asset Manager

Asset manager responsibilities:

- Stable handles.
- Async load requests.
- Hot reload for data.
- Cooked asset lookup.
- Dependency tracking.

The first skeleton only defines the direction; real cooking begins in M3.

## Acceptance

The architecture is acceptable when:

- Client and server link the same engine library.
- Server can run without platform window/render systems.
- Game can create a world, renderer, and input layer.
- Future modules can be added without changing the root process split.







