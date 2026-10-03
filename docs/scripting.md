# Lua scripting

`LuaScriptSystem` owns one Lua 5.4 runtime and an isolated global environment for each loaded script. Each file or source chunk returns a table with optional `on_create()`, `on_update(delta_seconds)`, and `on_destroy()` functions. The system calls `on_create` after a successful load, calls every script's `on_update` from the engine's simulation tick, and calls `on_destroy` when a script unloads or the runtime shuts down. Lua errors are returned as `std::expected` errors for the host to log or handle.

The host provides native engine operations through `EngineScriptApi`. The Lua `engine` table exposes:

| Lua call | Behavior |
| --- | --- |
| `engine.log(message)` | Send a message to the engine log callback. |
| `engine.create_entity()` | Create an entity and return its opaque integer id. |
| `engine.is_alive(id)` | Check whether the host still recognizes that entity id. |
| `engine.destroy_entity(id)` | Destroy a live entity; returns whether it succeeded. |
| `engine.set_position(id, x, y, z)` | Update position through the host callback. |
| `engine.get_position(id)` | Return `{ x, y, z }`, or `nil` if unavailable. |

Entity identifiers belong to the engine. Scripts should retain them only for their own lifetime and check `engine.is_alive` before use. The injected callbacks keep Lua independent of the current ECS implementation and allow the engine to supply generation-checked handles.

Lifecycle operations (`load_file`, `load_string`, `update`, `unload`, and `shutdown`) cannot be nested from an `EngineScriptApi` callback while Lua is running. During shutdown, the runtime is marked unavailable before Lua close finalizers run, so lifecycle calls made by those finalizers are rejected (a repeated `shutdown` remains harmless).

The example in `assets/scripts/moving_entity.lua` creates an entity, moves it using frame delta time, and releases it during teardown. The focused C++ checks are in `src/test/scripting/lua_script_system_tests.cpp`; add that source to the project's scripting test target together with `src/scripting/lua_script_system.cpp` and the Lua 5.4 C source target.

## Entity-owned project scripts

Scripts attached to an authored entity also receive `self` and `input` in their private environment. `self.get_position()` returns the owning entity's `{ x, y, z }` position, and `self.set_position(x, y, z)` updates that owner. `input.pressed(action)`, `input.held(action)`, and `input.released(action)` query the input snapshot for the current simulation tick. The host supplies stable action names; Lua does not poll a window or retain a persistent entity ID. The example in `examples/first-project/scripts/player.lua` moves its authored Player when `move_right` is pressed.

Project scripts can register a typed ECS component while loading, for example
`engine.register_component("PlayerStats", {ticks = "number", enabled = "boolean"})`.
Fields accept `number`, `boolean`, or `string`; an existing schema must match.
For versioned fields and defaults, use
`engine.register_component("Vitals", {hp = {type = "number", version = 2, default = 100}, active = {type = "boolean", default = true}}, 2)`.
The final argument is the component schema version; it defaults to 1. Each field
version defaults to 1 and cannot exceed its component version. Omitted values
use declared defaults. Project registration allows at most 64 component schemas,
32 fields per schema, and 512 fields total. Schema and field names are limited
to 64 ASCII bytes; string values and defaults are limited to 4096 bytes, and
number values must be finite.
`engine.set_component(entity, name, values)`, `engine.get_component(entity, name)`,
and `engine.remove_component(entity, name)` operate on entity handles. The
corresponding `self` methods omit the entity argument and use the authored owner.

A returned script table may include `systems = {{name = "Count", all =
{"PlayerStats"}, reads = {"PlayerStats"}, writes = {"PlayerStats"},
update = function(entity, dt) ... end}}`. `all` selects entities; every queried
component must appear in `reads` or `writes`. `phase` and `order` are optional
integer scheduling keys. Systems with conflicting access at the same phase and
order are rejected. Queries visit packed entity handles in sorted order, and a
system may only read or write the component names it declared. The complete
sample is [player.lua](../examples/first-project/scripts/player.lua).

Project Runtime scripts execute in a restricted global environment, both during
declaration validation and play. They may use deterministic Lua math, string, table, and UTF-8
helpers, safe `print`/`io.write` logging, and `require` for Lua modules confined
to the project root. `math.random`, `math.randomseed`, and `coroutine` are unavailable,
including inside callbacks and required modules. Direct file and OS APIs, the ordinary Lua package loader,
debug APIs, and dynamic code loading are unavailable. Legacy standalone Engine
scripts have a separate host contract.

Project modules can use the same engine and desktop service tables as their
calling lifecycle script. Cached module functions resolve `self` and `input`
from the current callback's entity context on every call. Calling `self` without
an active owner reports an unavailable-context error. Declaration validation
still rejects service side effects, including calls made while requiring a module.

`project::Runtime` instantiates the authored scene in an isolated ECS instance and advances it with a caller supplied fixed delta and input snapshot. When the project declares `inputActions`, those action names define the runtime allowlist; otherwise hosts can configure names through `RuntimeOptions::allowedActions` and the default allowlist is empty. A tick containing an undeclared action is rejected before applying pending reloads or advancing simulation. Runtime sends Lua `print` through its log callback and directs `io.write` to stderr so project output does not mix with machine-readable CLI output. Lifecycle and reload operations must run on the thread that constructed the Runtime; wrong-thread calls return a `runtime.thread.owner` diagnostic. A staged Lua replacement is loaded at the next tick boundary. If replacement initialization or the old script's `on_destroy` fails, Runtime reports the failure, retains the old script table where possible, and faults the runtime. No further simulation callback runs until the caller stops the session. `stop()` reports teardown errors while still destroying the isolated scene entities.

