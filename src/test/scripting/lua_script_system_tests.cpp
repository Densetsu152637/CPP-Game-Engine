#include "../../scripting/lua_script_system.h"
#include "../test_assertions.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

void test_lua_script_system_lifecycle_and_engine_api()
{
    std::string last_log;
    std::optional<std::array<float, 3>> position;
    bool alive = false;
    int created = 0;
    int destroyed = 0;
    EngineScriptApi api;
    api.log = [&](const std::string_view message) { last_log.assign(message); };
    api.create_entity = [&] { ++created; alive = true; return INT64_C(73); };
    api.is_entity_alive = [&](const std::int64_t id) { return id == 73 && alive; };
    api.destroy_entity = [&](const std::int64_t id)
    {
        if (id != 73 || !alive) return false;
        alive = false;
        ++destroyed;
        return true;
    };
    api.set_position = [&](const std::int64_t id, const float x, const float y, const float z)
    {
        if (id != 73 || !alive) return false;
        position = std::array<float, 3>{x, y, z};
        return true;
    };
    api.get_position = [&](const std::int64_t id) { return id == 73 && alive ? position : std::nullopt; };

    LuaScriptSystem scripts(std::move(api));
    const auto loaded = scripts.load_string(R"lua(
        local entity_id
        return {
            on_create = function()
                entity_id = engine.create_entity()
                engine.set_position(entity_id, 1, 2, 3)
                engine.log("created")
            end,
            on_update = function(dt)
                local p = engine.get_position(entity_id)
                engine.set_position(entity_id, p.x + dt, p.y, p.z)
            end,
            on_destroy = function()
                engine.destroy_entity(entity_id)
            end
        }
    )lua", "lifecycle_test");
    test::require(loaded.has_value(), "valid lifecycle script should load");
    test::require(created == 1 && alive, "on_create should create a native engine entity");
    test::require(last_log == "created", "Lua should call the native logging service");
    test::require(position && (*position)[0] == 1.0f, "on_create should set native position");
    test::require(scripts.update(0.5f).has_value(), "valid on_update should run");
    test::require(position && (*position)[0] == 1.5f, "on_update should receive delta seconds and mutate native state");
    test::require(scripts.unload(*loaded).has_value(), "unload should invoke on_destroy");
    test::require(!alive && destroyed == 1, "on_destroy should release its native entity");
    test::require(!scripts.unload(*loaded), "unloading an unknown script should report an error");
    test::require(!scripts.load_string("return function(", "syntax_error"), "syntax errors should be reported");

    const auto path = std::filesystem::temp_directory_path() / "cpp_game_engine_lua_test.lua";
    {
        std::ofstream file(path, std::ios::binary);
        file << "return { on_create = function() engine.log('file') end }";
    }
    const auto from_file = scripts.load_file(path.string());
    std::filesystem::remove(path);
    test::require(from_file.has_value() && last_log == "file", "load_file should execute a file and its lifecycle");
    test::require(scripts.shutdown().has_value(), "shutdown should complete");
    test::require(!scripts.update(0.1f), "updates after shutdown should report an error");
}

void test_lua_script_system_reports_runtime_errors_and_cleans_up()
{
    int destroy_calls = 0;
    EngineScriptApi api;
    api.destroy_entity = [&](std::int64_t) { ++destroy_calls; return true; };
    LuaScriptSystem scripts(std::move(api));
    auto loaded = scripts.load_string(R"lua(
        return {
            on_update = function() error("update exploded") end,
            on_destroy = function() engine.destroy_entity(1) end
        }
    )lua");
    test::require(loaded.has_value(), "script with runtime lifecycle error should load");
    const auto update = scripts.update(0.016f);
    test::require(!update && update.error().find("update exploded") != std::string::npos,
        "Lua runtime errors should be returned to the engine");
    test::require(scripts.shutdown().has_value() && destroy_calls == 1,
        "shutdown should run on_destroy even after an update error");
}

void test_lua_script_system_cleans_up_failed_initialization()
{
    bool alive = false;
    int destroy_calls = 0;
    EngineScriptApi api;
    api.create_entity = [&] { alive = true; return INT64_C(19); };
    api.destroy_entity = [&](std::int64_t id)
    {
        if (id != 19 || !alive) return false;
        alive = false;
        ++destroy_calls;
        return true;
    };

    LuaScriptSystem scripts(std::move(api));
    const auto loaded = scripts.load_string(R"lua(
        local entity_id
        return {
            on_create = function()
                entity_id = engine.create_entity()
                error("initialization failed")
            end,
            on_destroy = function()
                engine.destroy_entity(entity_id)
            end
        }
    )lua");
    test::require(!loaded && loaded.error().find("initialization failed") != std::string::npos,
        "on_create errors should be reported");
    test::require(!alive && destroy_calls == 1,
        "on_destroy should clean up native state after a failed on_create");
}

void test_lua_script_system_validates_lifecycle_and_isolates_scripts()
{
    std::vector<std::string> messages;
    EngineScriptApi api;
    api.log = [&](const std::string_view message) { messages.emplace_back(message); };
    LuaScriptSystem scripts(std::move(api));

    test::require(!scripts.load_string("return true", "wrong_return"),
        "script chunks must return a lifecycle table");
    test::require(!scripts.load_string("return { on_create = 42 }", "wrong_lifecycle_type"),
        "lifecycle members must be functions when present");
    const auto custom_lookup = scripts.load_string(
        "return setmetatable({}, { __index = function() error('lookup should not run') end })", "custom_lookup");
    test::require(custom_lookup && scripts.unload(*custom_lookup),
        "lifecycle lookup should not invoke script metatable code outside a protected call");

    const auto first = scripts.load_string("return { on_update = function() engine.log('first') end }", "first");
    const auto second = scripts.load_string("return { on_update = function() engine.log('second') end }", "second");
    test::require(first && second && *first != *second, "a runtime should load multiple distinct script instances");
    test::require(scripts.update(0.01f).has_value(), "multiple script instances should update");
    test::require(messages == std::vector<std::string>{"first", "second"},
        "scripts should keep isolated state and update in load order");
}

void test_lua_script_system_translates_native_exceptions()
{
    EngineScriptApi api;
    api.log = [](std::string_view) { throw std::runtime_error("logger unavailable"); };
    LuaScriptSystem scripts(std::move(api));
    const auto loaded = scripts.load_string("return { on_create = function() engine.log('message') end }", "native_exception");
    test::require(!loaded && loaded.error().find("logger unavailable") != std::string::npos,
        "native callback exceptions should become Lua lifecycle errors");
    test::require(scripts.shutdown().has_value(), "shutdown should remain safe after a native exception");
}
