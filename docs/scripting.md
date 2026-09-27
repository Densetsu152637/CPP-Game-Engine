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

`project::Runtime` instantiates the authored scene in an isolated ECS instance and advances it with a caller supplied fixed delta and input snapshot. When the project declares `inputActions`, those action names define the runtime allowlist; otherwise hosts can configure names through `RuntimeOptions::allowedActions` and the default allowlist is empty. A tick containing an undeclared action is rejected before applying pending reloads or advancing simulation. Runtime sends Lua `print` through its log callback and directs `io.write` to stderr so project output does not mix with machine-readable CLI output. Lifecycle and reload operations must run on the thread that constructed the Runtime; wrong-thread calls return a `runtime.thread.owner` diagnostic. A staged Lua replacement is loaded at the next tick boundary. If replacement initialization or the old script's `on_destroy` fails, Runtime reports the failure, retains the old script table where possible, and faults the runtime. No further simulation callback runs until the caller stops the session. `stop()` reports teardown errors while still destroying the isolated scene entities.

Lua callbacks can mutate ECS state, create entities, log, or perform other external effects before they return an error. Those effects can be partial and are not automatically rolled back, including when candidate `on_create` succeeds but the old script later fails `on_destroy`. Runtime cleans entities it owns when stopped, but cannot undo external side effects. Callers should treat lifecycle failure as a failed play session, stop it, inspect the returned diagnostic, and restart from the authored scene if needed. The authored Project/Scene remains separate from the runtime ECS instance.

Before startup creates runtime entities, it resolves, reads, and compiles every attached script. If an `on_create` callback later fails, the failed script receives `on_destroy`, Runtime removes the failed script and destroys authored and runtime-spawned entities before returning the setup diagnostic. Logs or other external effects already performed by that callback remain visible and cannot be rolled back. `liveEntityCount()` and `activeScriptCount()` expose the remaining runtime-owned state for diagnostics.
