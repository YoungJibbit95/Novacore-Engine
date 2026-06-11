# 13 Testing & Acceptance

## Testing Goal

NovaCore testing protects correctness, performance, game feel, networking stability, and long-term engine maintainability.

The project must not only test whether code compiles. It must also verify whether movement, recoil, networking, rendering, asset loading, and server simulation behave consistently across changes.

For an FPS engine, feel is a technical requirement. Movement, prediction, recoil, hit validation, frame pacing, and replication must be measurable.

## Testing Principles

* Every core system should have automated coverage.
* Gameplay feel should be protected through replayable scenarios.
* Network behavior must be tested under bad conditions.
* Renderer tests should verify stability before visual complexity.
* Server and client simulation must share validation paths.
* Smoke tests should remain dependency-light and fast.
* Regression tests should be added for every serious bug.

## Test Layers

NovaCore uses multiple test layers:

1. Build tests
2. Unit tests
3. Smoke tests
4. Replay tests
5. Integration tests
6. Runtime debug validation
7. Performance benchmarks
8. Acceptance scenarios

Each layer has a different purpose and should not replace the others.

## Build Tests

Supported build targets:

* Windows MSVC
* Linux Clang
* macOS later

Build validation includes:

* Configure project
* Build engine
* Build game
* Build dedicated server
* Build tools
* Compile shaders
* Run smoke tests

A build is not accepted if one platform path silently fails or disables required systems without reporting diagnostics.

## Unit Tests

Initial unit coverage:

* Entity ID generation
* Entity liveness checks
* Component attach/remove
* Deferred entity destruction
* Transform hierarchy updates
* Fixed timestep accumulator
* Config parsing
* Config reload errors
* Packet sequence math
* Packet bitstream serialization
* Packet overread rejection
* Input action state transitions
* Asset manifest parsing
* Asset registry lookup
* Streaming request coalescing

Unit tests should be deterministic and should not require a GPU.

## Existing Smoke Test Target

`novacore_smoke_tests` validates:

* Entity lifetime
* Fixed-step accumulation
* Loopback packet flow
* Packet bitstream roundtrips
* Headless relative-mouse fallback
* Input action transitions
* File change tracking
* Config parsing
* Config registry reloads
* glTF metadata loading
* GLB scene-info loading
* GLB triangle mesh extraction
* Vulkan runtime probe stability
* Mesh-handle registration

This target should stay fast enough to run frequently during development.

## Movement Replay Tests

Movement must be tested through deterministic replay files.

A movement replay records:

* Initial player state
* Input command stream
* Tick rate
* Movement configuration
* Expected final state
* Allowed tolerance
* Optional intermediate checkpoints

Test scenarios:

* Walk
* Sprint
* Crouch
* Slide
* Slide jump
* Air control
* Jump buffering
* Coyote timing
* Step-up
* Step-down
* Slope walking
* Slope sliding
* Mantle
* Controller analog movement
* Advanced movement timing

Replay tests should catch changes that make movement slower, floatier, less responsive, or inconsistent.

## Movement Acceptance Metrics

Movement tests should measure:

* Final position error
* Final velocity error
* Grounded state correctness
* Jump timing
* Slide duration
* Acceleration curve
* Air-control strength
* Step resolution
* Slope response

Movement tuning changes should update expected results intentionally, not accidentally.

## Netcode Tests

Network tests simulate:

* Latency
* Jitter
* Packet loss
* Packet duplication
* Packet reordering
* Delayed snapshots
* Missing acknowledgements
* Prediction correction
* Snapshot interpolation
* Lag-compensated hits
* Packet primitive roundtrip
* Overread rejection

The same test scenarios should run through loopback first and real UDP later.

## Prediction and Reconciliation Tests

Prediction tests verify:

* Client input buffering
* Local prediction replay
* Server correction thresholds
* Replaying unacknowledged inputs
* Smooth visual correction
* No permanent desync after packet loss

