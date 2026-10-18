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
physics overhaul. Trigger lifecycle, overlap/shape casts/all-hits, static mesh
cooking, shared material tags/impact events, two-way character contact/support,
dropped weapons, constraints/ragdolls and prop replication remain separate work.
No game default, damage, movement tuning or network schema changes are included.
No graphical application was launched or visual acceptance claimed.
