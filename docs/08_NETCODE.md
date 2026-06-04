# 08 Netcode

## Netcode Goal

The netcode is server-authoritative and built for fast movement and short TTK. It must support prediction and lag compensation early because movement tech and gunplay both depend on timing accuracy.

## Transport

Use UDP for real networking:

- Packet sequence numbers.
- Ack and ack bitfield.
- Reliable event channel.
- Unreliable snapshot channel.
- Fragmentation avoided unless absolutely necessary.

M1 uses loopback/direct abstraction before real UDP is implemented.

The first code foundation defines stable network primitive types:

- `SimulationTick`
- `PacketSequence`
- `NetworkRate`
- `Packet`
- `LoopbackChannel`

## Simulation Rates

- Server simulation: 60Hz.
- Snapshot send: 30Hz target.
- Client render: variable.
- Client input command send: 60Hz or higher batching.

## Client Prediction

Client predicts:

- Own movement.
- Own weapon feedback.
- Local UI state.

Client does not authoritatively decide:

- Damage.
- Objective score.
- Spawn state.
- Match result.

## Reconciliation

Server sends authoritative state with input ack. Client:

1. Finds matching local prediction frame.
2. Compares authoritative transform/state.
3. Corrects if error exceeds threshold.
4. Replays unacknowledged inputs.
5. Smooths visual correction where possible.

## Interpolation

Remote players are rendered from snapshot history:

- Buffer snapshots by interpolation delay.
- Interpolate positions and rotations.
- Extrapolate only briefly when packets are late.
- Mark severe discontinuities as teleports.

## Lag Compensation

For hitscan:

1. Server receives shot command.
2. Validate fire timing/ammo/state.
3. Rewind target hitboxes to command time.
4. Raycast.
5. Apply damage.

Movement history and hitbox history must be retained for enough time to cover normal latency.

## Bandwidth Budget

Replication priorities:

- Local player authoritative correction.
- Nearby enemy players.
- Projectiles and utility.
- Objective state.
- Low-priority cosmetics.

Snapshots should be delta-compressed against acknowledged baselines.

## Packet Simulation

Debug controls:

- Artificial latency.
- Jitter.
- Packet loss.
- Duplication.
- Reordering.

These controls are required for testing before external online services exist.

## Acceptance

Netcode is acceptable when:

- Simulated latency does not break local movement feel.
- Packet loss recovers.
- Reconciliation error is visible in debug UI.
- Server rewind validates hits.
- Dedicated and listen server paths share simulation code.







