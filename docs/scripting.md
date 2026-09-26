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
