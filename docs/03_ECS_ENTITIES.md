# 03 ECS Entities

## ECS Goal

The ECS is custom and data-oriented enough for replication, streaming, and high-frequency gameplay. It should avoid actor-style inheritance and keep entity identity stable across systems.

## Entity Identity

`EntityId` contains:

- Index.
- Generation.

An entity is valid only if its generation matches the current slot generation. Destroyed slots can be reused after generation increments.

## Initial Components

M1 components:

- `NameComponent`
- `TransformComponent`

`TransformComponent` uses `novacore::math` primitives rather than ECS-owned vector types. That keeps math reusable for renderer, physics, networking, and game code.

Future components:

- `RenderMeshComponent`
- `ColliderComponent`
- `CharacterControllerComponent`
- `PlayerControllerComponent`
- `WeaponOwnerComponent`
- `NetworkReplicatedComponent`
- `TeamComponent`
- `HealthComponent`
- `ObjectiveComponent`

## Component Stores

M1 uses simple type-indexed sparse maps. Later optimization can move high-frequency components into packed arrays.

Rules:

- Component APIs must check entity liveness.
- Removing an entity removes all attached components.
- Systems should iterate specific component views, not every entity.
- Replicated components carry explicit replication metadata.

## System Scheduling

System order matters in FPS gameplay:

1. Input command ingestion.
2. Network receive.
3. Movement prediction/simulation.
4. Character state update.
5. Weapon simulation.
6. Damage and events.
7. Objective/mode rules.
8. Network snapshot extraction.
9. Render extraction.

M1 does not implement the scheduler fully, but the architecture should leave space for it.

## Serialization

Entity serialization has two paths:

- Save/load path for editor and map data.
- Network path for snapshots and deltas.

Network serialization must never blindly serialize every component. It uses explicit replicated state.

## Streaming

Large-map support requires entity ownership by zones:

- Static map entities belong to stream zones.
- Dynamic entities belong to the authoritative world and may reference zones.
- Stream-out must not destroy network-critical entities unexpectedly.

## Spawn/Despawn

Spawn flow:

1. Reserve entity.
2. Attach components.
3. Register in systems.
4. Emit spawn event.

Despawn flow:

1. Mark entity pending destroy.
2. Emit despawn event.
3. Remove from systems.
4. Remove components.
5. Recycle slot.

## Acceptance

ECS M1 is acceptable when:

- Entities can be created and destroyed.
- Liveness checks detect stale IDs.
- Components attach and detach.
- Destroy removes components.
- Game can create a camera entity.







