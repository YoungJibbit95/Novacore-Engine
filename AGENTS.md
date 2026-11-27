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

## Continuous studio delivery

For YoungJibbit95/Novacore-Engine and YoungJibbit95/Nemisis the owner authorizes studio development, local integration, topic commits, pushes, pull requests and individually gated merges. The integration lead owns publication and final merges. Keep all other projects and user edits outside that authority.

Read the supported Codex account rate limits at start, at package boundaries, before increasing concurrency and before shutdown. Record the reported windows, remaining percentages and reset times. Use the lowest remaining percentage across applicable windows. When a window is unavailable, record that, work in small checkpoints and request a reading if needed. Context capacity is a different measurement. Never buy credits, reset the account or change billing.

| Remaining allowance | Studio action |
| --- | --- |
| Above 30% | Coordinate normal production and checkpoint regularly. |
| Above 15% through 30% | Reduce package size and publish verified increments continuously. |
| Above 10% through 15% | Finish existing increments, collect reviews and prepare releases. |
| Above 5% through 10% | Enter shutdown. Start no new agents. Preserve unfinished branches, complete essential validation and publish eligible work. |
| 5% reserve | Return the final checkpoint before reaching the reserve. Allow for concurrent use and reporting delays. |

Inspect local changes, existing aggregate PRs, the reachable engine pin and the last verified playable baseline before new production. Audit the intended diff, asset rights, large files, credentials, private paths and generated output. Publish validated packages through focused topic PRs. Record both revisions, the department, dependencies, local tests, head-specific CI, reproducible evidence and limitations. Use repository-accessible images, motion evidence or matched performance measurements as applicable.

Only the lead may merge after independent review, relevant local suites, real Vulkan and visual acceptance where applicable, successful current-head CI, clean scope, mergeability, satisfied dependencies and compatibility with the target. Preserve branch protection, reviews and tests. Avoid force-pushes, duplicate aggregate integration and history rewrites. Validate NovaCore independently and its exact candidate in Nemisis. Merge the engine first, pin Nemisis to the reachable integrated commit and repeat consumer validation. Verify main revisions and post-merge CI. At shutdown record pending CI, open PRs, exact identities, unfinished work and the next executable action.

Preserve irreplaceable source assets and existing communication safeguards.

Update the lightweight recovery record with commits/PRs, exact engine revisions,
validation, baseline failures, blockers, and the next executable step. Keep
ownership, architecture, and build instructions accurate. Do not describe plans
as implemented or add large competing documentation systems.
