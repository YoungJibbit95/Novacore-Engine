# 00 Repository Architecture

NovaCore is the engine repository. It owns reusable runtime code only: core loop, platform, renderer, ECS, networking foundations, server runtime, shaders, and tools. Game-specific weapons, movement tuning, modes, assets, and UI screens stay in the Nemisis repository.

## Boundaries

- `engine/include/novacore`: public engine API, stable enough for Nemisis to include.
- `engine/src`: private engine implementation.
- `server`: NovaCore dedicated server executable and server-runtime entrypoint.
- `configs/net`: engine-owned server, tick, snapshot, and packet-simulation defaults.
- `shaders`: engine shader source and shader build notes.
- `tools`: future engine/editor/cooking tools.
- `docs`: engine architecture and subsystem plans.

NovaCore must not include Nemisis headers, assets, game tuning, or game rules. Network/server runtime defaults belong here; match rules, weapons, movement, input feel, and assets belong in Nemisis. The dependency direction is always Nemisis -> NovaCore.

## Code Quality Rules

- Prefer explicit ownership and simple value types.
- Keep server code free of renderer/window dependencies.
- Keep public headers minimal and stable.
- Use data-oriented storage where high-frequency systems need it.
- Avoid global mutable state except narrow diagnostics singletons.
- Add abstractions only when they protect a real boundary.

## Build Rules

- `novacore_engine` is the reusable engine target.
- `Novacore::Engine` is the exported/alias target for consumers.
- `novacore_server` is optional through `NOVACORE_BUILD_SERVER`.
- Nemisis can consume NovaCore as a sibling checkout or installed CMake package.

## Push Rule

Large engine work should be committed and pushed to `YoungJibbit95/Novacore-Engine` before starting the next large game-only step.
