# Recovery progress

## 2026-10-19 Combined public sensor package

- Local combined runtime `130c4fdadd05d7cb0e7d8f808deeec35f29f10fa` adds the
  reviewed #20 API to #19's UI, proper camera, R2 and Jolt integration. The merge
  retains both recovery records; no runtime conflict resolution was needed.
- Complete Release headless engine/server/sandbox build and all nine CPU suites
  pass in 1.91s. A separate installed public package builds/runs the real Nemisis
  sensor/material/impact fixture: six props, tagged floor contacts, native player
  Enter/Exit, physical closest-hit tags, reset and a 1.21828m walked crate push.
  Full game regression is running. This record-only commit changes no API/runtime.
- The prior `cca45a41` Vulkan runtime/shader/package paths remain frozen during
  user inspection. No new sensor Vulkan build or GPU acceptance is claimed.

## 2026-10-19 Phase 5 bounded sensor and tagged-contact slice

- Published Draft [engine PR #20](https://github.com/YoungJibbit95/Novacore-Engine/pull/20),
  runtime `34e87207f0219e1e8901e5b192703fa2adbb2d31`. Exact runtime complete
  default-off MSVC/Clang [run 39174235581](https://github.com/YoungJibbit95/Novacore-Engine/actions/runs/39174235581)
  and Jolt MSVC/Clang [run 39174235602](https://github.com/YoungJibbit95/Novacore-Engine/actions/runs/39174235602)
  all pass. This following record-only commit changes no runtime or API.
- Branch `recovery/phase5-physics-sensors` starts at immutable adoption head
  `0da8ba32eed6617238c6a8f45553c7e5ec037c23`, stacked on Draft PR #17.
  Only RigidBodyWorld, its tests and recovery records change; CMake, canonical
  derivation, KCC, renderer and game source remain unchanged.
- Append application material/surface IDs to descriptions/snapshots/rays; allow
  live tag edits, retaining tags across canonical geometry replacement.
  Game-owned presets still supply the explicit friction/restitution values.
- Optional bounded extraction uses native Jolt Added/Persisted contact callbacks
  and active kinematic sensor bodies. Extract sorted owned pre-solve contact
  records and Enter/Stay/Exit after fixed Update. Sensor target poses are sampled
  on the following tick; no continuous sensor/CCD or solved impulse is claimed.
- Removal/replacement exits retain old world/generation identity, reset clears
  queues, symmetric filters remain explicit, default rays skip sensors, and any
  capture overflow invalidates the world without a partial successful packet.
  Event capture remains disabled by default; no implicit canonical triggers.
- Local GCC 13.2/O2 complete headless build plus 8/8 suites pass in 1.18 s,
  including sleeping overlap, actual kinematic-player detection, removal/reuse,
  tags, replacement/reset, stable ordering and both overflow paths. Final
  Jolt-disabled adapter compiles/links/runs without Jolt. KCC720 hash remains
  17925562298706474255 with capture off/on and after dynamics.
- New C: variant public CMake package configures/builds/links/runs native sensor
  Enter/Exit and tagged rays against unchanged installed Jolt. Earlier installed
  artifacts remain untouched. Hosted CI passes; exact Nemisis combined revision
  remains a separate acceptance gate. No graphical application was launched.

## 2026-10-19 Main integration: UI, proper camera, shadows and rigid bodies

- Exact combined source `cca45a41aded8b13171943553e27f85071c76ed3` retains
  UI #15, proper camera #16, R2 #18 and the optional Jolt adapter #17.
  Integration merges changed no runtime implementation or shared API by hand.
- Complete Release engine/server/sandbox builds pass with Jolt enabled, both
  headless and SDL3/Vulkan. All nine CPU suites pass in 1.80s; all 23 shader
  modules compile and pass SPIR-V validation for Vulkan 1.0.
- Installed public packages were used for the real Nemisis Dev Range fixture
  test: six bodies settle, accept a closest-hit impulse, render the existing
  crate geometry with physical rotation, respond to bounded KCC walking and
  reset safely. The real headless game fixture smoke also passes after fixing
  its pre-existing mismatch between spawn transform and movement position.
- Full game regression and combined hosted CI are pending. GUI was not started;
  Vulkan execution, shadow/contact quality, resource resizing and GPU timing
  remain separate acceptance gates. R3 texture sampling is a subsequent branch.

## 2026-10-19 Phase 5 optional rigid-body adoption

- Published Draft [engine PR #17](https://github.com/YoungJibbit95/Novacore-Engine/pull/17),
  runtime `b17ff125f9d0b6a0a5150155f067e9e92ed6ed91`. Exact runtime head passes
  complete default-off MSVC/Clang [run 39171464412](https://github.com/YoungJibbit95/Novacore-Engine/actions/runs/39171464412)
  and optional Jolt MSVC/Clang [run 39171464486](https://github.com/YoungJibbit95/Novacore-Engine/actions/runs/39171464486).
  This following record-only commit changes no runtime or API. No automatic merge.
- Isolated `recovery/phase5-physics-adoption` starts at exact main
  `38e718bad65001879b482f8039bff338855e5f0b`. No original dirty worktree,
  character/KCC source, renderer/UI source or existing tests changed.
- PH-1 gate adopts MIT Jolt 5.6.0 at immutable
  `e77f175595e64cb44218cc9d9d56fc365ad0e36a` for optional CPU dynamics.
  See [capability audit and authority contract](PHYSICS_ADOPTION.md).
- New public PIMPL `RigidBodyWorld` supports real boxes/spheres/capsules,
  oriented boxes, mass/inertia/angular motion, gravity, contact friction,
  restitution/damping, sleeping/waking, CCD, generation/world-owned handles,
  static/kinematic/dynamic modes, symmetrical 8-layer masks and closest rays.
  Canonical blocking boxes/four ramp wedges derive from existing StaticCollider
  data; unchanged fixtures persist, changed/removed support wakes nearby bodies.
  A real kinematic capsule pushes dynamic props without applying impulses to
  the unchanged competitive motor. Prop-blocked KCC movement is not implemented.
- Complete local GCC 13.2/O2 default-off 8/8 pass in 1.67 s and Jolt-on 8/8 pass in 2.74 s.
  Existing 720-tick KCC replay hash remains 17925562298706474255 in both configs
  and after dynamic-world simulation. Jolt default 2 cm slop caused measured
  two-box penetration 0.229792/0.709549 m; explicit 5 mm slop improves centres to
  0.244805/0.739578 m without weakening the original 2.5 cm test assertion.
- Single-thread 60 Hz CPU benchmark (30 warmup/270 samples), median/p95 us:
  32 props 6.2/76.9, 128 props 87.7/431.1, 256 props 726.2/1184.5. One local run;
  no GPU/frame-time or cross-platform determinism claim.
- Installed public CMake consumer actually builds/links/runs a dropped box
  using only NovaCore headers/target (Y=0.245 m, sleeping). Jolt static dependency
  and MIT notice install correctly. Hosted default-off plus opt-in MSVC/Clang
  workflows pass; exact Nemisis Dev Range integration and visual review remain gates.
- Final trigger-kind exclusion retains the existing KCC's nonblocking semantics
  even for inconsistent trigger flags; closest-ray regression covers this.
  Complete final Jolt-on 8/8 suites pass in 5.03 s. Final disabled adapter also
  compiles/runs without linking Jolt. No existing suite/assertion was removed.
- No GUI launched. Next executable step: main-agent exact-pin Dev Range
  integration/tests against the existing UI/camera engine changes;
  preserve default-off adoption until accepted. Trigger events, shape casts,
  two-way KCC contact, materials/impact event tags and prop replication remain
  explicit later slices. No general solver/constraint engine was reinvented.

## 2026-10-18 Phase 2B dynamic mesh frame slots

- Published DRAFT https://github.com/YoungJibbit95/Novacore-Engine/pull/11;
  implementation/published Nemisis pin a687c8816fbcc8a7f4d313cf092c6dc17a59f8f7.
  Hosted MSVC and Clang full builds/5 suites pass at that exact runtime commit:
  https://github.com/YoungJibbit95/Novacore-Engine/actions/runs/39139271352.
  This following record-only commit does not change runtime or the tested pin.
- Nemisis draft #16 https://github.com/YoungJibbit95/Nemisis/pull/16 implements
  real-grip constraints at10610e7, stacked after shared-pose #15 and the exact
  combined #12/#13/#14 base5baab6137dc5e0a3a81a2b0e9ec62ac4f8a78fa0.
  Against exact engine a687c88: full opt-in build/37 CPU cases, default-off
  build/34 cases and full real SDL3 3.2.22/Vulkan SDK1.4.350.0 conditional
  backend/game compilation pass. Headless productive IK confirms4 samples,
  constraints, skins and accepted vertex updates; no invalid/unreachable frames.
  These CPU requests do not prove GPU execution. No graphical app was launched.

- Separate worktree F:/Coding/Project/recovery/phase2b/dynamic-mesh/Novacore-Engine,
  branch recovery/06-dynamic-mesh-frames, based on exact engine PR #10 HEAD
  e22d39708f09a80472c185ab52cc59b90a3f8c14. Existing PRs and dirty original
  checkout remain untouched. No automatic merge.
- Concrete Nemisis hand-IK integration blocker: fixed ticks queue one skinned
  pose, but Vulkan wrote only the current frame's dynamic vertex buffer and
  cleared the pending payload. The next render without another fixed tick
  selected the other frame buffer's old arm pose while the weapon was current.
- A small private scheduling helper retains the latest positions/normals until
  both frame slots consume them. Replacements coalesce by full handle/generation
  and dirty both slots; release discards exactly that generation. Queued meshes
  awaiting residency and failed writes retain their payload for a safe retry.
- Vulkan still writes only the current slot after its fence completes. A slot
  already holding this pose does not upload again. No public API, new manager,
  shader, animation sampling, asset transform or game policy change.
  Each new payload needs two frame-slot copies, without extra game uploads,
  sampling or skinning. Existing stats distinguish backend queue-request attempts
  from successful safe-slot copies (which can exceed requests); failure counts
  include rejected requests and failed/discarded slot attempts.
- Existing smoke suite uses the same payload scheduler called by Vulkan, with
  simulated safe-slot uploads: pose A on both slots without another tick, newer
  pose superseding partly consumed A, repeated-slot suppression, residency/write
  retry, handle-generation release, discard and invalid slot. Existing tests and
  assertions remain. Test inventory is 60 called functions, still five suites.
- Complete fresh local GCC 13.2/Ninja dependency-light build passes; all five
  CTest suites pass (1.53 seconds). Commands: `cmake --preset local-debug-no-deps
  -DCMAKE_CXX_COMPILER=C:/Strawberry/c/bin/g++.exe`, `cmake --build --preset
  local-debug-no-deps --parallel 4`, `ctest --test-dir build/local-debug-no-deps
  --output-on-failure --timeout 120`. Existing unused sandbox helper warning
  remains visible. `git diff --check` passes.
  Nemisis pin was updated only after exact a687c88 cross-repo validation;
  its former 15c1b684a430bb70235ea9407f78106d958812b6 did not contain this fix.
- No GUI automation, Vulkan execution or visual acceptance performed. Compile
  the SDL3/Vulkan path and run hosted MSVC/Clang checks before accepting the PR;
  actual GPU hand-contact acceptance remains a separate explicit gate.

Next executable step: recheck the published PR heads/hosted checks and the exact
Nemisis pin, then perform the documented Vulkan image matrix only after actual
manual evidence/fresh GUI authorization. Do not auto-merge or replace the game
legacy/default-off gates without visual acceptance. The old next-step note below
is superseded by the published/cross-validated state above:

Historical pre-publication step: finish CPU tests, review/commit this focused engine change,
build Nemisis hand constraints against that exact SHA, and compile its graphics
target. Preserve legacy placement/default-off migration until visual acceptance.

## 2026-10-18 Phase 2 authored socket hierarchy

- GitHub rechecked: recovery PRs #7/#8/#9 merged; current main is
  a727930568bb5e5e60830dad6fe52ba2d7d3c074. Nemisis #7 through #11 merged;
  main 741aeae2edeb2df67564ae0b2379ab06a3df5505 still pins dbdfc38.
- Separate worktree F:/Coding/Project/recovery/phase2/Novacore-Engine; original
  dirty user checkout and previous recovery worktrees are preserved.
- Confirmed two bridge bugs: name-hint socket attachment discarded authored local
  TRS, and nearest-skin-joint parenting omitted non-joint ancestor transforms.
  Actual Nemisis FP arms and the selected chr_project_male1 both have a required
  non-joint Y180 ancestor. Do not compensate with new per-weapon Euler repairs.
- Bridge now follows node parentage. Static sockets retain authored local TRS;
  missing selected-skin ancestry produces an explicit warning and no guessed
  attachment. Invalid/multiple parents, duplicate node IDs and cycles diagnose.
- Non-deforming ancestors/intermediates are appended to the existing pose as
  ordinary joints. Original skin palette slots/JOINTS_0 and inverse binds remain
  first and unchanged; helper inverse binds are identity and unused by weights.
  Node animation channels, including animated socket nodes, use this one runtime.
  Animated socket locals live in their pose node rather than being applied twice.
  No public signature, new animation runtime or renderer/KCC change.
- Added four small functions in existing glTF import tests: exact authored socket
  translation/rotation/scale in bind and animated poses, misleading socket names,
  scene ancestors, matrix/TRS intermediates between sockets AND skin joints,
  animated non-joint/socket channels, instance order and malformed ancestry.
  Existing binary writer reused; no checked-in GLB or test framework added.
- Fresh baseline complete GCC 13.2/Ninja headless build: 5/5 suites pass. Complete
  post-change build and 5/5 suites also pass. No existing function/assertion removed.
- Published PR https://github.com/YoungJibbit95/Novacore-Engine/pull/10; runtime
  fix 455869c3229dc39f665a5958d727a7daaf4c9d21, published/tested/pinned revision
  15c1b684a430bb70235ea9407f78106d958812b6 (following commit changes docs only).
- Hosted complete Windows/MSVC and Ubuntu/Clang builds and 5/5 suites pass:
  https://github.com/YoungJibbit95/Novacore-Engine/actions/runs/39126715016.
- Nemisis e48c6e38605a3c3abf971646ffd8901c63583c16 built against exact 15c1b68:
  all 34 CPU cases pass locally and on both hosted platforms in game PR #12:
  https://github.com/YoungJibbit95/Nemisis/actions/runs/39127318199.
  Actual arms/selected-character bind socket parity and skinning now pass; old pin
  reproduced 24 failures. Separate geometry-cleanup PR #13 also passes 34/34.
- Local acceptance branch recovery/phase2-review combines only published #12/#13,
  commit259755f; combined full CPU build/34 tests and SDL3/Vulkan game build pass.
  This is an unpublished review branch, not a merge to main. No GUI actions or
  visual validation: the previous Escape boundary remains in force pending the
  explicit new GUI question. No PR merged automatically.

Next: visually inspect the combined existing Dev Range only after renewed user
authorization. Then solve hand contact after animation and before final
skinning; the existing whole-mesh average translation is not IK. ozz TwoBoneIK is
a validated CPU-prototype candidate for that separate game blocker; parser migration does not fix this
bridge bug. Preserve the user's chosen chr_project_male1 and unmodified GLBs.

## 2026-10-18 baseline and rules

- Published baseline: dbdfc38d092e018952ead43d9cb669333ebf70bc.
- Rules commit: f769998; PR: https://github.com/YoungJibbit95/Novacore-Engine/pull/7.
- Worktree: F:/Coding/Project/recovery/Novacore-Engine. The original main checkout
  contains user changes (including untracked CI/benchmarks); none were imported.
- Executed: `cmake --preset local-debug-no-deps -DCMAKE_CXX_COMPILER=C:/Strawberry/c/bin/g++.exe`,
  `cmake --build --preset local-debug-no-deps --parallel 4`,
  `ctest --test-dir build/local-debug-no-deps --output-on-failure --timeout 120`.
- Result: complete C++23 engine/server/sandbox/test build; 5/5 suites pass.
  GCC 13.2 / Ninja / Windows; SDL3 and Vulkan disabled. Logs: build/baseline-*.log.
- Cross-repo baseline: Nemisis 6b0f411eab1a717656adfd7f70f10e2f76a5dcbf also
  builds against this engine revision, with 37/37 registered tests passing.
  Its three Vulkan-named smokes currently pass in headless mode: this is a
  pre-existing validation gap, not evidence of GPU rendering.
- Pre-existing compiler warning: unused buildSummary in EngineSandbox.cpp when
  SDL3 is disabled. No baseline test failures observed on this toolchain.
- Not run: Linux/Clang, Windows/MSVC, GPU/Vulkan and playable visual validation.
  No MSVC installation was found locally. GitHub access: ADMIN; push/PR succeeded.
- Removed: contradictory large-patch/silence rules and historical feature queues.
  No runtime code, assets, or tests removed. PR remains unmerged.

Next: branch recovery/01-ci-baseline from the rules PR, add minimal headless
Windows/MSVC and Ubuntu/Clang CI, and record real hosted results. Keep all
existing tests; do not promote a no-GPU run into graphics validation.

## 2026-10-18 CI baseline implementation

- Added shared `ci-headless` configure/build/test presets plus Linux/Clang and
  Windows/MSVC variants. GitHub workflow runs on PRs, main pushes and manual
  dispatch, with read-only permissions, cancellation/timeouts and failure logs.
- Action releases resolved through GitHub and pinned to verified commits:
  checkout v7.0.1 / 3d3c42e5aac5ba805825da76410c181273ba90b1;
  upload-artifact v7.0.2 / cf430e030ddbb5b0abf93d22962f4752f3646cd9.
- Executed: configure with `--preset ci-headless -B build/local-debug-no-deps
  -DCMAKE_CXX_COMPILER=C:/Strawberry/c/bin/g++.exe`, complete incremental build,
  all 5 CTest suites (pass), `cmake --list-presets=all`, actionlint and
  `git diff --check` (pass). Reused the baseline binary directory locally;
  hosted jobs will validate fresh platform-specific preset directories.
- No runtime code, API, assets or tests removed. The unused sandbox helper warning
  remains visible; CI does not disguise it by claiming warnings-as-errors.
- Nemisis remains pinned to the published dbdfc38 runtime; this PR changes only
  CI/presets/docs, so no engine API compatibility dependency is introduced.
- Hosted MSVC/Clang results pending publication; GPU rendering intentionally not
  covered by this CPU-only workflow. Do not mark Phase 0 complete until results
  are recorded. Next: inspect hosted runs, then classify tests/ownership in Phase 1.
- Hosted first run: Ubuntu/Clang builds and all 5 suites pass
  (https://github.com/YoungJibbit95/Novacore-Engine/actions/runs/39120015726).
  Windows-2025 could not resolve a Visual Studio 2022 instance at configure time.
  Selected the explicitly documented Windows-2022 / VS2022 runner instead;
  no tests or compiler checks were disabled. Fresh validation pending.

## Phase 0 validation complete

- Rules: f769998 / tracking 28dca6f, PR #7.
- CI: 49740d4 plus runner correction 9c87ef2, PR
  https://github.com/YoungJibbit95/Novacore-Engine/pull/8 (base: recovery/00-agent-rules).
- Fresh hosted run at 9c87ef2 passed on both platforms:
  https://github.com/YoungJibbit95/Novacore-Engine/actions/runs/39120124857.
  Ubuntu 24.04 / Clang 18.1.3: complete build, 5/5 suites pass.
  Windows 2022 / MSVC 19.44: complete build, 5/5 suites pass.
- Windows-2025 had resolved to windows-2025-vs2026, explaining the first
  VS2022-generator configure failure. Windows-2022 matches the existing toolchain.
- Actionlint and the final diff whitespace check pass. No merge performed.
- No GPU/visual validation performed. No runtime API or code changed; Nemisis CI
  continues to validate the identical published runtime at dbdfc38.

Next executable step: classify each existing test contract, then correct
`docs/13_TESTING_ACCEPTANCE.md` so weapon TTK/objective scoring/spawn safety are
Nemisis acceptance. Do this as a separate bounded PR after the CI stack. Keep
all existing coverage until its replacement/ownership is verified. Renderer,
importer and KCC extractions remain deferred behind real product needs.

## 2026-10-18 Phase 1 test architecture audit

Current status correction: GitHub confirms PR #7 merged as 9c2566d and PR #8 as
aca50b8. Earlier unmerged/pending statements above describe their original dates;
they are historical, not current instructions. Phase 1 starts from
aca50b8c1c4bec920aa4255575bb7591ca825bb0 in the clean recovery worktree. Original
user checkout remains dirty and untouched. Nemisis main is ff88eb0; its runtime
pin remains dbdfc38d092e018952ead43d9cb669333ebf70bc.

- Read all 54 test functions/assertions and CMake registrations; TEST_AUDIT.md
  records group/function categories, invariants, overlap, decisions and risk.
- Removed only four assignment-only material assertions. Neutral defaults,
  validator preservation, diagnostics and all clamp checks remain in that test.
- Corrected a no-op canonical snapshot fixture: reverse the multi-component
  entity after reversing entity order. Existing component-order checks now test
  unsorted input. No runtime code, public API, test function or suite removed.
- Updated testing acceptance to separate verified CPU behavior, missing coverage,
  real GPU requirements and Nemisis combat/movement/match ownership.
- Before changes: complete local C++23 build and 5/5 suites pass (GCC 13.2/Ninja).
  Post-change build/test and hosted MSVC/Clang results will be recorded below.
- Next: validate/publish this bounded PR; game audit and coordinate contract are
  independent Nemisis PRs. Engine pin change is unnecessary for test/doc edits.
- Post-change: complete local build passes; all 5/5 CTest suites pass with
  GCC 13.2/Ninja. `git diff --check` passes. No GPU rendering is claimed.
- Coordinate analysis also closed one engine-owned test gap: mixed parent TRS /
  child matrix plus nonuniform scale and marker forward/up. Existing tiny GLB
  writer reused; resulting function count is 55 (54 baseline + one contract).
  No importer implementation or public API changes. Independent game contract
  tests still run against the exact old pin, whose runtime behavior is unchanged.
- The added nested-transform contract also passes the complete local build and
  5/5 suites. It is engine-owned; no parser/socket repair code was introduced.
- Published PR https://github.com/YoungJibbit95/Novacore-Engine/pull/9 from main
  aca50b8. Cleanup commit 6689cfd5fe1fab50543e71d042c5774003f775f5; added hierarchy
  test at 59929d6c6941f62ea20dd48424119811d37b0afe.
- Hosted complete builds and all 5/5 suites pass on Windows/MSVC and Ubuntu/Clang
  at 59929d6c6941f62ea20dd48424119811d37b0afe:
  https://github.com/YoungJibbit95/Novacore-Engine/actions/runs/39123953218.
  This evidence follow-up changes documentation only. No merge performed.
- Independent Nemisis audit PR #10 retains 177 functions; contract PR #11 adds
  two game functions against unchanged pin dbdfc38. A separate graphical build
  reached the RTX 3070 Ti backend and one live hipfire image; physical Escape
  stopped Computer Use. Held ADS, muzzle/grips and animation are not fully
  visually validated; GPU CTest smokes were not run. CPU CI is not graphics proof.

Next executable step: inspect the current/candidate M4A1 image matrix in Nemisis
PR #11 using the existing Dev Range, then consider a separate visually reviewed
cook/catalog migration. Preserve active offline fixups and both GLBs until CPU
contracts plus hip/held ADS/muzzle/grips/scale/procedural-motion review pass.
The documented authored glTF socket-offset gap needs a separate engine fixture/
fix and exact cross-repo validation if pursued; do not mix it into this audit.

## 2026-10-18 Phase 4 R0 sky/sun projection

- Isolated renderer worktree starts from clean engine main d4eee3a. Its runtime
  matches Nemisis's a687c88 pin; the intervening changes are merged history/docs.
  Original dirty checkouts and the pinned engine worktrees remain untouched.
- Root causes: sky rays used fixed 0.92/0.58 projection factors, opposite vertical
  screen convention to the world projection, and only yaw/pitch. The old world
  look-at also omitted camera roll and was singular at exactly vertical pitch.
- World view/projection and sky rays now share one private camera math helper:
  actual FOV, swapchain aspect, analytic yaw/pitch/roll basis and Vulkan Y flip.
  Appended optional RenderCamera3D::rollDegrees defaults to zero. No projection
  handedness, model coordinates or mesh transforms are changed.
- Sky uses the same normalized world-space toward-sun vector as existing world
  lighting/contact shadows. Translation does not enter its 128-byte push block.
  No shadow map, material/PBR or texture changes are mixed into R0.
- Proposed isolated Nemisis config handoff: sun direction
  (0.397131, 0.342020, 0.851651), approximately azimuth 25/elevation 20 degrees.
  The old vector's 62.7-degree elevation is outside a level 74-degree VFOV;
  correcting projection alone cannot make that sun visible at horizontal pitch.
- Added CPU sky/world pixel round trips across translation, yaw, vertical pitch,
  roll, portrait/landscape/zero extents and three FOVs; explicit world-sun,
  low-elevation, light-direction and Vulkan depth conventions are covered.
- Validation results and exact implementation revision follow after execution.
  No new GUI authorization exists after the user's physical Escape stop: no
  Vulkan pixels, GPU timings or visual acceptance are claimed for this step.
- Executed GCC 13.2 / Ninja Release dependency-light full build (engine,
  dedicated server, sandbox and tests): pass. All 6/6 CTest suites pass in
  1.49 seconds, including the new projection suite. Existing unused sandbox
  buildSummary warning is unchanged. Public headless package installed for
  exact-revision Nemisis validation. Vulkan compilation is recorded next.
- Compiled the actual SDL3/Vulkan backend object with Vulkan SDK 1.4.350.0 and
  all 15 GLSL shaders with glslc. All 15 modules pass spirv-val for Vulkan 1.0;
  sky push-member offsets are 0/16/32/48/64/80/96/112 (128 bytes total).
  No Vulkan device was started; visual/GPU acceptance remains pending.
- User's later visual-reference addon incorporated without restarting work:
  inspected actual local S01-S03, M01-M02, P01-P02 and L01-L03 images, the
  README, reference JSON and original board. Renderer docs now identify original
  source URLs, concrete targets, package boundaries and a reproducible R0/R1
  before/after matrix. Reference binaries remain outside the repository/assets.
- Full SDL3/Vulkan Release static library also built and installed successfully.
  Both public candidate packages have runtime f0c90b7. The following commit is
  documentation-only; cross-repo and visual evidence are still separate gates.

## 2026-10-18 Phase 4 R1 render-instance normals

- Separate branch recovery/phase4-instance-normals is based on R0 fa37f17.
  World meshes previously uploaded instance WVP only and left normals in input
  mesh space while key/fill lighting was world space. Rotation and nonuniform
  instance scale therefore produced incorrect diffuse orientation.
- Implemented actual world-space inverse-transpose instance normals using the
  existing T*Ry*Rx*Rz*S contract, quaternion rotation and reciprocal scale.
  Imported/skinned node transforms are not repeated. Invalid singular/non-finite
  instance scales/rotations skip draws with a bounded diagnostic.
- Kept the 128-byte minimum push block and existing GPU lifetimes: WVP/RGBA and
  normal transforms remain float; world key/fill directions use octahedral
  unorm16 pairs, six bounded intensity/material factors use unorm16 pairs.
  Non-finite scalar factors are replaced before integer conversion; finite
  values retain prior clamp behavior. Tests bound quantization, independent
  transformed-tangent orthogonality, rotation/180-degree lighting and identity
  instance preservation of already-baked normals.
- Scoped limitations: importer applyNodeTransform still incorrectly uses its
  linear node matrix for baked nonuniform normal transforms; document and fix
  separately with an independent slanted-triangle hierarchy regression. The
  fragment shader's fixed view-vector specular/rim approximation is retained
  and explicitly deferred to R4. No PBR/shadow/texture completion is claimed.
- Inspected reference criteria and original S/M/P/L source URLs are retained in
  renderer docs. R1 matched visual matrix holds camera/light fixed while mesh
  rotation/scales change; historical screenshots are not a matched baseline.
- Validation pending coordinated heavy slot; R0 generated shader binaries and
  both installed R0 packages remain unchanged for exact game integration.
- Final tiny-scale robustness check: accept finite nonzero scales without an
  arbitrary epsilon, reject a non-finite reciprocal, and remove common normal
  magnitude in the vertex shader before rotation/normalization to avoid overflow.
- Executed final complete incremental Release build and 7/7 engine CTest suites
  (1.63 seconds), including NaN/Inf/FE_INVALID, tiny scale and independent
  nonuniform transformed-tangent checks. Across 9,260 sampled directions,
  maximum packed light error is 0.00371697 degrees; across 10,001 factor pairs,
  maximum scalar error is 7.62939e-06. All 45 public headers hash-identical to R0.
- Actual SDL3/Vulkan backend object and all 15 GLSL shaders compile in the separate
  phase4-r1-vulkan directory. All 15 modules pass Vulkan 1.0 spirv-val. World-mesh
  push offsets are 0/64/80/96/108/112 and size remains 128 bytes, matching C++
  offset assertions. R0 generated shader files remain frozen and untouched.
- Full R1 Vulkan static library/install and exact-revision Nemisis checks follow;
  no device launch, GPU timing or visual acceptance is claimed.
- Completed full separate SDL3/Vulkan Release static-library build and public
  package install for implementation a68ea79de2c97b8b46e773da856d12f8886593e2.
  Package: recovery/phase4/engine-r1-vulkan; library SHA-256:
  92cf9b49a03d4fed0020bb3e722e1ccd4557a7524abaf33cf1e7f115b5a58428.
  Headless package: recovery/phase4/engine-r1-headless; library SHA-256:
  18361aa242c768e27042fc74086b6a603822f7f299d611e01b62c0b78fd64952.
  Existing unused sandbox helper warning remains; no warnings/tests disabled.
- Clarified visual capture matrix: Nemisis clamps FOV to 55..105 degrees, so
  actual game comparisons use 55/74/105; engine CPU/sandbox cases 40/110 are
  additional math coverage, not reported game camera settings.
- Main-agent R0 cross-repo evidence: complete Nemisis build and 45/45 CPU suites
  pass against exact R0 fa37f17 (133.77 seconds); engine R0 PR #12 Windows/MSVC
  and Ubuntu/Clang checks pass, run 39166870602. This is CPU/compilation evidence.
- R1 cross-repo integration/hosted CI and both packages' actual matched Vulkan
  screenshots remain separate next gates. No GUI permission or device launch
  occurred in this rendering subagent. Main owns publication/game pin changes;
  no R2 resources or further rendering features started.
## 2026-10-18 UI geometry adapter (UI-0)

Separate worktree/branch `ui/00-overlay-adapter`, based on Nemisis' compatible
engine pin a687c8816fbcc8a7f4d313cf092c6dc17a59f8f7. Adds a public indexed UI draw
packet and an isolated Vulkan renderer recorded after legacy overlays in the
existing render pass. No new instance/device/swapchain/fence owner. Shared
VulkanBackend integration is limited to lifecycle, prepare, record and stats.

CPU textures are immutable premultiplied RGBA8 sources; completed frame slots
retain them until safe release. Host-visible vertex/index buffers are reused
per slot. First-time atlas upload currently waits for the existing graphics
queue. Advanced filters, offscreen layers and backdrop blur are not provided.
Window's existing snapshot adds wheel delta on mouse axis 2 for UI scrolling.

Validation: full GCC13.2 Release SDL3/Vulkan build, new shaders compiled by SDK
1.4.350.0 glslc, all 5 engine suites pass, including malformed UI packet tests.
Game integration and visual evidence are recorded in the dependent Nemisis UI
PR after its native RmlUi build. No gameplay, PBR, lighting or weapon changes.

## 2026-10-19 Phase 5 physical camera chirality

- Branch recovery/phase5-camera-chirality starts at combined UI/render base
  5518818a6b89f20b0ad2ddb3c959116786dd9189 (merged main 38e718 plus UI fb33cb5).
  The UI adapter code, packet API and pass/lifetime hooks remain unchanged.
- Independent source audit confirms the projection seam: glTF RH geometry and
  proper source Y180 rotation produce physical front +Z/right -X. The old
  camera used +Z view depth with screen right +X, horizontally reversing that
  anatomy. Renaming bones or swapping grip targets cannot repair this seam.
- Use a proper RH physical camera frame (right,up,-logicalForward), standard
  RH perspective clip W=-viewZ, unchanged Vulkan depth 0..1/Y flip, and the same
  shared basis for sky rays. Logical yaw/pitch/forward API and public headers
  remain unchanged. No asset, Euler mount repair, mirror flag or mesh edit.
- Added independent asymmetric wrist/thumb/index/front geometry against a
  standard glTF -Z-camera reference, proper determinant, opposite yaw and
  pole/roll tests. Kept all existing sky/world projection/depth/FOV/aspect
  coverage. Corrected the earlier sun-yaw screen-sign assumption to match the
  physical RH basis, while preserving its world-direction/rotation contract.
- Standalone C++23 projection suite passes (GCC 13.2, no SDL/Vulkan launch).
  The same current tests compiled against the untouched 551 camera fail with
  112 assertions, including wrist/medial-thumb/finger-triangle parity and
  opposite yaw. Negative control log: recovery/phase5/render-validation/
  old_projection_tests.log. This is CPU evidence, not a GPU visual review.
- Full engine/headless/Vulkan and Nemisis compatibility checks are pending
  integration-lead build scheduling. Matching Nemisis strafe/input-yaw and
  camera-relative basis changes are required before a production pin migration.
  UI pixel-space geometry remains independent. No new GUI permission exists.
- Prior R2 isolated math/tests/shader work is preserved only on local branch
  recovery/phase5-shadow-maps at 4e31fd461a7d13558caee5548146b39d5856132e;
  it is unbuilt, unregistered and unpublished. Camera/content correctness takes
  precedence. R2 lifecycle/backend work and R3 textures have not been started.

## 2026-10-19 Phase 5 R2 source candidate

- Isolated branch recovery/phase5-directional-shadows starts at RH camera
  f4d7c99b0c522ae872ae05a6eb42818629b16607 (stacked on UI base 5518818).
  Preserved the earlier math-only WIP as 4e31fd4 and cherry-picked it as 96da1c5.
- Implemented the real source path: lazy per-frame-slot depth maps, depth-only
  mesh/box caster pass, world receiver shader variants, 3x3 comparison PCF,
  finite configurable bias/resolution/distance, conservative light-frustum
  culling and per-slot bounds from successful dynamic vertex-buffer writes.
  Explicit cast/receive flags include World-layer first-person opt-out. Legacy
  ViewModel stays on its existing camera/pipeline; UI hooks and resources remain
  unchanged. No new queue, submission, fence or render graph ownership.
- Default disabled creates no shadow Vulkan handles. World push blocks stay
  128 bytes; the private aligned shadow UBO is 160 bytes. Resource replacement
  waits for idle; ordinary updates follow the existing slot fence. Stats count
  recorded depth draws and current dynamic caster draws.
- Standalone GCC 13.2 C++23 shadow math suite passes: finite inputs, world-sun
  depth, whole receiver-sphere coverage, texel stability, off-camera casters,
  six-plane conservative culling, RH camera reconstruction across FOV/aspect/
  rotation/roll, signed nonuniform instance bounds and changed skinned bounds.
  Six new shader modules compile with SDK 1.4.350.0 glslc and pass Vulkan 1.0
  spirv-val. Small outputs are in C:/Users/Administrator/.codex/tmp/phase5-render-r2.
- Full C++ backend/library, engine suites, hosted MSVC/Clang and exact-revision
  Nemisis integration are pending coordinated build resources. The frozen
  Phase 4 and Phase 5 camera shader directories have not been modified. This
  source candidate is not a production pin or a completed GPU/visual gate.
- Renderer docs define matched L01/L03/S02 visual criteria and an explicit
  validation-layer/resize/two-slot matrix. No GUI permission or graphical launch
  exists. R3 actual textures and subsequent PBR/reflection/LOD work remain next
  bounded packages after R2 technical and integration checks.
- Completed full separate GCC 13.2 Release builds in C:/Users/Administrator/
  .codex/tmp/phase5-render-r2/{headless,vulkan}, leaving every frozen prior shader
  directory intact. Exact tested source/test head d1dab476ec2201346b4a675cec711632abff5365;
  runtime implementation is 5af5c830868ef5f3f6db06310fb03ba5ab22cd81. Full engine,
  server, sandbox and all 8/8 CPU CTest suites pass (1.42 seconds). The separate
  actual SDL3/Vulkan static library builds and installs; all 23 generated shader
  modules pass Vulkan 1.0 spirv-val. Only the existing unused sandbox helper
  warning remains. No tests/warnings were disabled.
- Installed packages: C:/Users/Administrator/.codex/tmp/phase5-render-r2/
  package-headless and package-vulkan. SHA-256 for lib/libnovacore_engine.a:
  headless 2a8d3bc2749ab83460b81819f5ce0fea23107289787b93136d698dc8eb4a85a6;
  Vulkan 6f49c386def5c79345af3c131cac743a22b2bf30c279937398ae191414f0e82d.
  Hosted checks and exact-revision Nemisis extraction/build/tests remain next
  integration gates. No GPU launch, validation-layer execution, timing or visual
  acceptance is claimed. R3 was audited read-only; no R3 source was implemented.
- Published [Draft PR #18](https://github.com/YoungJibbit95/Novacore-Engine/pull/18),
  stacked on recovery/phase5-camera-chirality. No remote merge. Integration lead
  owns the matching Nemisis extraction, exact engine pin and consumer gates.

## 2026-10-19 Phase 5 R3 texture source candidate

- Isolated recovery/phase5-gltf-material-textures starts at R2 75b27d140332378413ebcac587bc8839311eeaea.
  Added a default-OFF real opaque glTF texture path, shared encoded payloads,
  UV1 through skinning/dynamic uploads, authored samplers/mips, base color/MR/
  normal/emissive sampling and explicit unsupported alpha/transform fallbacks.
  UI ownership/order is unchanged; R2 set 0 and 128-byte push constants remain.
- Vendored unchanged official pinned stb_image PNG/JPEG decoding in one private
  translation unit with MIT provenance/hashes. Lazy material preparation avoids
  decoding/upload when disabled. Per-image/scene/decode limits and a bounded
  actual-allocation image budget protect CPU/VRAM use; content/color-space
  identity shares GPU images across separately loaded mesh copies.
- Early read-only audit of all 36 required Nemisis sources found one NCAR
  KHR_texture_transform material. Its exact transform metadata is retained with
  explicit material fallback, preserving default-OFF loading. Existing BLEND
  character hair and FR17 glass/rubber remain visibly unsupported transparency.
- Targeted initial importer/decoder tests passed before the final backend and
  skin/budget additions. Complete fresh C: headless and Vulkan builds, all tests,
  36 actual asset imports, shader validation, hosted CI and exact combined game
  integration are pending. Frozen previous shaders/packages remain unchanged.
  No device execution, GUI control or visual quality claim is made. Renderer
  docs record M01/M02 criteria and the pending matched material capture matrix.
- Completed fresh GCC 13.2 Release engine/server/sandbox and all 9/9 CPU
  suites (final run 5.76 seconds), then a separate complete actual SDL3/Vulkan
  static-library build/install. All 27 generated modules pass Vulkan 1.0
  spirv-val. Disassembly confirms set 1 material/frame offsets 0/16/32 and
  0/16/32/48/64, R2 set 0 offsets through 144, and the unchanged 128-byte push
  block. Only the inherited unused sandbox helper warning remains.
- Sequential actual-game probe using this new library loaded all 36 required
  asset paths and decoded all 79 retained images without failure, retaining UV1
  in 39 primitives and the one NCAR texture transform. No game assets changed.
  This is an importer/decoder compatibility check, not a complete game build.
- Separate packages are C:/Users/Administrator/.codex/tmp/phase5-render-r3/
  package-headless and package-vulkan. lib/libnovacore_engine.a SHA-256:
  headless e06937dd1e79ec45f181e3ce4971e43367078c6baef45bb49d1b9b46fa3836f8;
  Vulkan 2acc2a9bcc056d50fc0730e4cc7a61494bb972782a29d644348e2e59cc059933.
  Previous frozen packages/builds were not overwritten. Hosted CI, the exact
  combined physics/UI/camera/R2/R3 consumer and actual device/visual gates remain
  pending integration ownership. The material flag stays default OFF.
- Published [Draft PR #22](https://github.com/YoungJibbit95/Novacore-Engine/pull/22),
  stacked on recovery/phase5-directional-shadows. Exact tested runtime/source
  head is 9ff8ea038c2f21f242bd8af9920bc78c7123b420. The separate integration
  branch must combine the physics APIs before a full Nemisis consumer gate;
  public glTF/renderer aggregate changes require a full consumer rebuild.
