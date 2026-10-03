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

`componentDescriptors()` registers serialized component names, component versions,
property names/types/defaults, and required fields. Loading and saving dispatch
through registered component handlers; the editor uses the same property metadata.
An omitted component `version` means version 1; saving writes `"version":1`
explicitly. The shared edit API supports typed position, script-reference, and
mesh-renderer changes with expected revisions. Injected serialization and replace
failures are tested to preserve the original scene bytes.

## Desktop 2D components

Schema 1 additionally accepts version 1 `SpriteRenderer`, `Camera2D`, and
`Collider2D`. Legacy documents retain their existing behavior; an older engine
rejects these new names explicitly. Unknown fields and unsupported component
versions remain errors. Saving preserves these components through the shared
registry even when the native editor has no dedicated control for a property.

`SpriteRenderer` requires a texture asset ID. Optional `size` is a positive
world-unit pair (default `[1,1]`), `pivot` is normalized from left/bottom
(`[0.5,0.5]`), and `tint` is RGBA in `[0,1]` (`[1,1,1,1]`). `source` and each
`frames` item are `[x,y,width,height]` atlas rectangles measured in texture pixels
from its top-left. An omitted source covers the texture. `framesPerSecond`
defaults to zero, `loop` to true, `layer` to zero, and `visible` to true. Atlas
frames are limited to 4096 and positive dimensions within the 8192 pixel limit.
Sprites require `Transform`.

`Camera2D` supplies `logicalSize` (`[320,180]`), `pixelsPerUnit` (`16`), world-unit
`center` (`[0,0]`), optional `follow` persistent entity ID, and `pixelSnap`
(`true`). A scene permits one camera. A follow target must have a Transform in
the same scene. Integer framebuffer scaling preserves authored pixels, with
letterboxing; a framebuffer smaller than the logical size uses uniform
fractional fit. Camera snapping does not change simulation positions.

`Collider2D` requires positive world-unit `size`. Its optional `offset` is
`[0,0]`, `trigger` is false, `layer` is the nonzero unsigned bitmask `1`, and
`mask` is `4294967295`. Bodies are positioned by their Transform and the offset.
Layer/mask values filter collision and trigger pairs.

The asset catalog additionally accepts `font`, `audio`, and `scene`. References
remain confined to the project root and retain the existing stable-ID rules.
Font assets are bitmap-atlas JSON and audio assets are WAV. Scene assets point
to authored scene JSON for runtime room changes.

The optional manifest `inputBindings` object maps action names to arrays of
physical tokens, for example `{"move_up":["Key:W","Gamepad:LeftYNegative"]}`.
Tokens come from the shared desktop binding registry; mouse buttons, wheel,
keyboard keys, gamepad buttons and signed axes are supported. Each action has
1–16 unique tokens, with at most 128 actions. Legacy `inputActions` remains
supported. Explicit inputBindings for an action override its legacy binding.

