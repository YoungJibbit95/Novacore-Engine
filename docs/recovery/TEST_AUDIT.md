# NovaCore test audit

Baseline: `aca50b8c1c4bec920aa4255575bb7591ca825bb0`, inspected 2026-10-18.
All 54 called test functions in the five registered CTest executables were read,
including their fixtures/assertions. The mains call them unconditionally.
Categories describe assertions, not filenames or fixture nouns. REGRESSION marks
focused failure/edge-case protection; historical bug provenance was not inferred.

Decisions: KEEP, CONSOLIDATE, MOVE, DELETE, REVIEW. Risk H/M/L is the loss of
protection if removed without replacement. CONTRACT protects reusable behavior;
INTEGRATION protects a subsystem seam; SCAFFOLDING protects development structure;
GAME-SPECIFIC belongs to Nemisis; UNCERTAIN remains until understood.

## Smoke functions

Source: [`tests/novacore_smoke_tests.cpp`](../../tests/novacore_smoke_tests.cpp).

| Function | Category | Protected invariant | Decision / removal risk |
| --- | --- | --- | --- |
| `testWorldLifetime` | CONTRACT | Entity liveness, component attach/read, destruction and counts | KEEP / H |
| `testFixedStepAccumulator` | CONTRACT | Two 60 Hz steps from 1/30 s, tick count and remainder | KEEP / H |
| `testLoopbackChannel` | INTEGRATION | Server receives loopback sequence and payload length | KEEP / M |
| `testSequenceBufferStoresWrapsAndPrunes` | CONTRACT | Bounded ring lookup/overwrite/expiry, stale rejection, prune, rollback, iteration, clear | KEEP / H |
| `testInterpolationBufferSamplesSnapshots` | CONTRACT | Integer brackets/alpha, exact/empty samples, clamps, prune and rollback | KEEP / H |
| `testRenderSkyFrameData` | CONTRACT + SCAFFOLDING | Disabled-sky/sun/haze/cloud/contact-shadow defaults; other checks inspect directly assigned fields | KEEP defaults; REVIEW assignment-only checks / M, L for tautologies |
| `testRenderMaterialFallbackFrameData` | CONTRACT + SCAFFOLDING | Neutral material/binding/depth defaults; validator preserves legal values and diagnoses/clamps invalid values | KEEP behavior; DELETE four assignment-only checks / H for behavior, L for deletion |
| `testRenderSwapchainExtentStressValidation` | REGRESSION | Equal extent stable, changed extent requests recreation, zero extent rejected | KEEP / H; CPU decision only |
| `testPacketBitStream` | CONTRACT | Integer/float/string roundtrip, full consumption and overread rejection | KEEP / H |
| `testHeadlessRelativeMouseFallback` | CONTRACT | Relative mouse cannot enable without a window | KEEP / H |
| `testInputActions` | CONTRACT | Press/hold/release transitions without repeated press | KEEP / H |
| `testInputShortTapAndPointerSnapshot` | REGRESSION | One-frame tap retains both edges and pointer coordinates | KEEP / H |
| `testTransientMouseAxis` | REGRESSION | Deltas accumulate in-frame and clear next frame | KEEP / H |
| `testInputDeviceActivity` | CONTRACT | Default device and controller/keyboard activity switching | KEEP / M |
| `testFileChangeTracker` | INTEGRATION | Deduplicated watch, unchanged suppression and real file-change event | KEEP / H |
| `testConfigDocument` | CONTRACT | Nested number/bool and array string/integer lookups | KEEP / H |
| `testConfigRegistry` | INTEGRATION | Initial watch/load, lookup and successful reload from disk | KEEP / H |
| `testAssetManifestAndRegistry` | INTEGRATION | Parsed records/dependencies, mount, tags and streamable lookup | KEEP / H |
| `testAssetStreamer` | CONTRACT | Coalescing, budget, priority, pending preservation and zone preload | KEEP / H |
| `testGltfMeshDataAppliesNodeTransforms` | CONTRACT | Static hierarchy translation/scale baked into vertices; marker/source mapping | KEEP / H |
| `testGltfMeshAndMarkersShareNestedTransform` (new) | CONTRACT | Parent quaternion/TRS, child column-major matrix, nonuniform scale and normalized marker basis share one mesh/marker space | KEEP / H; closes a composition/direction gap |
| `testGltfMetadataAndMeshCatalog` | INTEGRATION | Metadata validation, GLB chunks/counts/decoding, imported handles and provenance | KEEP / H |
| `testRendererMeshResourceRegistry` | CONTRACT | Invalid/deduplicated mesh registration, counts, release, generation reuse, dynamic topology/handles | KEEP / H; CPU resource state only |
| `testDynamicMeshPoseReachesEveryFrameSlot` (Phase 2B) | REGRESSION | Actual Vulkan update scheduler consumes latest position/normal payload on both safe slots without another simulation tick; coalescing, no redundant writes, residency/write retries and generation release | KEEP / H; CPU upload callback, no GPU execution |
| `testPhysicsCharacterControllerSurfaces` | CONTRACT + REGRESSION | Ground/step/ramp/snap policy, contacts, no stale support, fast sweeps, wall slide, mantle geometry | KEEP / H |
| `testPhysicsSystemCharacterMotor` | INTEGRATION | Motor/sandbox colliders, tick/sweep/ground telemetry, support carry/jump inheritance and replay | KEEP / H |
| `testEngineSandboxRunsStandalone` | INTEGRATION | Headless scenario selection/ticks, explicit exit status, telemetry suppression, missing IDs and text/JSON output | KEEP / H |
| `testVulkanRuntimeProbeIsStable` | INTEGRATION | Useful probe summary and loader-present instance version | KEEP / M; no drawable GPU assertion |

