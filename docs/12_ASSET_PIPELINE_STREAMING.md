# 12 Asset Pipeline & Streaming

## Pipeline Goal

NovaCore uses a deterministic asset pipeline designed for reproducible builds, asynchronous loading, efficient streaming, and clean separation between authoring tools and runtime systems.

Runtime code never consumes authoring files directly. Every asset passes through validation and cooking before becoming available to the engine.

The same source assets should always produce identical cooked outputs.

## Design Principles

* Source assets are never loaded directly during gameplay.
* Runtime assets are immutable once cooked.
* CPU-side data and GPU-side resources have separate lifetimes.
* Asset loading is asynchronous whenever possible.
* Stable handles are used instead of raw pointers.
* Missing assets fail loudly while allowing graceful fallback.
* Asset ownership is centralized inside NovaCore.

## Asset Lifecycle

Every asset progresses through the following stages:

```text id="8zfx3t"
Source File
      ↓
Import
      ↓
Validation
      ↓
Cooking
      ↓
Cooked Asset
      ↓
Asset Registry
      ↓
Runtime Load
      ↓
CPU Representation
      ↓
GPU Upload
      ↓
Resident Runtime Resource
```

Gameplay code interacts only with runtime handles.

## Source Assets

Permitted sources include:

* Original creations
* Commercial marketplace assets with compatible licenses
* Placeholder development assets
* Procedurally generated assets

Assets with uncertain ownership or redistribution rights must never enter the pipeline.

## Cooking Pipeline

Cooking performs:

* Coordinate normalization
* Scale normalization
* Material cleanup
* Metadata extraction
* Dependency discovery
* Mesh optimization
* Compression
* Validation
* Version stamping

Cooking should produce deterministic outputs independent of machine state.

## Blender Workflow

Recommended pipeline:

1. Import asset.
2. Verify licensing.
3. Normalize transforms.
4. Clean hierarchy.
5. Rename nodes consistently.
6. Configure sockets.
7. Configure collision helpers.
8. Export GLB.
9. Generate metadata.
10. Cook runtime asset.
11. Register in manifest.

Blender remains an authoring tool only and is not part of runtime loading.

## Runtime Asset Types

Supported categories include:

* Mesh
* Material
* Texture
* Shader
* Skeleton
* Animation
* Audio
* Font
* Gameplay data
* Navigation data
* World zones
* Particle definitions

Every asset category should expose a stable runtime handle.

## Asset Registry

The registry owns:

* Identifier lookup
* Manifest mounting
* Dependency graph
* Tag indexing
* Residency state
* Streamability metadata

The registry should remain independent of rendering APIs.

## Runtime Loading

Asset loading executes asynchronously:

```text id="dxj4dr"
Request Asset
      ↓
Lookup Registry
      ↓
Resolve Dependencies
      ↓
Disk Read
      ↓
Decode
      ↓
CPU Representation
      ↓
Queue GPU Upload
      ↓
Publish Runtime Handle
```

Only completed assets become visible to gameplay systems.

## GPU Upload Pipeline

Meshes and textures should follow:

```text id="s9fd4w"
CPU Asset
      ↓
Staging Buffer
      ↓
Transfer Queue
      ↓
Device Local Memory
      ↓
Resident GPU Resource
```

GPU upload completion should be synchronized explicitly before rendering.

## Residency Management

Assets transition through explicit residency states:

* Unloaded
* Queued
* Loading
* CPU Ready
* Upload Pending
* GPU Resident
* Evict Pending
* Unloaded

State transitions should remain observable through diagnostics.

## Dependency Management

Dependencies form a directed graph.

Example:

```text id="r8r92o"
Weapon Mesh
      ↓
Material
      ↓
Textures
      ↓
Shaders
```

Loading a parent asset should automatically request required dependencies.

Cycles must be rejected during validation.

## Streaming

Streaming prioritizes assets near gameplay relevance.

Priority factors include:

* Player position
* Camera position
* Active objectives
* Predicted movement
* Zone transitions
* Gameplay importance

Streaming requests should merge duplicate requests and retain the highest priority.

## Zone Streaming

Large worlds divide content into streaming zones.

```text id="zqjlwm"
Player Movement
        ↓
Zone Detection
        ↓
Predictive Preload
        ↓
Asset Requests
        ↓
GPU Residency
        ↓
Visible Scene
```

Leaving a zone schedules assets for deferred eviction after reference counts reach zero.

## Reference Counting

Runtime resources remain resident while referenced.

Eviction occurs only when:

* No active handles exist.
* No pending GPU work references the resource.
* Streaming policy permits removal.

Immediate destruction should be avoided.

## Hot Reload

Supported:

* Gameplay configuration
* Materials
* Development shaders
* Input mappings
* Weapon tuning

Restricted:

* Renderer backend
* Network protocol
* Skeleton topology
* Core engine configuration

Hot reload should preserve runtime stability and avoid invalidating active handles unexpectedly.

## Versioning

Cooked assets include:

* Format version
* Source revision
* Import settings hash
* Dependency hashes

Outdated cooked assets should trigger rebuilds instead of silent loading.

## Diagnostics

Developer tools should expose:

* Loaded asset count
* GPU residency
* Streaming queue length
* Pending uploads
* Reference counts
* Dependency graph
* Missing assets
* Streaming bandwidth
* Memory consumption

## Current Foundation

Current NovaCore functionality includes:

* AssetRecord
* AssetManifest
* AssetRegistry
* AssetStreamer
* GltfAssetMetadata
* GltfSceneInfo
* GltfMeshData
* MeshCatalog
* Stable mesh handles
* Request coalescing
* Priority queues
* GLB metadata parsing
* CPU-side mesh extraction

GPU uploads, residency tracking, asynchronous decoding, eviction, and transfer scheduling remain future milestones.

## Acceptance Criteria

The pipeline is considered production-ready for its milestone when:

* Source assets require successful cooking before runtime use.
* Stable runtime handles remain valid across subsystem boundaries.
* Dependencies resolve deterministically.
* Duplicate streaming requests coalesce correctly.
* CPU decoding and GPU upload are separated.
* Missing assets fall back gracefully while generating diagnostics.
* Streaming zones preload content before visible transitions.
* Residency tracking prevents premature eviction.
* Cooked outputs remain reproducible across builds.
