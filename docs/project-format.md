# Project and scene format, version 1

The engine's initial authoring contract is JSON. A project directory contains a `project.json` manifest, a startup scene, and source assets. Generated data belongs in a derived directory.

The manifest requires `schema`, `name`, and `startup_scene`. Optional `assets` entries declare stable IDs mapped to project-relative file paths and kinds. Optional `inputActions` maps action names to supported keys (`Right`, `Left`, `Up`, `Down`, `Space`).

```json
{
  "schema": 1,
  "name": "First Project",
  "startup_scene": "scenes/main.scene.json",
  "inputActions": { "move_right": "Right" },
  "assets": [
    { "id": "asset:player-script", "path": "scripts/player.lua", "kind": "script" },
    { "id": "asset:player-mesh", "path": "assets/player.mesh", "kind": "mesh" },
    { "id": "asset:player-texture", "path": "assets/player.ppm", "kind": "texture" }
  ]
}
```

A scene has `schema`, a persistent `scene_id`, and entities with unique persistent string IDs, names, and components:

```json
{
  "schema": 1,
  "scene_id": "scene:first-project",
  "entities": [{
    "id": "entity:player",
    "name": "Player",
    "components": {
      "Transform": { "position": [0.0, 0.0, 0.0] },
      "Script": { "asset": "asset:player-script" },
      "MeshRenderer": { "mesh": "asset:player-mesh", "texture": "asset:player-texture" }
    }
  }]
}
```

Asset IDs are stable authored identifiers. Moving or renaming an asset changes only its manifest `path`; scene references keep the same ID. Duplicate asset IDs and path collisions are detected case-insensitively; references use the exact declared ID spelling. Supported kinds are `script`, `mesh`, and `texture`; component references must match the declared kind. Entity IDs are independent of EnTT handles, which exist only for a runtime instance.

Positions are finite single-precision world coordinates in metres using a right-handed basis: X points right, Y up, and positive Z toward the viewer. These authored world coordinates are not Vulkan clip coordinates. The view-projection transform maps visible points to Vulkan clip space with X and Y in `[-w,w]` and depth in `[0,w]` (normalized depth `[0,1]`). With the renderer's positive-height viewport and top-left framebuffer origin, projection setup must flip clip-space Y so authored positive Y remains visually upward. The current schema has no camera component; camera/view-projection data remains runtime supplied. Version 1 has no rotation, parent, or local transform fields.

The parser rejects unknown fields and component types rather than dropping data on save. It rejects unsupported schema versions, duplicate IDs, malformed documents, invalid asset kinds/references, non-finite or out-of-range positions, missing files, absolute paths, `..` escapes, and symlink targets outside the project root. JSON uses the vendored [picojson](../third_party/picojson/picojson.h), pinned to upstream commit `111c9be5188f7350c2eac9ddaedd8cca3d7bf394`; its BSD-2-Clause license is alongside it.

Schema version 1 is the initial baseline and has no migration from earlier versions. Future format changes must define an explicit, tested migration; newer unsupported documents remain errors. Diagnostics have stable codes, source files, and field paths for CLI, editor, and automation clients.

