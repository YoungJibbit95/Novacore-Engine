# 09 Server Architecture

## Server Goal

The NovaCore server is the authoritative owner of simulation state and executes all gameplay-critical logic independently of rendering.

Movement validation, physics, combat resolution, spawning, objectives, replication, and match progression are executed exclusively on the server.

Dedicated servers and listen servers share identical simulation code and differ only in process layout.

## Design Principles

* Server simulation is authoritative.
* Simulation executes on deterministic fixed ticks.
* Rendering is completely optional.
* Client connections never bypass server validation.
* Listen servers reuse the dedicated server implementation.
* Gameplay code is independent from transport implementation.
* Networking, simulation, and presentation remain separate layers.

## Server Types

### Dedicated Server

Runs as a standalone executable.

Owns:

* Simulation
* Networking
* Match state
* Replication
* Logging

Does not initialize:

* Renderer
* Audio
* Window system
* Client UI

Dedicated servers should remain deployable on headless operating systems.

### Listen Server

Runs inside the client process.

Uses:

* The same simulation code
* The same world update pipeline
* The same snapshot generation
* The same validation logic

The local player communicates through an in-process or loopback transport while remaining subject to identical server authority.

## Server World Ownership

The server exclusively owns:

* ECS world
* Player sessions
* Physics simulation
* Character controllers
* Weapons
* Damage
* Objectives
* Spawn system
* AI
* Match timers
* Snapshot history

The server never owns:

* Camera transforms
* HUD state
* Screen-space effects
* Local audio playback
* Client-only animations

## Startup Sequence

Server startup follows:

```text id="3ejaxq"
Load Configuration
        ↓
Initialize Core Systems
        ↓
Initialize Networking
        ↓
Load Map
        ↓
Create ECS World
        ↓
Spawn Static Entities
        ↓
Initialize Match Rules
        ↓
Begin Accepting Clients
```

Simulation begins only after initialization completes successfully.

## Client Connection Flow

```text id="tebnfd"
Connection Request
        ↓
Protocol Validation
        ↓
Version Check
        ↓
Create Session
        ↓
Assign Player ID
        ↓
Spawn Player
        ↓
Transmit Initial Snapshot
        ↓
Enter Simulation
```

Handshake failures must terminate cleanly without affecting active sessions.

## Fixed Tick Pipeline

Every simulation tick executes in deterministic order:

1. Receive packets.
2. Decode player commands.
3. Validate command sequence.
4. Execute gameplay systems.
5. Update character controllers.
6. Simulate physics.
7. Resolve combat.
8. Process objectives.
9. Update AI.
10. Advance match timers.
11. Generate replication state.
12. Build snapshots.
13. Send outgoing packets.

No rendering work occurs inside the server tick.

## Player Sessions

Each connected player maintains:

* Connection state
* Authentication status
* Last acknowledged tick
* Prediction baseline
* Ping estimate
* Packet statistics
* Controlled entity
* Replication filters

Disconnecting a player should cleanly release associated simulation resources.

## Authority Rules

Server-authoritative:

* Position
* Velocity
* Physics state
* Health
* Damage
* Ammo
* Weapon ownership
* Objectives
* Team state
* Spawn/despawn
* Match progression

Client-predicted:

* Local camera
* Immediate movement feedback
* Weapon recoil
* UI state
* Cosmetic effects

Any disagreement is resolved in favor of the server.

## Snapshot Generation

Snapshot construction follows:

```text id="clo6iz"
Authoritative World
         ↓
Replication Filtering
         ↓
Priority Sorting
         ↓
Delta Compression
         ↓
Packet Serialization
         ↓
Transmission
```

Only replicated state is serialized.

## Simulation Isolation

Gameplay systems operate exclusively on server-owned state.

Networking serializes data into packets.

Rendering never accesses authoritative simulation directly.

This separation allows dedicated servers to run without graphics dependencies.

## Match Management

The server controls:

* Warmup
* Match start
* Overtime
* Respawns
* Victory conditions
* Round transitions
* Map resets

Clients receive replicated match state but never determine outcomes.

## Configuration

Engine-owned runtime configuration includes:

* Tick rate
* Snapshot frequency
* Network ports
* Timeout values
* Packet simulation settings
* Default player limits

Game-specific balance values remain external data and support hot reload where appropriate.

## Diagnostics

The server exposes runtime statistics including:

* Tick duration
* Connected clients
* Average ping
* Packet loss
* Snapshot bandwidth
* Entity count
* Physics time
* Replication time
* Memory usage

Long-running simulations should maintain stable timings without renderer involvement.

## Scalability

Future versions should support:

* Worker-thread job execution
* Parallel snapshot generation
* Streaming worlds
* AI offloading
* Large entity counts
* Multiple game modes

Scalability improvements must preserve deterministic gameplay.

## Acceptance Criteria

The server architecture is complete for its milestone when:

* Dedicated servers execute the full simulation loop without graphics initialization.
* Listen servers reuse identical gameplay code.
* Player sessions survive connect and disconnect correctly.
* All authoritative gameplay executes exclusively on the server.
* Fixed-tick execution remains deterministic.
* Snapshot generation operates independently of rendering.
* Match progression continues correctly regardless of client frame rate.
* Networking, gameplay, physics, and replication remain cleanly separated.
