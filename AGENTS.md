# AGENTS.md

## Primary Directive

NovaCore is a modular C++23 engine repository for a modern multiplayer FPS.

Work implementation-first. Maximize useful code, tests, validation, and durable engine progress per session. Minimize conversational output.

At task start:
- maximum one sentence
- no bullets unless the user asks for a plan
- immediately inspect files and implement

Spend tokens in this order:

1. Inspect existing code.
2. Modify or add code.
3. Build and run relevant tests.
4. Fix failures.
5. Update documentation only when needed.
6. Report briefly.

Do not spend tokens on obvious explanations, repeated summaries, tutorials, or speculative architecture discussion unless explicitly requested.

## Agent Operating Mode

Primary behavior: produce a large, coherent implementation patch per task while keeping chat minimal.

Hard rules:

* Spend most context on repository inspection, code edits, tests, and fixes.
* Do not stop after a small cleanup unless the user explicitly requested a small cleanup.
* Do not choose the smallest safe slice by default.
* If the task is broad, implement the largest coherent slice that can be safely built and tested in the session.
* Prefer 5-15 meaningful file edits over 1-3 tiny edits when the task scope allows it.
* Prefer complete gameplay/render/UI/asset systems over cosmetic cleanup.
* Remove dead code only when it is part of enabling a larger feature or requested directly.
* Do not add filler, dead code, duplicate systems, artificial abstractions, or line-count padding.
* Do not optimize for raw line count. Optimize for meaningful shipped functionality.
* Do not narrate routine edits.
* Do not summarize every file after changing it.
* Stay quiet during work unless blocked, a command fails, or user input is required.

At task start, output at most 2 short bullets:
* implementation target
* expected validation

During work:
* continue implementing until the largest coherent slice is complete
* when one file is changed, immediately inspect adjacent systems and finish the connected feature path
* do not stop at cleanup if a playable/rendered/testable improvement is possible
* run build/tests, fix failures, and keep going if time/context remains

Final response must be short and contain only:
* files changed
* build/test/playable validation commands run
* result
* real blockers or skipped validation, if any

## Silence Policy

During implementation, do not send status updates.

Allowed messages:
1. One initial message before tool use, maximum one sentence.
2. One blocking message if user input is required.
3. One final report after build/test.

Forbidden during implementation:
* "I am checking..."
* "I will now..."
* "Next I will..."
* "I found..."
* "I am going to..."
* progress summaries
* per-file summaries
* reasoning updates
* Kanban/GitHub/project-board narration
* repeated validation plans

Use tool calls silently instead of describing tool calls.

## Work Output Standard

A successful session should usually produce one substantial outcome, such as:

* a playable Dev Range improvement
* a complete movement mechanic slice
* a complete weapon/HUD interaction slice
* a renderable asset or scene pipeline improvement
* a test-covered gameplay system
* a real UI/settings/loadout flow
* a bug fix plus regression test plus cleanup of the affected path

Avoid ending a session with only:

* dead-code deletion
* comment/doc edits
* tiny cosmetic changes
* one isolated helper function
* refactors that do not unlock visible or testable behavior

Small patches are acceptable only when the user explicitly asks for a small fix or the repository state makes a larger safe change impossible.

## Project Boundary

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

## Engineering Quality Bar

NovaCore is the long-term technical foundation for a game targeting high-quality graphics, performance, networking, tooling, and FPS feel.

Treat this as an engineering standard:

* build durable systems instead of throwaway prototypes
* keep APIs reusable from Nemisis without importing Nemisis concepts
* prefer explicit ownership and stable handles
* preserve deterministic fixed-step simulation where gameplay or networking may depend on it
* keep server/headless compatibility intact
* keep public headers small and dependency-light
* avoid hidden global state and silent fallbacks
* avoid large abstractions unless they protect a real boundary
* avoid prototype-only architecture that requires a future rewrite

## Public and Private Code

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
* Prefer forward declarations where practical.
* Do not expose Vulkan, SDL, or other backend internals unless intentionally part of the public API.
* Do not include private headers from game-facing public headers.

## Build Targets

Important targets:

* `novacore_engine`
* `Novacore::Engine`
* `novacore_server`
* `novacore_smoke_tests`

The server target must remain buildable without renderer, audio, or window dependencies.

## Build and Test Commands

Use a dependency-light build first when touching core systems:

```powershell
cmake --preset windows-vs2022-no-deps
cmake --build --preset windows-vs2022-no-deps
ctest --test-dir build/windows-vs2022-no-deps -C Debug --output-on-failure
```

