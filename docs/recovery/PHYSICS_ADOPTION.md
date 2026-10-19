# Physics adoption gate and first rigid-body slice

Audit base: NovaCore `38e718bad65001879b482f8039bff338855e5f0b`
(merged instance-normal work). Nemisis audited existing `GreyboxCollision`,
movement replay and Dev Range sources; game integration is a separate PR.

## Existing capability and actual gaps

| Capability | Existing implementation | Gate / gap |
| --- | --- | --- |
| Character | `PhysicsWorld` capsule sweep/rounded corners, depenetration, grounding, step clearance, slopes, snap, wall/mantle probes | Preserve this competitive motor and all replay assertions. |
| Static geometry | String IDs, axis-aligned `StaticCollider` boxes, four linear ramp directions and implicit character bounds/floor | No general oriented box/sphere/mesh queries; primitive scan rather than general broadphase. |
| Moving support | Collider centre/velocity updates, swept relative motion, support carry and jump inheritance | Genuine kinematic support, not a mass/inertia/contact solver. |
| Triggers | `blocksMovement=false`/`SurfaceKind::Trigger` can describe a region | No persistent enter/stay/exit tracker. Game `GreyboxCollision` skips nonblocking primitives altogether. |
| Materials | `surfaceResponseFor` speed/acceleration/traction/friction policy | Existing movement policy, not rigid-body material restitution or contact friction. Keep separate. |
| Dynamic bodies | None | No mass/inertia, angular velocity, gravity/contact resolution, stacks, CCD/sleeping solver. |
| Hit detection | Game debug-target shot trace and authoritative combat paths | Not an engine collision-world raycast against arbitrary props. Do not change damage authority implicitly. |
| Networking | Fixed-tick commands, replay/contact/state hashes and generic replication | No rigid-body schema, rollback state, replicated props or cross-platform body determinism acceptance. |
| Debug presentation | KCC telemetry, contacts and developer visualization | Rigid-body snapshots can feed existing extracted render data; no new sandbox. |

## Decision: adopt Jolt for optional rigid bodies