Generic parser fixtures named `movement`, `weapons` or `ar_01` assert parsing,
not Nemisis balance. They are not MOVE candidates.

## Animation runtime functions

Source: [`tests/animation_runtime_tests.cpp`](../../tests/animation_runtime_tests.cpp).

| Function | Category | Protected invariant | Decision / risk |
| --- | --- | --- | --- |
| `testMathAndGlobalHierarchy` | CONTRACT | Quaternion rotation/slerp, nonuniform parent scale, parent-after-child evaluation | KEEP / H |
| `testSkeletonValidationAndSockets` | CONTRACT | Full socket hierarchy/local offset, lookup, nonfinite/missing data, cycles/parents/names/zero transforms/short poses | KEEP / H |
| `testClipValidationAndSampling` | CONTRACT | Sparse bind fallback, linear/step/quaternion samples, loop/clamp, invalid time ordering/tracks/quaternions | KEEP / H |
| `testPoseBlendingAndMasks` | CONTRACT | Descendant masks, weighted blending, mask size and nonfinite/quaternion rejection | KEEP / H |
| `testRootMotion` | CONTRACT | Loop/multiple/reverse-cycle motion, clamp, invalid identity and translation through accumulated rotation | KEEP / H |
| `testAnimationRuntime` | INTEGRATION | Set/play/update, published pose/root motion, crossfade/layers/sockets and invalid delta/root handling | KEEP / H |

## glTF animation import functions

Source: [`tests/gltf_animation_import_tests.cpp`](../../tests/gltf_animation_import_tests.cpp).

| Function | Category | Protected invariant | Decision / risk |
| --- | --- | --- | --- |
| `testGlbSkeletonAnimationAndSkinningAttributes` | CONTRACT | Hierarchy/TRS, joints/inverse binds, channel timing/interpolation/materials, unbaked skin coordinates and normalized weights | KEEP / H |
| `testTextGltfExternalBuffer` | INTEGRATION | Relative external buffer and percent-decoded URI resolution, mesh/channel import | KEEP / H |
| `testDiagnostics` | REGRESSION | Bad joint indices and skin/animation references rejected with diagnostics | KEEP / H |

Fixture operator/socket names do not introduce game policy. Static node baking
and skinned mesh-local data are complementary contracts.

Phase 2 addition to the glTF import suite (baseline main a727930):
`testAuthoredSocketHierarchy`, `testAnimatedSocketAndIntermediate`,
`testSocketHierarchyDiagnostics` and `testNonJointBetweenSkinJoints` are
CONTRACT/INTEGRATION, KEEP / H. They compare exact authored socket matrices in
bind/animated poses, retain original skin slots, exercise non-joint ancestors and
channels, and reject invalid/ambiguous hierarchy rather than binding by name.
The fixture uses the existing generated GLB writer. Result: 59 called functions
(55 after Phase 1 plus four), still five CTest suites.

## Networking functions

Source: [`tests/netcode_tests.cpp`](../../tests/netcode_tests.cpp).

