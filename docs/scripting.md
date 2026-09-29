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

`project::Runtime` instantiates the authored scene in an isolated ECS instance and advances it with a caller supplied fixed delta and input snapshot. When the project declares `inputActions`, those action names define the runtime allowlist; otherwise hosts can configure names through `RuntimeOptions::allowedActions` and the default allowlist is empty. A tick containing an undeclared action is rejected before applying pending reloads or advancing simulation. Runtime sends Lua `print` through its log callback and directs `io.write` to stderr so project output does not mix with machine-readable CLI output. Lifecycle and reload operations must run on the thread that constructed the Runtime; wrong-thread calls return a `runtime.thread.owner` diagnostic. A staged Lua replacement is loaded at the next tick boundary. If replacement initialization or the old script's `on_destroy` fails, Runtime reports the failure, retains the old script table where possible, and faults the runtime. No further simulation callback runs until the caller stops the session. `stop()` reports teardown errors while still destroying the isolated scene entities.

Structural ECS changes requested during a tick become visible at its successful
boundary. If a callback fails, Runtime discards pending structural changes and
faults the play session. Lua-local state, logs, direct position updates, and
external effects already performed by a callback cannot be rolled back. Runtime
cleans entities it owns when stopped, but cannot undo external side effects.
Callers should stop a faulted session, inspect its diagnostic, and restart from
the authored scene if needed. The authored Project/Scene remains separate from
the runtime ECS instance.

Before startup creates runtime entities, it resolves, reads, and compiles every attached script. If an `on_create` callback later fails, the failed script receives `on_destroy`, Runtime removes the failed script and destroys authored and runtime-spawned entities before returning the setup diagnostic. Logs or other external effects already performed by that callback remain visible and cannot be rolled back. `liveEntityCount()` and `activeScriptCount()` expose the remaining runtime-owned state for diagnostics.
