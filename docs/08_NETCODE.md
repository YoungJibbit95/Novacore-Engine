# 08 Netcode

## Netcode Goal

NovaCore uses a server-authoritative networking model optimized for fast-paced multiplayer FPS gameplay.

The server owns all authoritative simulation. Clients are responsible only for input generation, local prediction, and presentation.

The networking layer is designed around deterministic simulation ticks, low latency, prediction, reconciliation, and scalable snapshot replication.

## Core Principles

* Server simulation is authoritative.
* Clients never decide game outcomes.
* All gameplay executes on fixed simulation ticks.
* Rendering is independent of networking.
* Prediction exists only for locally controlled entities.
* Remote entities are reconstructed from snapshots.
* Corrections should be visually smooth while remaining simulation-correct.

## Simulation Timeline

```text
Client Input
      ↓
Command Packet
      ↓
Server Tick
      ↓
Gameplay Simulation
      ↓
Physics
      ↓
Snapshot Generation
      ↓
Client Receive
      ↓
Reconciliation
      ↓
Interpolation
      ↓
Rendering
```

Simulation is always tick-driven and never frame-driven.

## Network Tick Rates

| System                | Frequency |
| --------------------- | --------- |
| Server Simulation     | 60 Hz     |
| Client Input Commands | 60 Hz     |
| Snapshot Transmission | 30 Hz     |
| Rendering             | Variable  |
| Interpolation Buffer  | 50–100 ms |

Future tuning may allow configurable snapshot frequencies without affecting simulation.

## Transport Layer

Networking is based on UDP.

Each packet includes:

* Packet sequence number
* Latest acknowledged sequence
* Acknowledgement bitfield
* Packet type
* Tick number
* Payload

Packet fragmentation should be avoided whenever practical.

Large payloads should instead be divided across multiple simulation updates.

## Packet Channels

Separate logical channels exist:

### Reliable

* Match events
* Spawn/despawn
* Inventory
* Chat
* Objective state

### Unreliable

* Snapshots
* Movement
* Animation state
* Effects

Reliable traffic must not block snapshot delivery.

## Input Commands

Each client sends deterministic input commands:

```text
Simulation Tick
Forward Movement
Side Movement
View Rotation
Jump
Sprint
Crouch
Fire
Reload
Interact
Weapon Slot
```

Inputs are timestamped using simulation ticks rather than wall-clock time.

Commands should remain compact for bandwidth efficiency.

## Prediction

Clients immediately simulate:

* Character movement
* Camera motion
* Weapon recoil
* Local animations
* UI feedback

Clients never authoritatively predict:

* Damage
* Kills
* Match results
* Objective captures
* Inventory ownership

## Reconciliation

Each authoritative update contains:

* Server tick
* Last acknowledged input tick
* Position
* Velocity
* Rotation
* Gameplay state

Client correction pipeline:

```text
Receive Snapshot
       ↓
Locate Matching Prediction
       ↓
Compare State
       ↓
Apply Correction
       ↓
Replay Pending Inputs
       ↓
Resume Prediction
```

Minor corrections should be visually smoothed while preserving simulation accuracy.

## Snapshot System

Snapshots contain only replicated state.

Typical contents:

* Entity identifier
* Transform
* Velocity
* Animation state
* Health
* Weapon state
* Team
* Replicated gameplay variables

Transient effects should not consume replication bandwidth.

## Delta Compression

Snapshots are transmitted relative to acknowledged baselines.

Only changed replicated fields should be serialized.

Unchanged values should consume zero bandwidth whenever possible.

## Relevance Filtering

Before snapshot construction:

```text
World State
      ↓
Visibility / Distance Check
      ↓
Priority Sorting
      ↓
Bandwidth Budget
      ↓
Snapshot Serialization
```

Priority order:

1. Local player correction
2. Nearby enemies
3. Nearby teammates
4. Active projectiles
5. Objectives
6. Interactables
7. Cosmetic entities

## Interpolation

Remote entities maintain a snapshot history buffer.

Rendering samples between historical snapshots using interpolation.

When packets arrive late:

* Short extrapolation is permitted.
* Long extrapolation should be avoided.
* Large discontinuities should trigger teleport handling.

Interpolation should never affect authoritative simulation.

## Lag Compensation

Hitscan evaluation follows:

```text
Fire Command
      ↓
Receive On Server
      ↓
Validate Weapon State
      ↓
Rewind Historical Hitboxes
      ↓
Perform Raycast
      ↓
Restore Current State
      ↓
Apply Damage
```

Historical transforms should be retained long enough to cover expected network latency.

## Entity Replication

Replicated entities declare:

* Network identifier
* Replication frequency
* Ownership
* Prediction capability
* Interpolation mode

Replication remains opt-in and component-driven.

## Packet Simulation

Development builds expose configurable:

* Latency
* Packet loss
* Duplication
* Reordering
* Jitter

Simulation should function identically for loopback and real UDP transport.

## Diagnostics

Debug overlays should expose:

* Current ping
* Packet loss
* Interpolation delay
* Prediction error
* Correction count
* Snapshot size
* Bandwidth usage
* Tick drift

## Current Foundation

Current implementation includes:

* Loopback transport
* Deterministic packet primitives
* PacketWriter
* PacketReader
* SimulationTick
* PacketSequence
* NetworkRate
* Little-endian serialization
* Overread protection
* Round-trip serialization tests

Future milestones extend this foundation with prediction buffers, snapshot replication, delta compression, and live UDP networking.

## Acceptance Criteria

Networking is considered production-ready for its milestone when:

* Client prediction remains responsive under latency.
* Packet loss does not permanently desynchronize clients.
* Reconciliation remains stable and measurable.
* Snapshot interpolation produces smooth remote motion.
* Lag compensation validates hits consistently.
* Dedicated and listen servers execute identical simulation code.
* All networking logic operates on deterministic simulation ticks rather than render frames.