| Function | Category | Protected invariant | Decision / risk |
| --- | --- | --- | --- |
| `testStrictBitStreamSpanReads` | CONTRACT | Span/length serialization, exact consumption and overread rejection | KEEP / H |
| `testPacketEnvelopeRoundTripAndStrictFailures` | CONTRACT | Header/payload/ACK roundtrip; checksum/trailing/truncation/type rejection | KEEP / H |
| `testSequenceComparisonAcrossWrap` | CONTRACT | uint32 rollover ordering and modular distance | KEEP / H |
| `testReceiveAcknowledgementWindow` | CONTRACT | Gaps/reordering, duplicates/stale packets, ACK bits, rollover and 32-packet boundary | KEEP / H |
| `testSequenceChannelTracksSentAndAcknowledgedPackets` | INTEGRATION | Sent history, selective/duplicate ACK accounting, outgoing ACK and payload bounds | KEEP / H |
| `testFullSnapshotRoundTripAndCanonicalization` | CONTRACT | Canonical entities/components and strict full-snapshot decode | KEEP; repair unsorted component fixture / H |
| `testSnapshotDeltaBuildEncodeApply` | INTEGRATION | Changed/removed/new/replaced entities/components; exact reconstruction and baseline mismatch | KEEP / H |
| `testSnapshotDeltaRejectsMalformedData` | REGRESSION | Duplicate removals and remove/change conflicts rejected | KEEP / H |
| `testSnapshotBaselineStoreRotation` | CONTRACT | Capacity/newest tracking and ACK-preserving prune | KEEP / H |
| `testFractionalInterpolationAndBoundedExtrapolation` | CONTRACT | Subtick alpha/value, short extrapolation and bounded newest/oldest clamps | KEEP / H |
| `testPredictionReconciliationPlan` | CONTRACT | Error thresholds, ACK discard, ordered pending replay plan, hard/missing/stale corrections | KEEP / H; not executed command replay |
| `testServerTickEstimator` | CONTRACT | Initial/smoothed offset and fractional delayed render timeline | KEEP / H |

## Physics functions

Source: [`tests/physics_kcc_tests.cpp`](../../tests/physics_kcc_tests.cpp).

| Function | Category | Protected invariant | Decision / risk |
| --- | --- | --- | --- |
| `testDeterministicCornerManifold` | REGRESSION | Corner planes/crease stop, no penetration, contact hash and insertion-order equality | KEEP / H |
| `testRoundedCornerSweep` | REGRESSION | Rounded AABB-corner hit with radial normal and correct fraction/collider | KEEP / H |
| `testStepClearanceAndTelemetry` | REGRESSION | Support/step telemetry and ceiling clearance, no tunneling | KEEP / H |
| `testUnwalkableSlopeBlocksClimb` | CONTRACT | Steep ramp blocks uphill motion and cannot be walkable support | KEEP / H |
| `testMovingSupportIntegrationAndJumpInheritance` | INTEGRATION | Actually moving support preserves anchor/contact and transfers jump velocity/event | KEEP / H |
| `testLandingSnapAndReplayDeterminism` | REGRESSION | Single landing transition, impact/snap events, settling and replay hashes | KEEP / H |
| `testCoyoteTimeAndJumpBuffer` | CONTRACT | Configurable grace expiry and buffered jump when support arrives | KEEP / H |

Configurable coyote/buffer mechanisms remain engine coverage. Nemisis owns its
chosen timings, input policy and movement feel.

## Applied cleanup and exact retained coverage

1. Removed four material comparisons of floats just assigned by the fixture
   (`1.35`, `1.80`, `1.10`, `0.75`). Original claim: material values can be tuned.
   The fields are plain floats; no runtime operation occurs before comparison.
   Real protection remains in the same function: neutral defaults, valid input
   acceptance, preservation of all four values, invalid diagnostics and all four
   clamp outputs. Removal risk is negligible. No function or CTest entry removed.
2. Repaired snapshot canonicalization setup. Reversing entities moved the only
   multi-component entity to the back; reversing the front entity's one component
   did nothing. Reverse the back entity's components instead, so the existing
   canonical-order assertion now consumes genuinely unsorted component input.
   No assertion weakened and no production encoder/decoder changed.
3. Clarified the CMake option description: it enables all five suites, not only
   smoke tests. Corrected acceptance documentation ownership and coverage claims.
4. Coordinate review exposed missing mixed TRS/matrix and marker-direction
   coverage. Added one engine-owned test through the existing tiny GLB writer;
   no new utility, binary fixture, importer change or game-specific rule.
   Baseline inventory is 54 functions; the resulting suite calls 55.

## Similarity is not equivalence

Primitive bitstream roundtrips and span reads cover different APIs. Sequence ring
slot expiry is not uint32 packet rollover. Integer interpolation/pruning differs
from fractional extrapolation. Direct KCC surface queries and integrated motor/
sandbox scenarios add different contact IDs, support motion, clearance, hashes,
scenario routing and exit-code evidence. Keep these overlaps.

Test-local `expect`, GLB writers and skeleton/world builders are SCAFFOLDING /
KEEP while callers exist. Sharing their syntax is not grounds for a framework.
Further assignment-only sky/enum checks are REVIEW, not an excuse to delete
meaningful default checks in the same functions. No whole test was proven safely
redundant; no actual game-balance test was found in the 54 functions.

Gaps remain explicit: no live impaired transport, drawable GPU, full glTF skin-
socket transform semantics, or Nemisis complete-match proof is inferred from
these CPU suites. See [testing acceptance](../13_TESTING_ACCEPTANCE.md).
