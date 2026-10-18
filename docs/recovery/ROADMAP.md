# Recovery roadmap

Work in bounded PRs. Historical status/feature lists are reference. See PROGRESS.md
before resuming; never repeat completed work or merge without approval.

| Step | Acceptance criteria |
| --- | --- |
| 0a Rules | Replace large-feature priorities; retain ownership, determinism, lifetime and validation rules. |
| 0b CI baseline | Windows/MSVC and Ubuntu/Clang configure/build; all dependency-light core, ECS, glTF, animation, networking and KCC suites run; failures preserve logs. |
| 1 Test/assets | Classify tests by contract/regression/integration/scaffolding/game; preserve standard glTF behavior and move game acceptance to Nemisis. No deletion without a recorded contract/replacement. |
| 2 Game support | Change engine APIs only for a concrete Nemisis blocker; build/test both repos at an exact engine revision. |
| 3 Vertical slice support | Keep deterministic authoritative primitives sufficient for a complete two-player match; no speculative engine expansion. |
| 4 Consolidation | Extract renderer/importer/KCC responsibilities individually behind stable APIs, with lifetime and behavioral validation. No render graph or rewrite. |
| 5 Hygiene | Remove proven obsolete paths/artifacts; keep source assets recoverable and revalidate compatibility. |

## Ownership

| Responsibility | NovaCore | Nemisis |
| --- | --- | --- |
| Simulation | Fixed-step runtime, ECS/lifetime, collision/KCC mechanics | Movement/ability policy, tuning, player sequencing |
| Rendering/assets | GPU lifecycle, generic submission, standards-correct import | Maps, cooked assets/sockets, mounts, camera and art poses |
| Network | Packets, sequences, generic replication/reconciliation | Game commands, authoritative combat and match schemas/rules |
| Acceptance | Reusable engine contracts | TTK, weapons, objectives, spawn safety, complete match |

No broad Vulkan/KCC refactor before its required coverage and an actual product
need or identified stability risk. Never remove per-asset fixes until the
replacement cook path has been validated in Nemisis.