Structural ECS changes requested during a tick become visible at its successful
boundary. If a callback fails, Runtime discards pending structural changes and
faults the play session. Lua-local state, logs, direct position updates, and
external effects already performed by a callback cannot be rolled back. Runtime
cleans entities it owns when stopped, but cannot undo external side effects.
Callers should stop a faulted session, inspect its diagnostic, and restart from
the authored scene if needed. The authored Project/Scene remains separate from
the runtime ECS instance.

## Desktop gameplay services

Project entities expose `self.id()`, `self.move(dx,dy)`, `self.overlaps()`, and
`self.safe_position(radius,step)`. The corresponding engine methods accept an
opaque entity handle first. `engine.find_entity(persistentId)` returns a live
handle or nil. Project Runtime maps these handles to generation-checked ECS
entities and never reuses a public handle within that Runtime, including across
scene changes. They are not persistent save identifiers. `move` returns
`{x,y,contacts={persistentIds}}`; `safe_position` returns `{x,y}` or nil when no
sampled safe location exists. Use collision movement for exploration; direct
`set_position` remains an explicit teleport and must be paired with safe-placement
validation when restoring checkpoints.

`input.value(action)` returns the gameplay analog value. `input.pointer()` and
`input.wheel()` return `{x,y}` in logical UI pixels and wheel steps respectively.
Pressed/held/released and analog/pointer data are consumed by modal UI and control
locks. A modal open or close during a callback suppresses subsequent gameplay
queries immediately and quarantines held controls until release.

| Call | Contract |
| --- | --- |
| `engine.change_scene(sceneAsset, spawnId?, travellerId?)` | Queue a transition for the successful tick boundary; optional persistent traveller is placed at the target spawn marker. |
| `engine.sprite_frame(handle, zeroBasedFrame)` | Freeze a sprite at a validated atlas frame. |
| `engine.sprite_visible(handle, visible)` | Change runtime sprite visibility. |
| `engine.triggers()` | Previous completed tick's sorted `{first,second,phase}` events; phase is enter, stay, or exit. |
| `engine.lock_controls(key, locked)` | Acquire/release a named gameplay-input lock, limited to 64 keys of at most 64 bytes. |
| `engine.set_camera(x,y)` / `engine.reset_camera()` | Override/reset the runtime camera center in world units. |
| `ui.open(panel)` | Open a panel with id, font asset, text, x/y/width/height, optional scale, modal flag, and choices array. |
| `ui.close(id)`, `ui.set_text(id,text)`, `ui.scroll(id,pixels)` | Change a panel and return whether it exists and accepts the change. |
| `ui.event()` | Poll `{panel,type,selection}` with 1-based choice selection, or nil. |
| `audio.play(asset,loop?,bus?,gain?)` | Play a WAV and return its voice handle; defaults are false, effects, and 1. |
| `audio.stop(voice)` / `audio.volume(bus,gain)` | Stop a voice or set master/music/effects/dialogue gain in `[0,1]`. |
| `state.read()` / `state.write(object)` | Read/replace bounded session JSON shared by every entity in the current Runtime and preserved across room changes. |
| `save.read(slot,backup?)` / `save.write(slot,object)` | Read/write a versioned durable player snapshot through the host-selected data root. |
| `save.recover(slot)` | Explicitly replace a corrupted primary with a validated backup; newer formats remain protected. |
| `settings.read()` / `settings.write(object)` | Load or persist/apply separate input bindings and audio gains. |

UI defaults use actions `ui_confirm`, `ui_back`, `ui_up`, `ui_down`, and
`ui_click`; declare the ones used in the manifest. UI font metrics come from the
host's bitmap atlas so wrapping, scrolling and visible glyph placement agree.
Settings use `{bindings={action={physicalTokens}},audio={master=0.8,music=0.5}}`.
Bindings use the complete desired action map and cannot introduce undeclared
actions. Settings validate before writing and apply to the live mapper/mixer
after a successful durable write. Startup loads them independently of game saves;
invalid settings report a diagnostic log while preserving default bindings.

Service operations return `nil,error` for expected operational failures. JSON
conversion limits, cycles, incompatible table shapes, and oversized host reads
also return `nil,error` without writing data. Both conversion directions reserve
Lua stack capacity and apply the same bounds before publishing tables. Malformed
Lua values and unavailable injected services raise script errors. Save/session
objects permit finite numbers, booleans, strings, nested string-keyed objects and
dense arrays, bounded to 1 MiB, 24 nesting levels, 65,536 nodes, and 64 KiB per
string. JSON null, cycles, sparse arrays, mixed key types and function/userdata values are
rejected. Lua still has no direct filesystem or OS access. Call `state.write({})`
to explicitly reset session progression; game-specific reducers/events remain
authored Lua responsibilities.

Scene preparation uses an isolated ECS/Lua state and a copy of shared session
JSON. The previous scene remains playable if loading, startup or safe placement
fails. Candidate `on_create` may prepare local UI and session state, but audio
mutations, save writes/recovery and settings writes return errors until commit;
perform these effects in the first accepted `on_update`. Declaration validation
never invokes lifecycle functions or runtime services. Successful commit retires
the previous scene's scripts, UI, control locks and audio voices. Teardown errors
are reported and leave the source stopped; effects already performed by a failing
`on_destroy` cannot be rolled back. Script faults clear modal UI, control locks,
camera overrides and scene-owned voices before returning the error.

Before startup creates runtime entities, it resolves, reads, and compiles every attached script. If an `on_create` callback later fails, the failed script receives `on_destroy`, Runtime removes the failed script and destroys authored and runtime-spawned entities before returning the setup diagnostic. Logs or other external effects already performed by that callback remain visible and cannot be rolled back. `liveEntityCount()` and `activeScriptCount()` expose the remaining runtime-owned state for diagnostics.
