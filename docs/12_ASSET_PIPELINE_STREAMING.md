# 12 Asset Pipeline Streaming

## Pipeline Goal

Assets are commercial-ready and reproducible. Marketplace content is allowed but must pass through cleanup, licensing review, and glTF export before entering the engine.

## Asset Sources

Allowed:

- Custom assets.
- Marketplace assets with commercial license.
- Generated placeholder assets.

Not allowed:

- Assets with unclear redistribution rights.
- Ripped assets.
- Engine-locked content that cannot be legally used outside its ecosystem.

## Blender-to-glTF Flow

1. Import marketplace/custom asset into Blender.
2. Normalize scale and orientation.
3. Clean materials.
4. Rename nodes/skeleton/clips.
5. Add sockets if needed.
6. Export glTF.
7. Cook into engine asset cache.

## Runtime Asset Types

- Mesh.
- Material.
- Texture.
- Skeleton.
- Animation clip.
- Audio event.
- Map zone.
- UI font.
- Shader.
- Gameplay data.

## Hot Reload

Hot reload priority:

- Weapon configs.
- Movement configs.
- Input curves.
- Game mode settings.
- Materials in editor/debug builds.

Not hot-reloaded first:

- Renderer backend.
- Network protocol shape.
- Skeleton topology.

## Streaming

Large-map-ready architecture:

- Stream zones.
- Asset dependency bundles.
- Async load requests.
- Reference-counted handles.
- Priority near player/camera/objectives.
- Safe unload boundaries.

First map remains small enough to avoid making streaming the main blocker.

## Acceptance

Pipeline is acceptable when:

- glTF content can be loaded.
- Missing assets fail loudly but gracefully.
- Data configs hot-reload.
- Stream zones are represented in data even before full large maps exist.

