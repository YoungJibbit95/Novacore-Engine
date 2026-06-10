# 03 ECS Entities

## ECS Goal

NovaCore uses a custom Entity Component System designed for deterministic simulation, multiplayer replication, streaming, and cache-efficient execution.

The ECS is intentionally data-oriented. Entities are lightweight identifiers, components contain only data, and systems own all behavior. Inheritance between gameplay objects is avoided in favor of composition.

The design must scale from small gameplay scenes to large streamed worlds while remaining deterministic across client and server simulation.

## Core Principles

* Entities are identifiers only.
* Components contain data only.
* Systems contain behavior.
* Rendering, networking, and physics consume ECS state but do not own it.
* Components are stored independently of one another.
* High-frequency systems iterate contiguous component memory whenever practical.
* Entity lifetime is explicit and deterministic.

## Entity Identity

Every entity is represented by:

```cpp
struct EntityId
{
    uint32_t index;
    uint32_t generation;
};
```

The index selects a slot in the entity table.

The generation prevents stale references after destruction.

Destroyed slots may be reused only after incrementing their generation counter.

All APIs receiving an EntityId must validate both fields before access.

## Entity Lifecycle

Creation:

1. Reserve free slot.
2. Increment generation if recycled.
3. Mark slot alive.
4. Return EntityId.
5. Attach requested components.
6. Notify interested systems.

Destruction:

1. Mark pending destroy.
2. Queue destruction event.
3. Remove entity from runtime systems.
4. Remove attached components.
5. Increment generation.
6. Return slot to free list.

Destruction should be deferred until the end of the current simulation tick to avoid invalid iterators.

## Component Philosophy

Components contain state only.

Example:

* TransformComponent
* NameComponent
* RenderMeshComponent
* ColliderComponent
* HealthComponent

Behavior belongs exclusively inside systems.

Components should avoid owning external resources directly whenever possible.

## Transform Component

Transform stores:

* Local position
* Local rotation
* Local scale
* Parent entity
* World matrix
* Dirty flag

World transforms are recalculated only when local state or an ancestor changes.

Dirty propagation flows downward through the hierarchy.

## Component Storage

Early milestones may use sparse associative containers.

Long-term layout should prefer Structure of Arrays (SoA) for hot components.

Hot components:

* Transform
* Velocity
* CharacterController
* Physics state

Cold components:

* Name
* Editor metadata
* Debug information

Systems should iterate component arrays rather than scanning all entities.

## Component Registration

Adding a component:

```text
Entity
    ↓
Allocate Storage
    ↓
Construct Data
    ↓
Register With Views
```

Removing a component:

```text
Remove Request
    ↓
Destroy Data
    ↓
Compact Storage
    ↓
Update Views
```

Component removal must not invalidate unrelated entities.

## System Scheduling

Simulation executes in deterministic order:

1. Input ingestion
2. Network receive
3. Gameplay commands
4. Character controllers
5. Physics
6. Weapon simulation
7. Damage resolution
8. AI
9. Objective logic
10. Animation update
11. Snapshot extraction
12. Render extraction

Systems should never rely on undefined execution order.

## Thread Ownership

Gameplay writes occur on the simulation thread.

Render extraction reads immutable snapshots.

Background workers may prepare data but must never mutate ECS state concurrently without synchronization.

## Serialization

Two serialization paths exist.

### Save Data

Used for:

* Maps
* Prefabs
* Editor persistence

### Network Replication

Used for:

* Snapshots
* Delta compression
* Client synchronization

Replication is opt-in. Components are never serialized automatically.

## Streaming

World streaming divides static content into zones.

Static entities belong to zones.

Dynamic entities belong to the authoritative simulation and may reference streamed content without becoming zone-owned.

Streaming operations must preserve stable EntityIds whenever possible.

## Stable References

Cross-system references should prefer:

* EntityId
* AssetHandle
* ResourceHandle

Raw pointers between systems should be avoided.

## Render Extraction

Rendering never accesses ECS directly during draw submission.

Instead:

```text
ECS World
     ↓
Visibility Filtering
     ↓
RenderWorld Extraction
     ↓
Renderer
```

This separation allows multithreaded rendering preparation and prevents gameplay/render synchronization issues.

## Networking

Replicated entities carry explicit metadata describing:

* Replication frequency
* Ownership
* Prediction eligibility
* Interpolation mode

Network systems serialize only registered replicated components.

## Acceptance Criteria

The ECS milestone is complete when:

* Entities can be created and destroyed safely.
* Generation checks reject stale handles.
* Components attach and detach correctly.
* Deferred destruction prevents iterator invalidation.
* Transform hierarchies update correctly.
* Systems iterate only relevant component sets.
* Render extraction produces immutable presentation data.
* Network replication operates only on explicitly replicated components.
