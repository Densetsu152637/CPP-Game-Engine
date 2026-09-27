# First Project

This project is the first authored scene example. `project.json` points to
`scenes/main.scene.json`, which creates one persistent entity ID,
`entity:player`, with a transform and the entity-owned Lua script in
`scripts/player.lua`, cataloged by stable asset ID `asset:player-script` in the
project manifest. The same manifest maps the authored `move_right` action to
Right Arrow and catalogs the player's mesh and texture; the scene references
those assets by stable ID through `MeshRenderer`.

The script uses `self` to read and update its authored owner, so it does not
create a duplicate player. It responds to the `move_right` action supplied by
the host as a pressed input event. The action is sampled once for a simulation
tick; scripts never poll the window directly.
The sample runtime's `move_right` action is mapped from the authored
`inputActions` entry to Right Arrow in visible mode and can be injected directly
in headless fixtures.

From the repository root, validate and inspect the data with:

```text
CPPGameEngine project validate examples/first-project/project.json --format json
CPPGameEngine scene inspect examples/first-project/scenes/main.scene.json --project-root examples/first-project --format json
```

Run a bounded headless session with a fixed 60 Hz simulation step:

```text
CPPGameEngine run examples/first-project/project.json --headless --ticks 120 --format json
```

To inject a deterministic action at zero-based tick 0, use
`--input move_right:0`. The host maps the same action to Right Arrow in
windowed play; the scene and script files do not change between modes. The
sample contains no random behavior, so identical input snapshots and tick
counts produce the same authored position sequence on the same build.

Edit the JSON scene or Lua script, then validate and run again. The
`player_reload.lua` file is a replacement-script fixture used to verify that a
compile-checked reload becomes active at a tick boundary.
If a lifecycle callback fails, the isolated runtime faults and should be stopped
before another session starts; the authored project files remain unchanged.