For normal visible Windows development:

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug --config Debug
ctest --test-dir build/windows-msvc-debug -C Debug --output-on-failure
```

For Vulkan/vcpkg work:

```powershell
cmake --preset windows-ninja-vcpkg-debug
cmake --build --preset windows-ninja-vcpkg-debug
ctest --test-dir build/windows-ninja-vcpkg-debug --output-on-failure
```

If Vulkan SDK is unavailable, do not remove Vulkan code. Keep graceful fallback paths and report the missing dependency clearly.

## Testing Rules

After engine changes, run the most relevant build and tests available.

Always add or update tests when fixing bugs or adding reusable behavior.

Run smoke or targeted tests after changes to:

* ECS
* Config
* Input
* Net packet primitives
* Asset registry
* glTF/GLB loading
* Mesh catalog
* Renderer backend selection
* Platform/headless behavior
* Server/headless runtime behavior

Do not claim a feature works unless it was built and validated, or clearly state that validation was skipped.

## Renderer Rules

The renderer must remain explicit, debuggable, and suitable for Vulkan-first growth.

Current high-value renderer priorities:

1. GPU upload for extracted GLB mesh data.
2. Vertex/index buffer ownership.
3. Mesh draw submission through `MeshCatalog`.
4. Depth buffer.
5. Camera matrix path.
6. Swapchain resize/recreate.
7. First in-world greybox rendering.

Rules:

* Do not let rendering own gameplay state.
* Do not read ECS directly during draw submission.
* Use extracted render data.
* Separate CPU asset data from GPU resources.
* Keep SDL debug renderer available as a fallback/tool path.
* Keep null/headless renderer support.
* Grow material, lighting, resource residency, shader, and draw-submission systems as engine systems, not game-specific hacks.
* Grow UI/text infrastructure toward font rendering, layout, batching, clipping, scaling, and input navigation instead of relying on debug text paths.

## Asset Rules

Assets flow through NovaCore systems:

```text
Manifest -> Registry -> CPU Load -> GPU Upload -> Runtime Handle
```

Rules:

* Gameplay code should use stable handles.
* Do not load authoring files directly from gameplay code.
* Preserve manifest and registry ownership boundaries.
* Missing assets should fail loudly but gracefully.
* Invalid metadata must not be silently ignored.
* Keep source assets, cooked assets, metadata, CPU assets, GPU resources, and runtime handles conceptually separate.

## ECS Rules

Entities are identifiers only.

Components contain data.

Systems contain behavior.

Rules:

* Validate entity liveness.
* Preserve generation-based stale ID detection.
* Prefer component views over scanning all entities.
* Prefer deferred destruction during simulation ticks.
* Keep transform, movement, and physics paths ready for future packed storage.
* Avoid actor-style inheritance.

## Networking Rules

Networking is server-authoritative.

Rules:

* Use deterministic packet serialization.
* Keep packet readers strict.
* Reject overreads.
* Keep simulation tick numbers explicit.
* Do not let clients authoritatively decide damage, score, spawns, objectives, or match results.
* Loopback and future UDP paths should share protocol logic.

High-value networking priorities:

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

Movement and physics engine code must support deterministic FPS gameplay.

Rules:

* Use fixed timestep simulation.
* Prefer capsule-based character controller foundations.
* Keep camera presentation separate from physics state.
* Support ground detection, sliding, step-up, step-down, jumping, sprinting, crouching, and air control as explicit systems when implemented in engine code.
* Design movement for future client prediction, reconciliation, replay, and server validation.
* Keep character controller code deterministic, debuggable, and suitable for later capsule sweeps, moving platforms, contact manifolds, and server replay checks.
* Do not implement FPS movement as a camera-only transform hack.

Game-specific movement tuning belongs in Nemisis.

## Documentation Rules

Update documentation only when architecture, public APIs, build/test flow, or actual project status changes.

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

Keep `PROJECT_STATUS.md` focused on actual current state and next concrete engine blocks.

Do not document aspirational features as implemented.

## Commit Discipline

For large work:

1. Inspect existing code first.
2. Implement one coherent block.
3. Build.
4. Run relevant tests.
5. Fix failures.
6. Update docs/status if required.

Do not mix unrelated renderer, ECS, netcode, tooling, and documentation changes in one patch unless explicitly requested.

Do not commit, push, or update GitHub Projects unless explicitly requested.

## Current Highest-Value Next Steps

Prefer visible, playable, or reusable engine progress:

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

Avoid spending long sessions expanding infrastructure without producing a validated engine capability.
