# 09 Server Architecture

## Server Goal

The server is the authority for match state, movement validation, damage, objectives, and spawns. Dedicated server and listen server use the same core server simulation.

## Server Types

Dedicated server:

- Separate executable.
- No renderer.
- No game UI.
- Logs to console/file.
- Hosts LAN/direct-connect matches first.

Listen server:

- Server world runs in client process.
- Uses same server code.
- Local player connects through in-process or loopback channel.
- Useful for testing and small local play.

## Server World

Server owns:

- Entity world.
- Player sessions.
- Match rules.
- Spawn system.
- Authoritative movement state.
- Authoritative weapon/damage state.
- Snapshot history.

Server does not own:

- Client camera.
- Client HUD.
- Client-only effects.
- Local input device processing.

## Session Flow

1. Server starts.
2. Map and mode load.
3. Client requests connection.
4. Server validates protocol version.
5. Server creates player session.
6. Server assigns team/spawn.
7. Client receives initial snapshot.
8. Match state begins or continues.

## Tick Loop

Each server tick:

1. Receive packets.
2. Decode input commands.
3. Validate movement.
4. Simulate world.
5. Resolve combat.
6. Update game mode.
7. Build snapshots.
8. Send packets.

## Authority Rules

Server authoritative:

- Position after validation.
- Health.
- Ammo.
- Damage.
- Match score.
- Objective state.
- Spawn/despawn.

Client predictive:

- Local movement presentation.
- Weapon firing feedback.
- UI feedback.

## LAN/Direct First

V1 online flow:

- Direct IP connect.
- LAN discovery optional.
- No Steam/EOS dependency in first phase.

Future services can wrap the session layer without replacing core simulation.

## Acceptance

Server architecture is acceptable when:

- Dedicated server starts and ticks.
- Listen server can run the same simulation code.
- Client session abstraction exists.
- Match state can be advanced without renderer.







