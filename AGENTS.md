# AGENTS.md

NovaCore is a reusable C++23 engine consumed by Nemisis. New engine features
require a current game blocker or concrete stability need.

## Ownership

Dependency direction is always `Nemisis -> NovaCore`. NovaCore owns runtime and
fixed-step simulation, configuration/logging, platform/input, math, ECS, rendering
and GPU resources, generic assets/import/streaming, physics/KCC mechanics,
animation runtime, networking primitives, and headless server runtime.

Nemisis owns movement policy/tuning, weapons/damage/TTK, teams/matches/scoring,
spawn rules, gameplay UI, maps, asset mounts, input tuning, and presentation.
Do not add Nemisis headers, content, balance data, or per-asset axis repairs.

Public API lives in `engine/include/novacore`; implementation in `engine/src`.
Keep headers dependency-light, use forward declarations when practical, and do
not leak private headers or Vulkan/SDL internals into game-facing APIs. Preserve
working APIs unless a current need justifies changing them. No new render graph.

## Contracts to preserve

* Deterministic fixed-step simulation, explicit ticks, headless/dedicated-server
  support without window/audio/GPU dependencies, and shared simulation behavior.
* Explicit ownership and stable handles; generation/liveness validation, deferred
  entity destruction, data components, behavior systems, and no actor inheritance.
* Camera presentation separate from authoritative physics; capsule sweeps,
  contacts, grounding, slopes, step/snap, moving supports, and mantle geometry.
  Game-specific ability policy stays in Nemisis.
* GPU synchronization, destruction order, deferred release, dynamic meshes, and
  swapchain recreation. Rendering consumes extracted data, not ECS/game state.
  Keep CPU assets separate from GPU resources and retain null/tool render paths.
* Standards-correct glTF/GLB hierarchy, TRS/matrix, accessors, skins, and animation.
  Assets flow manifest -> registry -> CPU load -> GPU upload -> stable handle.
  Keep source/cooked/metadata distinct; invalid required data must report errors.
* Strict deterministic packet serialization, bounds checks, sequence invariants,
  and existing prediction/reconciliation. Clients never decide authoritative
  damage, score, spawns, objectives, or match results. Transport paths share logic.

After engine implementation changes, build/test Nemisis against the exact changed
engine revision and record it. Tests for TTK, weapon tuning, objective scoring,
and spawn safety belong in Nemisis.
## Recovery workflow

Prioritize stability, simplification, playability, and shipping. Prefer small to
medium, coherent, validated changes with one responsibility per PR. Simplifying
code and deleting unnecessary complexity are valuable outcomes. Historical
feature lists are reference, not the active backlog.

At every session start check Git status, branches/remotes, open PRs, and
`docs/recovery/PROGRESS.md` plus `ROADMAP.md` when present. Preserve uncommitted
user changes and isolate work in a separate worktree when needed.

* Preserve behavior during refactoring; avoid full rewrites.
* Reuse existing structures before adding files/classes or interfaces.
* Add abstractions only for real, current ownership boundaries. Do not create
  managers, registries, frameworks, service locators, event buses, or generic
  interfaces for one-off problems.
* Add features only for a current product need or concrete stability blocker.
* After implementation, search for obsolete code, duplicate paths, workarounds,
  tests, and documentation. Verify replacements before removing old behavior.
* Do not combine broad renderer, KCC, networking, assets, and gameplay changes.
* Give concise progress updates for meaningful findings; keep routine edits quiet.

## Validation and Git discipline

Start with the dependency-light build:

```powershell
cmake --preset local-debug-no-deps
cmake --build --preset local-debug-no-deps
ctest --test-dir build/local-debug-no-deps --output-on-failure
```

On MSVC use `windows-vs2022-no-deps` and CTest with `-C Debug`. For visible
Windows validation use `windows-msvc-debug`; use `windows-ninja-vcpkg-debug`
only when its toolchain is available. Missing GPU/SDK support is not a successful
Vulkan test. Preserve graphics code when the environment cannot validate it.

Run relevant existing tests and the available full build. Add/update tests for
new behavior or real bugs, not merely to justify an AI-generated workaround.
Refactors retain behavior coverage. Before deleting/substantially changing a
test, record its original contract, current validity, and replacement coverage.
Never disable failures merely to obtain green CI. Separate passed, failed before
the change, not run, and environment-blocked checks in reports.

Work on focused recovery branches, never directly on `main`. When authorized,
commit, push, and open PRs with validation evidence. Never auto-merge, force-push
protected branches, rewrite history, or delete irreplaceable source assets
without separate explicit authorization. Do not message others or update project
boards without authorization.

Update the lightweight recovery record with commits/PRs, exact engine revisions,
validation, baseline failures, blockers, and the next executable step. Keep
ownership, architecture, and build instructions accurate. Do not describe plans
as implemented or add large competing documentation systems.
