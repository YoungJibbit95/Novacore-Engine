# AGENTS.md

## Project

NovaCore is a modular C++23 engine repository for a modern multiplayer FPS.

NovaCore owns reusable engine technology only:

* Core runtime
* Platform abstraction
* Input
* Math
* ECS
* Renderer
* Vulkan backend
* Asset registry and streaming foundation
* Networking primitives
* Dedicated server runtime
* Engine tests
* Engine documentation
* Engine tools

Game-specific content belongs in Nemisis, not NovaCore.

## Repository Boundary

NovaCore must not include:

* Nemisis headers
* Nemisis assets
* Weapon tuning
* Movement tuning
* Match rules
* Game UI screens
* Game-specific balance data

Dependency direction is always:

```text
Nemisis -> NovaCore
```

Never introduce a dependency from NovaCore back to Nemisis.

## Architecture Priorities

Prefer:

* Explicit ownership
* Stable handles instead of raw cross-system pointers
* Deterministic fixed-step simulation
* Server/headless compatibility
* Data-oriented layouts for high-frequency systems
* Small public headers
* Clear module boundaries
* Dependency-light smoke tests

Avoid:

* Actor-style inheritance
* Hidden global state
* Renderer dependencies in server code
* Gameplay logic inside engine systems
* Large abstractions without a real boundary
* Silent fallbacks that hide broken configuration

## Public vs Private Code

Public engine API lives under:

```text
engine/include/novacore
```

Private implementation lives under:

```text
engine/src
```

Rules:

* Keep public headers minimal.
* Do not expose Vulkan/SDL internals unless intentionally part of the public API.
* Do not include private headers from game-facing public headers.
* Prefer forward declarations where possible.

## Build Targets

Important targets:

* `novacore_engine`
* `Novacore::Engine`
* `novacore_server`
* `novacore_smoke_tests`

The server target must remain buildable without renderer, audio, or window dependencies.

## Preferred Build Commands

Use a dependency-light build first when touching core systems:

```powershell
cmake --preset windows-vs2022-no-deps
cmake --build --preset windows-vs2022-no-deps
ctest --test-dir build/windows-vs2022-no-deps -C Debug
```

For normal visible Windows development:

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --test-dir build/windows-msvc-debug -C Debug
```

For Vulkan/vcpkg work:

```powershell
cmake --preset windows-ninja-vcpkg-debug
cmake --build --preset windows-ninja-vcpkg-debug
ctest --test-dir build/windows-ninja-vcpkg-debug
```

If Vulkan SDK is not visible, do not remove Vulkan code. Keep graceful fallback paths and report diagnostics clearly.

## Testing Rules

After engine changes, run the most relevant tests.

Always run smoke tests after changes to:

* ECS
* Config
* Input
* Net packet primitives
* Asset registry
* glTF/GLB loading
* Mesh catalog
* Renderer backend selection
* Platform/headless behavior

Add or update tests when fixing bugs.

Do not accept changes that only “seem to work” without a relevant smoke, unit, or runtime validation path.

## Renderer Rules

The Vulkan renderer must remain explicit and debuggable.

Important upcoming priorities:

1. Swapchain resize/recreate.
2. GPU upload/resource ownership for extracted mesh data.
3. Device-local vertex/index buffers.
4. Camera matrices.
5. Depth buffer.
6. Mesh draw submission through `MeshCatalog`.
7. First in-world greybox rendering.

Rules:

* Do not let rendering own gameplay state.
* Do not read ECS directly during draw submission.
* Use extracted render data.
* Separate CPU asset data from GPU resources.
* Keep SDL debug renderer available as a fallback/tool path.
* Keep null/headless renderer support.

## Asset Rules

Assets flow through NovaCore systems:

```text
Manifest -> Registry -> CPU Load -> GPU Upload -> Runtime Handle
```

Rules:

* Gameplay code should use stable handles.
* Do not load authoring files directly from gameplay code.
* Preserve manifest/registry ownership boundaries.
* Missing assets should fail loudly but gracefully.
* Do not silently ignore invalid metadata.
* Keep source assets, cooked assets, and runtime resources conceptually separate.

## ECS Rules

Entities are identifiers only.

Components contain data.

Systems contain behavior.

Rules:

* Validate entity liveness.
* Preserve generation-based stale ID detection.
* Avoid scanning all entities when component views are possible.
* Keep transform, movement, and physics paths ready for future packed storage.
* Prefer deferred destruction during simulation ticks.

## Networking Rules

Networking is server-authoritative.

Rules:

* Use deterministic packet serialization.
* Keep packet readers strict.
* Reject overreads.
* Keep simulation tick numbers explicit.
* Do not let clients authoritatively decide damage, score, spawns, or match results.
* Loopback and future UDP paths should share protocol logic.

Important future priorities:

1. UDP transport.
2. Input command packets.
3. Snapshot generation.
4. Prediction buffer.
5. Reconciliation.
6. Interpolation.
7. Lag compensation.

## Server Rules

Dedicated server must remain headless.

Rules:

* No renderer dependency.
* No window dependency.
* No audio dependency.
* Shared simulation code between listen and dedicated server.
* Server owns authoritative movement, damage, objectives, spawns, and match state.

## Movement and Physics Rules

When implementing movement:

* Use fixed timestep simulation.
* Prefer capsule-based character controller.
* Keep camera presentation separate from physics state.
* Support ground detection, sliding, step-up, step-down, jumping, sprinting, crouching, and air control as explicit systems.
* Design movement for future client prediction and reconciliation.

Do not implement FPS movement as a camera-only transform hack.

## Documentation Rules

When changing architecture, update relevant docs.

Important docs:

* `docs/00_Repository_Architecture.md`
* `docs/01_Engine_Architecture.md`
* `docs/02_Renderer_Vulkan.md`
* `docs/03_ECS_Entities.md`
* `docs/08_Netcode.md`
* `docs/09_Server_Architecture.md`
* `docs/12_Asset_Pipeline_Streaming.md`
* `docs/13_Testing_Acceptance.md`
* `docs/14_IDE_And_Toolchain_Runbook.md`
* `docs/PROJECT_STATUS.md`

Keep `PROJECT_STATUS.md` focused on actual current state and next engine blocks.

Do not document aspirational features as implemented.

## Commit Discipline

For large work:

1. Inspect existing code first.
2. Make a small plan.
3. Implement one coherent block.
4. Build.
5. Run relevant tests.
6. Update docs/status if architecture changed.
7. Keep commits focused.

Do not mix unrelated renderer, ECS, netcode, and tooling changes in one large patch unless explicitly requested.

## Current Highest-Value Next Steps

Prioritize visible engine progress:

1. GPU upload for extracted GLB mesh data.
2. Vertex/index buffer ownership.
3. Mesh draw submission.
4. Depth buffer.
5. Camera matrix path.
6. Swapchain resize/recreate.
7. First in-world greybox.
8. Capsule character controller.
9. UDP transport.
10. Prediction/reconciliation.

Avoid spending too long expanding infrastructure without producing a playable or visible test scene.

## Safety Checks Before Finishing

Before reporting completion:

* Mention what files changed.
* Mention what build/test command was run.
* Mention any skipped tests and why.
* Mention known limitations.
* Do not claim Vulkan, networking, or physics features work unless verified.
