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

## Current Engine Backbone

Implemented now:

- `novacore::assets::AssetRecord` describes id, kind, source path, cooked path, dependencies, tags, priority, estimated bytes, and streamable state.
- `novacore::assets::AssetManifest` loads flattened JSON config data into deterministic asset records.
- `novacore::assets::AssetRegistry` mounts one or more manifests and provides lookup by id, tag, and streamable flag.
- `novacore::assets::AssetStreamer` keeps a small priority queue for future async loads and streaming-zone preloads.

Expected manifest shape:

```json
{
  "manifest": {
    "name": "nemisis_core_assets",
    "root": "assets"
  },
  "assets": [
    {
      "id": "wpn_ar_01",
      "kind": "mesh",
      "source": "assets/source/blender/weapons/wpn_ar_01.blend",
      "cooked": "assets/export/gltf/weapons/wpn_ar_01.glb",
      "streamable": true,
      "priority": 80,
      "estimated_bytes": 262144,
      "tags": ["weapon", "dev"],
      "dependencies": ["mat_wpn_ar_polymer_dark"]
    }
  ]
}
```

Ownership boundaries:

- NovaCore owns manifest parsing, registry lookup, streaming queues, dependency representation, and future cooked asset handles.
- Nemisis owns concrete asset ids, game tags, weapon/character/environment categories, and tuning-specific asset catalogs.
- Blender tools may produce source/export/metadata files, but runtime loading must pass through NovaCore's asset registry and future importer.

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

Current streaming queue behavior:

- Duplicate requests coalesce by asset id.
- Higher priority requests win.
- Older requests win ties.
- Zone preloads can enqueue groups of asset ids.
- Actual async IO, glTF parse, GPU upload, residency, and unload are future renderer/asset-manager work.

## Acceptance

Pipeline is acceptable when:

- glTF content can be loaded.
- Missing assets fail loudly but gracefully.
- Data configs hot-reload.
- Stream zones are represented in data even before full large maps exist.
- Smoke tests cover manifest loading, registry lookup, tag filtering, streamable filtering, request coalescing, and priority popping.







