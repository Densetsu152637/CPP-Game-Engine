# Project and scene format, version 1

The engine's initial authoring contract is JSON. A project directory contains
one `project.json` manifest, a startup scene, and the source assets it
references. Paths are relative to that directory. Generated data belongs in a
derived directory and is not part of the scene document.

The manifest has a required schema version, display name, and startup scene:

```json
{
  "schema": 1,
  "name": "First Project",
  "startup_scene": "scenes/main.scene.json"
}
```

A scene has a required schema version, persistent scene ID, and entity list.
Each entity has a unique persistent string ID, a name, and a component object.
Version 1 registers only `Transform.position` and `Script.asset`:

```json
{
  "schema": 1,
  "scene_id": "scene:first-project",
  "entities": [
    {
      "id": "entity:player",
      "name": "Player",
      "components": {
        "Transform": { "position": [0.0, 0.0, 0.0] },
        "Script": { "asset": "scripts/player.lua" }
      }
    }
  ]
}
```

IDs are authored strings and are independent of EnTT handles, which exist only
for a loaded runtime instance. Scene positions are finite single-precision
world coordinates in metres using a right-handed basis: X points right, Y up,
and positive Z toward the viewer. Version 1 has no rotation, parent, or local
transform fields.

The parser rejects unknown fields and component types rather than dropping
data on save. It rejects unsupported schema versions, duplicate entity IDs,
malformed documents, non-finite or out-of-range positions, and missing script
files. Scene and script paths must be relative to the project root; absolute
paths, `..` escapes, and symlink targets outside the root are rejected. JSON
values are parsed with the vendored [picojson](../third_party/picojson/picojson.h)
header, pinned to upstream commit `111c9be5188f7350c2eac9ddaedd8cca3d7bf394`.
Its BSD-2-Clause license is in the same directory.

Schema version 1 is the initial baseline. It has no migration from earlier
versions. A future format change must define an explicit, tested migration;
documents with a version the current engine does not support remain errors.
Diagnostics have stable codes, a source file, and a field path so command-line,
editor, and automation clients can present the same validation result.