The upstream release API and release page checked on 2026-10-18 identify
**Jolt 5.6.0**, published 2026-07-11. Pin the complete immutable source commit
`e77f175595e64cb44218cc9d9d56fc365ad0e36a`, not moving `master`.
Upstream distributes it under MIT and lists Windows/Linux/macOS/MinGW and
additional mobile/web platforms; required compilers are VS2022+, Clang16+ or
GCC12+. These are compatible with NovaCore's intended MSVC/Clang matrix and
local GCC13.2. Source: [release](https://github.com/jrouwe/JoltPhysics/releases/tag/v5.6.0),
[pinned README](https://github.com/jrouwe/JoltPhysics/blob/e77f175595e64cb44218cc9d9d56fc365ad0e36a/README.md),
[MIT license](https://github.com/jrouwe/JoltPhysics/blob/e77f175595e64cb44218cc9d9d56fc365ad0e36a/LICENSE).

Decision: use the maintained solver for dynamic contact resolution, friction,
restitution, inertia, damping, CCD and sleeping. Do not write another general
solver. **Do not replace the KCC.** Adoption is default-off pending exact game
integration, replay checks and user-authorized visual review.

The engine owns `RigidBodyWorld` through a private implementation. Public headers
expose NovaCore values, application IDs and world-owned generation handles;
neither Jolt headers nor native body IDs cross the boundary. Static builds retain
the Jolt link dependency in their installed CMake package.

## One source and fixed tick ownership

- `step(canonicalPhysicsWorld, nextTick)` derives blocking boxes and exact linear
  ramp wedges from the **existing** `StaticCollider` data. Geometry is never
  separately authored in a second level file. Unchanged shapes/bodies remain
  resident. Changed centres update in place; changed extents/ramp direction/motion
  recreate only that derived collider and invalidate its old handle.
- Shape transforms are metre-space origins plus unit quaternion. Positive box
  half extents are required; no glTF node/mesh transform or negative scale is
  applied by this adapter. The four ramp tests check actual height intersections.
- Existing implicit KCC bounds are movement constraints rather than authored
  world geometry. They are intentionally not invented as dynamic walls. Supply
  the actual Dev Range floor/walls from its canonical greybox source.
- Canonical moving colliders must contain the **end-of-tick** centre. Derive a
  Jolt kinematic target from it; Jolt ends at that same centre. Existing KCC
  integration/advancement order stays with its current owner. Never advance the
  same collider in both game and adapter.
- Optional player capsule is a kinematic proxy driven from the already resolved
  competitive player position. Layer2/mask `(1<<1)` collides with dynamic props
  only. This provides a real one-way physical push; it does **not** make props
  solid movement blockers or support surfaces for the existing KCC. Props do not
  inject impulses, platform speed or exploits into the competitive motor.
- Single simulation-thread ownership, Jolt single-thread jobs and configured
  fixed delta; consecutive explicit ticks start at 1. Duplicate/skipped ticks
  fail before simulation. A solver capacity-update error invalidates the world
  rather than allowing a partly advanced tick to be retried.
- Contact slop is an explicit metre-valued setting, default 5 mm. Upstream's 2 cm
  default measured box centres 0.229792/0.709549 m for expected 0.25/0.75 m:
  penetration accumulated across the two contact layers. Tighten solver slop
  for these half-metre props; retain the original 2.5 cm acceptance assertion.
- Application IDs identify authored objects; handles invalidate on deletion,
  slot reuse and world reinitialization. Do not send native handles or memory
  snapshots over the network. Only the server may authorize gameplay impulses;
  clients eventually interpolate an explicit versioned snapshot schema.

Upstream [determinism/rollback documentation](https://jrouwe.github.io/JoltPhysics/#deterministic-simulation)
requires consistent inputs, ordering, IDs and compatible settings. Enabling its
deterministic build option is not proof of cross-platform network determinism.
The current acceptance is same-build repeated fixed-tick simulation and unchanged
KCC replay. No networked rigid-body prediction or rollback is implemented.

## Bounded application sensors and material tags

The next slice appends application-owned `materialId` and `surfaceId` values to
body descriptions, snapshots and closest-ray results. Zero means unspecified.
`setBodyTags` also labels canonical bodies after their first synchronization.
Tags do not choose friction or restitution; the existing numeric coefficients
remain authoritative. Nemisis owns Concrete/Metal/Wood presets, art and audio.
A canonical collider with the same ID retains its tags during shape replacement;
actual removal and later re-creation starts with unspecified tags.

Contact extraction is explicit and disabled by default:

```cpp
RigidBodyWorldConfig config;
config.maxContactEvents = 128;
// initialize(config, error), then:
RigidBodyDesc zone;
zone.id = "application_zone";
zone.motion = RigidBodyMotion::Kinematic;
zone.isSensor = true;
zone.halfExtents = {1, 1, 1};
zone.filter = {3, 0x06}; // Observe categories 1 and 2.
// createBody(zone, error); occupants must also permit category 3 in their masks.
```

Sensors are native Jolt bodies with no collision response. This slice accepts
only active kinematic sensors: Jolt static sensors lose contacts when occupants
sleep, while active kinematic sensors also detect sleeping occupants. Native
kinematic-versus-nondynamic contact is enabled for sensors so they can observe
player proxies and static bodies when the explicit symmetric filters allow it.
Normal rays skip sensors; `raycast(origin, displacement, mask, true)` explicitly
includes them. Sensor creation requires a nonzero capture budget.

After a successful fixed `step`, `contacts` contains one deepest pre-solve
manifold per body pair. Endpoints carry owned IDs, world/generation handles and
tags, plus a representative point on each body, an oriented normal and depth.
Negative depth is speculative contact. These are native active-contact records,
not a complete overlap query for sleeping solid bodies. They contain **no solved
impulse**, damage decision or computed audio intensity.

`sensorEvents` contains Enter/Stay/Exit from the native callback pair set, sorted
by application ID and then handle identity. Both endpoints are owned copies;
old handles in Exit remain safely readable values but may fail live lookup.
Discrete callbacks sample the pre-solve pose: a `MoveKinematic` target integrated
this tick is sampled on the following tick. A fast body crossing an entire sensor
between samples can be missed. Sensor CCD/swept triggers are not implemented.
No second geometry scan or custom collision solver supplies these events.

Lifecycle rules:

- A native pair first present in a tick emits Enter, then one Stay per sampled
  tick, including while a dynamic occupant sleeps. Separation emits one Exit.
- Removing either endpoint queues a `BodyRemoved` Exit on the next successful
  tick. Replacing a canonical shape queues `ColliderReplaced` for the old
  generation; a newly overlapping generation emits its own Enter. Invalid ticks
  do not drain the queue. Native deferred removal never resolves a recycled body.
- Metadata edits retain occupancy. Filters remain creation-time values; remove
  and re-create the body to change filtering. Excluded pairs never enter.
- Reset/shutdown clears occupancy and queued events and changes world ownership.
  Consumers clear their occupancy on reset rather than waiting for stale exits.

Callbacks run with Jolt locks held and write only preallocated value records.
They never allocate strings, take body locks, mutate bodies or call game code.
Extraction/coalescing happens after the single-thread update. The configured
budget (1..65536) independently bounds raw Added/Persisted callback records,
tracked sensor pairs, and each extracted vector. Multiple manifolds and CCD
Added/Persisted repeats consume raw records, so leave headroom. Pending exits
also count toward the extracted event limit. Any overflow returns an explicit
error, no successful or partial event packet, and invalidates the world; the
partly advanced tick cannot be retried. Capture zero installs no listener and
keeps the existing default solver path unchanged.

Only explicitly created application sensors participate. Canonical
`SurfaceKind::Trigger`, nonblocking greybox markers, range/spawn markers and KCC
semantics retain their previous derivation. Trigger intent and level policies
remain game-owned. No UI, GameApp, CMake, material registry or network schema is
added by this slice.

Local GCC 13.2/O2 validation (2026-10-19): complete cached headless build and all
8/8 suites pass in 1.85 s (final added no-response assertion: 8/8 in 1.18 s).
Added cases cover native kinematic player detection,
360 sleeping-overlap ticks, explicit sensor rays, symmetric exclusions, metadata
edits, owned removal exits, old-slot reuse, reset, canonical replacement/tag
retention, stable event order, callback overflow and extracted-packet overflow.
The first sensor timing assertions expected the integrated target too early;
actual Jolt callbacks established the pre-solve timing documented above. Assertions
now check both the integration tick and the following sampled tick explicitly.
The original 720-tick KCC hash remains **17925562298706474255**, including with
capture enabled during the intervening 720 rigid-body steps.

The final disabled adapter also compiles/links/runs the updated public header and
test without any Jolt link dependency, retaining that hash. An installed consumer
using only NovaCore public headers and `Novacore::Engine` configures, links and
runs actual Enter tick 1 / removal Exit tick 2 plus tagged sensor ray queries.
It finds the unchanged installed Jolt archive explicitly. New variant package:
`C:/Users/Administrator/.codex/tmp/phase5-physics-sensors`; engine archive SHA256
`0e0096c22fdf9be3aa33639f090911b819f2ba9f73922066da7db27a77343530`.
The earlier `physics/package` engine/Jolt artifacts and recorded hashes remain
untouched. Published Draft [PR #20](https://github.com/YoungJibbit95/Novacore-Engine/pull/20)
contains runtime `34e87207f0219e1e8901e5b192703fa2adbb2d31`.
Exact runtime complete default-off MSVC/Clang [run 39174235581](https://github.com/YoungJibbit95/Novacore-Engine/actions/runs/39174235581)
and Jolt MSVC/Clang [run 39174235602](https://github.com/YoungJibbit95/Novacore-Engine/actions/runs/39174235602)
all pass. Exact combined Nemisis acceptance remains separate. No GUI or Vulkan
validation was performed.

## Native shape casts and bounded overlaps

The PH-2 query slice adds `castShape` and `overlapShape` directly to the existing
world. `RigidBodyQueryShape` contains only a box/sphere/capsule and its metre-space
origin, unit quaternion and positive dimensions. Validation and native shape
creation reuse body-creation conventions, including capsule cylindrical half
height excluding its caps. No render scale, solver, query world, gameplay rule,
KCC replacement or raycast change is introduced.

`castShape(shape, displacement, layerMask, includeSensors)` performs a native
linear translation while retaining the supplied orientation; it does not sweep
rotation. Displacement is the complete finite travel; its length must exceed
0.000001 m, matching the existing closest ray's near-zero guard. Start
and end origins stay inside the same +/-100000 m pose bound. The result
distinguishes an invalid/unavailable query (`queried=false`, error) from a valid
miss (`queried=true`, no hit). A successful query records current world ownership
and the last successful fixed tick. Native earliest fraction wins; initial
contacts at fraction zero take priority, selecting the deepest penetration if
several bodies overlap initially. Exact equal-depth/fraction ties retain native
traversal selection. Starting inside and moving out still reports that initial
contact, rather than silently ignoring it or applying depenetration.

`overlapShape(shape, maxHits, layerMask, includeSensors)` samples the current
native pose, including sleeping bodies. `maxHits` is an explicit 1..8192 bound.
A bounded native collector keeps one deepest manifold per target body; owned
results are sorted by application ID and world/generation handle. If an extra
body exceeds the bound, the result has `overflow=true`, `queried=false`, an
explicit error and **no partial hits**. The query leaves the world valid: no
simulation tick has advanced and there is no traversal-dependent subset to use
accidentally as a complete answer. Increasing the bound and retrying is safe.

Both operations return owned body identities/tags, a point on the query and
target, an outward target-body normal, native depth and fraction.
`initialOverlap` means a native start contact, including touching within native
collision tolerance; overlap hits all have fraction zero and this flag true.
The normal points from the target towards the query, including initial overlap;
an inherently degenerate native axis yields zero rather than an invented normal.
Overlap uses no extra speculative separation distance. Native numerical
tolerance remains; these operations do not promise exact mathematical open-set
intersection or a custom geometric tolerance.

Filtering follows the existing closest ray: the target's category must be in
`layerMask`; simulation pair-response masks are not a second query filter.
Sensors are excluded by default and included only through explicit
`includeSensors=true`. Results cover application dynamics and the same derived
canonical bodies; canonical geometry becomes available after its usual `step`
synchronization. Queries neither synchronize a second source nor mutate the
source, wake bodies, advance ticks or consume sensor events. Queuing
`moveKinematic` retains the old current query pose until native integration;
queries after the step see the integrated pose. This differs deliberately from
the pre-solve sensor-event sample described above.

Owned results remain readable after body deletion or world reset; their old
handles then fail normal live lookup. A reused slot has a different engine
generation. Reset produces a new result world token, including queries before
the first new simulation tick. No native IDs or borrowed solver memory escape.
These are CPU-authoritative query primitives, not a new weapon/explosion policy
or a network schema. The optional Jolt backend remains disabled by default.

Local GCC 13.2/O2 validation (2026-10-19): complete headless engine/server/sandbox
build and all 8/8 suites pass (final 1.19 s). Native cases cover all three shapes
against floor/wall, casts into/out of initial overlap with outward normals,
deepest initial contact, rotated query and target, closer dynamic targets,
sleeping bodies without waking, tags, category masks, explicit sensors, current
kinematic pose timing, stable unique-body ordering, exact/overflow/invalid hit
limits, owned results after deletion/slot reuse/reset and queries that leave
the next simulation tick untouched. Existing closest-ray assertions remain.
The original KCC 720-tick hash remains **17925562298706474255**.

The updated Jolt-disabled adapter compiles/links/runs without linking Jolt and
retains that hash. A new installed public CMake consumer uses only NovaCore
headers and `Novacore::Engine`, with native cast fraction 0.375, outward normal
X=-1, two sorted unique overlaps, an empty explicit overflow and owned material
42 after removal. It links the unchanged installed Jolt archive. New package:
`C:/Users/Administrator/.codex/tmp/phase5-physics-queries`; engine archive SHA256
`3dbf329da90e7e4b4453c51525e47ce6418f147584ea86e34213b67ca8cd8363`.
Earlier adoption/sensor and combined packages remain unchanged. Published Draft
[PR #24](https://github.com/YoungJibbit95/Novacore-Engine/pull/24) contains runtime
`765da36906ef0c69581a6c928f7f4c3c16d2f1a2`. Its complete default-off MSVC/Clang
[run 39176871616](https://github.com/YoungJibbit95/Novacore-Engine/actions/runs/39176871616)
and Jolt MSVC/Clang [run 39176871535](https://github.com/YoungJibbit95/Novacore-Engine/actions/runs/39176871535)
all pass. The main agent reviewed the complete implementation without actionable
findings; exact combined Nemisis acceptance remains separate. No renderer,
Vulkan or GUI validation was performed.

## PH-7 local replay and measurement acceptance

This test-only slice starts at query head
`87f9d77b3e06708f1ae0be00f847df4bc1628e75` (#24). It changes the existing
`rigid_body_tests.cpp` and recovery records, with no library/API, KCC, game,
network, CMake or installed-package changes.

The same-build authored replay now samples ticks 60, 180 and 300. A fixed
off-centre impulse before tick 179 produces real Z/angular motion. Compare all
XYZ positions, all XYZW quaternion components, XYZ linear/angular velocities
at absolute tolerance 1e-5, and exact motion/sleep/canonical/sensor flags and
material/surface IDs. Every sampled pose/velocity component must be finite and
each current handle must resolve. Application IDs match between the independent
worlds; process-local world IDs deliberately differ. This is sampled same-build
repeatability, not cross-platform determinism, rollback or network acceptance.

The old falling grid/stack benchmark mixed active and sleeping phases. Its
historical numbers below remain recorded, but are not a comparison baseline
for **grid_v2**. The new scene places 32/128/256 separated boxes at Y=1 m on one
canonical floor, using the same placements in both modes:

- `active_no_sleep`: body sleeping is disabled; every measured step must report
  exactly N active dynamic bodies, including when bodies are resting on the floor.
- `sleeping_verified`: native activity must reach zero within 600 untimed settling
  steps before warmup. Every measured step must still report zero active bodies;
  final dynamic snapshots must actually have `sleeping=true`.

After initial static synchronization and any required settling, both modes use
30 untimed warmup steps and 270 timed public `step()` calls at fixed 60 Hz with
single-thread jobs. Initial synchronization must create one floor. Subsequent
warmup steps and the complete measurement must create/remove/update zero statics.
Logs include settling ticks, actual minimum/maximum active counts, static resync
counts, median and p95 CPU microseconds. Timed calls include adapter work and
owned extraction; contact/sensor capture is disabled in this scene. Timing values
never decide test success.

The same 32-body worlds also run bounded hit-only ray, box/sphere/capsule cast and
box/sphere/capsule overlap batches. Each group has 32 warmup calls, then 32 batches
of 16 calls; all 512 timed calls must return the actual dynamic target. Layer mask
2 excludes the floor; sensors retain the default exclusion. Overlaps use explicit
`maxHits=1`. Timings include owned result construction/destruction and hit checks;
reported p95 is the p95 **batch average per call**, not a tail-latency estimate for
individual queries. No tick advances, target movement or waking is permitted.
Misses, initially-overlapping casts, query object-count scaling, sensor/contact
capture overhead and the complete six-prop game fixture remain unmeasured here.
Their functional tests remain separate; there is no frame/FPS or GPU claim.

Local GCC 13.2/O2 on Ryzen 5 5500: the existing test target compiled/relinked in
two steps, initial native run plus five serial repetitions pass, and all 8/8
cached CPU suites pass in 2.05 s. KCC720 remains **17925562298706474255**. The five
repetitions reported these ranges of per-run median/p95 CPU microseconds:

| Bodies | Active median | Active p95 | Sleeping median | Sleeping p95 |
| --- | --- | --- | --- | --- |
| 32 | 31.7..33.6 | 44.3..60.6 | 7.3..7.6 | 7.7..9.4 |
| 128 | 121.6..158.9 | 165.7..222.3 | 18.5..19.8 | 19.2..29.5 |
| 256 | 253.0..337.4 | 354.1..848.1 | 33.6..39.3 | 37.9..47.8 |

All sleeping fixtures settled after 52 steps; active minima/maxima were exactly
N or zero as specified, and every timed static resync count was zero. Query median
batch averages ranged 0.55..0.65 us for rays, 0.875..1.71875 us for casts and
1.75625..11.9437 us for overlaps across the modes/shapes/runs. Other agent streams
paused compilers/cooks during these repetitions, but clocks/background OS work
were not locked. These establish a local baseline, not an improvement claim or
portable timing guarantee. Source/scope validation is separate from performance.

Reproduce after configuring the existing headless cache with Jolt enabled:

```powershell
cmake --build build/phase5-headless --target novacore_rigid_body_tests --parallel 1
ctest --test-dir build/phase5-headless -R '^novacore_rigid_body_tests$' -V
ctest --test-dir build/phase5-headless --output-on-failure
```

Record exact revision/compiler/options/CPU with the output and repeat the native
test serially five times without concurrent builds for comparisons. Hosted
MSVC/Clang default-off and Jolt CI must test this exact new source. The earlier
local disabled-backend binary passed before this slice; it is only baseline
evidence and was not rebuilt from the PH-7 test source. No frozen package changed.

Nemisis props remain an opt-in local developer fixture. Current player prediction
and snapshot packets do not carry rigid-body state. A future authoritative prop
path needs server-owned validated impulses, stable application/network identity,
bounded body-state serialization, reconciliation and latency/loss tests; native
solver handles must not become wire IDs. These are remaining gates, not features
implemented by this test-only change.

## Build and validation

Default dependency-light configure/build/CTest remains supported without Jolt.
Opt in with `-DNOVACORE_ENABLE_JOLT=ON`. `FETCHCONTENT_SOURCE_DIR_JOLTPHYSICS`
may point at the exact pinned local source tree to avoid another checkout.
The private dependency disables compute/GPU backends, samples, debug rendering,
profiling, PCH, LTO and optional x86 instructions beyond SSE2. It does not need
an SDL/Vulkan SDK and does not alter the renderer/UI API.

The optional MSVC and Clang workflow builds the complete engine/server/sandbox
and runs original suites plus rigid-body tests. Relevant cases: three shapes,
oriented box, four ramps, stacking/sleep/wake, angular impulse, restitution,
moving support, real kinematic capsule pushes, symmetrical filters, closest ray
normal/ID, 500 m/s CCD against a 5 cm wall, finite validation, duplicate IDs,
unchanged-world reuse, deletion/reset/cross-world/stale handles beyond solver
sequence wrap, consecutive tick ownership and same-build replay.

Benchmark conditions are reported by the same headless test: release compiler,
single-thread jobs, fixed 60 Hz, deterministic grid/stack 32/128/256 dynamic boxes,
one static floor, 30 warmup and 270 timed steps. Report median/p95 CPU update
microseconds; no frame/render/FPS claim, hard hardware-independent threshold or
comparison against a nonexistent old dynamic solver. Full game movement replays
and installed-package consumer are separate integration gates.

Local GCC 13.2/O2 evidence (2026-10-19): complete default-off build, 8/8 CTest
suites pass in 1.67 s; optional Jolt complete build, 8/8 pass in 2.74 s. Original KCC
720-tick hash is **17925562298706474255** with the backend off, on and after
720 rigid-body updates. Final same-scene CPU median/p95 microseconds:
32 bodies 6.2/76.9; 128 bodies 87.7/431.1; 256 bodies 726.2/1184.5.
These are one local run, not a portable performance guarantee.
Host: AMD Ryzen 5 5500 (6 cores/12 logical processors); CPU clock and background
workload were not locked. Final trigger-kind guard was revalidated with all 8/8
suites in 5.03 s while other integration work was running. The final disabled
adapter also compiles and runs its replay without a Jolt link dependency.

Actual installed-package consumer uses only public NovaCore headers and
`Novacore::Engine`: configure/build/link/run succeeds, with its dropped box
resting at Y=0.245 m and sleeping. Installed Jolt library SHA256
`a4f3b886551fb786e1eb9afe17000405bdced70dcd969e161ae992737f82d79a`;
engine library SHA256
`e7ec69f0e555f5d85761a9a339ff3c343325817b8f0067487c185b80f0c9eead`.
Published Draft [PR #17](https://github.com/YoungJibbit95/Novacore-Engine/pull/17),
runtime `b17ff125f9d0b6a0a5150155f067e9e92ed6ed91`: hosted complete default-off
MSVC/Clang [run 39171464412](https://github.com/YoungJibbit95/Novacore-Engine/actions/runs/39171464412)
and optional Jolt MSVC/Clang [run 39171464486](https://github.com/YoungJibbit95/Novacore-Engine/actions/runs/39171464486)
all pass. Exact Nemisis cross-repo integration remains a separate gate.

## Remaining acceptance

This is a bounded PH-1 gate plus initial PH-2/PH-3 functionality, not a completed
physics overhaul. Application sensor lifecycle and tagged pre-solve contacts
are provided by the bounded next slice above. Native translation-only closest
shape casts and bounded unique-body overlaps are provided by the PH-2 query
slice. Canonical trigger derivation, all-hit rays, rotational casts, static mesh
cooking, solved impact events,
two-way character contact/support,
dropped weapons, constraints/ragdolls and prop replication remain separate work.
No game default, damage, movement tuning or network schema changes are included.
No graphical application was launched or visual acceptance claimed.