Measured values:

* Prediction error
* Correction count
* Correction magnitude
* Reconciliation time
* Replay tick count

## Lag Compensation Tests

Lag compensation tests verify:

* Historical hitbox storage
* Rewind to command tick
* Weapon fire validation
* Raycast correctness
* Current-state restoration
* Damage application

Required scenarios:

* Stationary target
* Moving target
* High-latency shooter
* Packet-delayed shot command
* Invalid fire timing
* Target teleport/discontinuity

## Gameplay Tests

Gameplay tests verify actual combat and rules.

Scenarios:

* AR time-to-kill
* SMG close-range time-to-kill
* Shotgun pellet distribution
* Sidearm fallback
* Reload timing
* Weapon swap timing
* Ammo consumption
* Damage falloff
* Objective scoring
* Spawn safety
* Respawn timing
* Team assignment

Gameplay tests should protect balance-critical behavior.

## Renderer Tests

Renderer tests verify stability before visual fidelity.

Scenarios:

* Vulkan runtime probe
* Swapchain creation
* Swapchain recreation
* Clear frame
* Debug triangle
* Depth-tested world box primitive draw
* Basic mesh render
* Mesh handle registration
* GLB CPU mesh extraction
* Material fallback
* Shader compile failure diagnostics
* Frame timing output
* Resize survival
* Null renderer fallback

Renderer tests should report useful errors instead of crashing without context.

## Asset Pipeline Tests

Asset tests verify:

* Manifest loading
* Registry lookup
* Tag filtering
* Streamable filtering
* Dependency parsing
* Duplicate request coalescing
* Priority sorting
* Metadata validation
* Missing asset diagnostics
* GLB scene metadata
* CPU mesh extraction
* Mesh-handle registration

Future tests should include:

* Cook determinism
* Dependency graph validation
* Asset version mismatch
* Streaming zone preload
* Residency transitions
* Safe unload behavior

## Server Tests

Server tests verify:

* Dedicated server startup
* Headless tick loop
* Client session creation
* Client disconnect cleanup
* Match state progression
* Spawn assignment
* Snapshot generation
* Fixed tick stability
* Listen server using shared server code

Server tests must run without renderer initialization.

## Performance Tests

Performance should be measured, not guessed.

Metrics:

* CPU frame time
* GPU frame time
* Server tick time
* Physics time
* Render extraction time
* Culling time
* Draw submission time
* Snapshot build time
* Packet serialization time
* Asset load time
* Memory usage

Performance tests should define warning thresholds and failure thresholds.

## Acceptance Dashboard

The debug UI should expose:

* FPS
* CPU frame time
* GPU frame time
* Server tick time
* Physics time
* RTT
* Packet loss
* Snapshot size
* Bandwidth usage
* Reconciliation error
* Correction count
* Entity count
* Draw call count
* Loaded assets
* Streaming queue length
* GPU memory estimate

The dashboard should make performance and networking problems visible during normal playtesting.

## Regression Policy

Every serious bug should produce a regression test.

Examples:

* Stale EntityId accepted
* PacketReader overread
* Movement replay divergence
* Swapchain resize crash
* Prediction correction loop
* Missing asset crash
* Server tick drift
* Hot reload parse failure

The test suite should become stronger as the engine matures.

## Milestone Acceptance

A milestone is accepted only when:

* Required tests pass.
* Known failures are documented.
* Debug diagnostics are available.
* Performance does not regress unexpectedly.
* Docs match actual behavior.
* Runtime errors fail loudly and clearly.

## First Implementation Acceptance

The first implementation is accepted when:

* Project skeleton is clean.
* Engine, game, and server targets build.
* Core loop exists.
* ECS basics work.
* Input layer works.
* Renderer stub or debug backend runs.
* Net skeleton runs through loopback.
* Asset registry loads manifests.
* Smoke tests pass.
* Missing external tools are documented.
* Headless server path does not depend on renderer/audio/window systems.
