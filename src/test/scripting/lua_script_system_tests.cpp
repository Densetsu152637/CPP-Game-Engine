#include "../../scripting/lua_script_system.h"
#include "../../scripting/lua_source_file.h"
#include "../../ecs/ecs.h"
#include "../test_assertions.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
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

    EngineScriptApi empty_error_api;
    empty_error_api.log = [](std::string_view) { throw std::runtime_error(""); };
    LuaScriptSystem empty_error_scripts(std::move(empty_error_api));
    const auto empty_error = empty_error_scripts.load_string(
        "return { on_create = function() engine.log('message') end }", "empty_native_exception");
    test::require(!empty_error, "an empty native exception message must still become a Lua lifecycle error");
}

void test_lua_script_system_rejects_reentrant_lifecycle_changes()
{
    LuaScriptSystem* system = nullptr;
    LuaScriptId target;
    bool load_blocked = false;
    bool update_blocked = false;
    bool unload_blocked = false;
    bool shutdown_blocked = false;
    EngineScriptApi api;
    api.log = [&](std::string_view)
    {
        if (!system) return;
        const auto nested_load = system->load_string("return {}", "nested");
        const auto nested_update = system->update(0.0f);
        const auto nested_unload = system->unload(target);
        const auto nested_shutdown = system->shutdown();
        load_blocked = !nested_load && nested_load.error().find("during Lua execution") != std::string::npos;
        update_blocked = !nested_update && nested_update.error().find("during Lua execution") != std::string::npos;
        unload_blocked = !nested_unload && nested_unload.error().find("during Lua execution") != std::string::npos;
        shutdown_blocked = !nested_shutdown && nested_shutdown.error().find("during script execution") != std::string::npos;
    };

    LuaScriptSystem scripts(std::move(api));
    system = &scripts;
    auto loaded = scripts.load_string("return { on_update = function() engine.log('reenter') end }", "reentrant");
    test::require(loaded.has_value(), "reentrancy test script should load");
    target = *loaded;
    test::require(scripts.update(0.016f).has_value(), "outer update should finish when nested mutations are rejected");
    test::require(load_blocked && update_blocked && unload_blocked && shutdown_blocked,
        "load, update, unload, and shutdown should reject reentrant calls from native callbacks");
    test::require(scripts.unload(target).has_value(), "script should remain valid for normal unload after callback");
}

void test_lua_script_system_guards_lua_close_finalizers()
{
    LuaScriptSystem* system = nullptr;
    LuaScriptId target;
    bool finalizer_ran = false;
    bool lifecycle_calls_rejected = false;
    EngineScriptApi api;
    api.log = [&](std::string_view message)
    {
        if (message != "gc" || !system) return;
        finalizer_ran = true;
        const auto nested_load = system->load_string("return {}", "finalizer_nested");
        const auto nested_update = system->update(0.0f);
        const auto nested_unload = system->unload(target);
        const auto nested_shutdown = system->shutdown();
        lifecycle_calls_rejected = !nested_load && !nested_update && !nested_unload && nested_shutdown;
    };

    LuaScriptSystem scripts(std::move(api));
    system = &scripts;
    auto loaded = scripts.load_string(
        "local finalizer = setmetatable({}, { __gc = function() engine.log('gc') end }); return { finalizer = finalizer }",
        "close_finalizer");
    test::require(loaded.has_value(), "finalizer test script should load");
    target = *loaded;
    test::require(scripts.shutdown().has_value(), "runtime should shut down through Lua close finalizers");
    test::require(finalizer_ran, "Lua close should run the finalizer and invoke the native callback");
    test::require(lifecycle_calls_rejected,
        "finalizer callbacks should not reenter lifecycle operations on a closing runtime");
}

void test_lua_dynamic_components_and_systems()
{
    std::map<std::int64_t, double> health;
    std::vector<std::int64_t> writeOrder;
    std::vector<LuaComponentField> registered;
    EngineScriptApi api;
    api.register_component = [&](std::string_view name, const std::vector<LuaComponentField>& fields)
    {
        if (name != "Health") return false;
        registered = fields;
        return fields.size() == 1 && fields[0].name == "hp" && fields[0].type == LuaComponentFieldType::Number;
    };
    api.set_component = [&](std::int64_t entity, std::string_view name, const LuaComponentValues& values)
    {
        if (name != "Health" || values.size() != 1 || values[0].first != "hp" ||
            !std::holds_alternative<double>(values[0].second)) return false;
        health[entity] = std::get<double>(values[0].second);
        writeOrder.push_back(entity);
        return true;
    };
    api.get_component = [&](std::int64_t entity, std::string_view name) -> std::optional<LuaComponentValues>
    {
        const auto found = health.find(entity);
        if (name != "Health" || found == health.end()) return std::nullopt;
        return LuaComponentValues{{"hp", found->second}};
    };
    api.query_components = [&](const std::vector<std::string>& required)
    {
        if (required != std::vector<std::string>{"Health"}) return std::vector<std::int64_t>{};
        return std::vector<std::int64_t>{4, 2}; // scripting scheduler must impose stable entity order
    };
    LuaScriptSystem scripts(std::move(api));
    const auto loaded = scripts.load_string(R"lua(
        assert(engine.register_component('Health', { hp = 'number' }))
        return {
            on_create = function()
                assert(engine.set_component(4, 'Health', { hp = 4 }))
                assert(engine.set_component(2, 'Health', { hp = 2 }))
            end,
            systems = {{ name = 'regenerate', all = {'Health'}, reads = {'Health'}, writes = {'Health'}, update = function(entity, dt)
                local value = engine.get_component(entity, 'Health')
                assert(engine.set_component(entity, 'Health', { hp = value.hp + dt }))
            end }}
        }
    )lua", LuaScriptContext{}, "dynamic_components");
    test::require(loaded.has_value(), "Lua should register custom component schemas and system definitions");
    test::require(registered.size() == 1, "schema registration should forward typed field definitions");
    writeOrder.clear();
    // Track visit order through an injected Lua-visible logger without exposing a per-row table scan.
    const auto system = scripts.load_string("return {}", "noop");
    test::require(system.has_value(), "unrelated script should coexist with registered systems");
    test::require(scripts.update(0.5f).has_value(), "registered component systems should execute");
    test::require(health[2] == 2.5 && health[4] == 4.5,
        "systems should read and write native component values for each matching entity");
    test::require(writeOrder == std::vector<std::int64_t>{2, 4},
        "systems should visit query results in deterministic entity order");
    test::require(scripts.unload(*loaded).has_value(), "unloading should release retained system callbacks");

    double ownerValue = 0;
    EngineScriptApi ownerApi;
    ownerApi.register_component = [](std::string_view, const std::vector<LuaComponentField>&) { return true; };
    ownerApi.set_component = [&](std::int64_t entity, std::string_view, const LuaComponentValues& values)
    { if (entity == 91) ownerValue = std::get<double>(values[0].second); return entity == 91; };
    ownerApi.get_component = [&](std::int64_t entity, std::string_view) -> std::optional<LuaComponentValues>
    { return entity == 91 ? std::optional<LuaComponentValues>{{{"hp", ownerValue}}} : std::nullopt; };
    LuaScriptSystem ownerScripts(std::move(ownerApi));
    const auto owner = ownerScripts.load_string(R"lua(
        engine.register_component('Health', {hp='number'})
        return { on_create=function()
            assert(self.set_component('Health', {hp=8}))
            assert(self.get_component('Health').hp == 8)
        end }
    )lua", LuaScriptContext{.owner = 91}, "owner_component");
    test::require(owner && ownerValue == 8, "entity-owned scripts should access their owner component through self");
}

void test_lua_system_access_order_and_schema_ownership()
{
    std::set<std::string> schemas;
    std::vector<std::string> events;
    EngineScriptApi api;
    api.register_component = [&](std::string_view name, const std::vector<LuaComponentField>&)
    { schemas.insert(std::string(name)); return true; };
    api.has_component_schema = [&](std::string_view name) { return schemas.contains(std::string(name)); };
    api.unregister_component = [&](std::string_view name) { return schemas.erase(std::string(name)) != 0; };
    api.rollback_component_schema = api.unregister_component;
    api.query_components = [](const std::vector<std::string>&) { return std::vector<std::int64_t>{1}; };
    api.set_component = [&](std::int64_t, std::string_view, const LuaComponentValues&)
    { events.emplace_back("write"); return true; };
    api.log = [&](std::string_view text) { events.emplace_back(text); };
    LuaScriptSystem scripts(std::move(api));

    const auto bad = scripts.load_string(R"lua(
        assert(engine.register_component('Candidate', {value='number'}))
        return {systems={{name='bad', all={'Candidate'}, reads={}, writes={}, update=function() end}}}
    )lua", "bad_candidate");
    test::require(!bad && !schemas.contains("Candidate"),
        "failed system parsing should roll back a candidate-only schema");

    const auto first = scripts.load_string(R"lua(
        assert(engine.register_component('Shared', {value='number'}))
        return {systems={{name='writer', all={'Shared'}, reads={}, writes={'Shared'},
            phase=1, order=2, update=function(entity)
                engine.log('writer')
                assert(engine.set_component(entity, 'Shared', {value=1}))
            end}}}
    )lua", "writer");
    test::require(first && schemas.contains("Shared"), "first owner should register its schema");
    const auto conflict = scripts.load_string(R"lua(return {systems={{name='conflict',
        all={'Shared'}, reads={'Shared'}, writes={}, phase=1, order=2,
        update=function() end}}})lua", "conflict");
    test::require(!conflict && conflict.error().find("conflicting") != std::string::npos,
        "unordered same-phase read/write conflicts should be rejected");
    const auto second = scripts.load_string(R"lua(
        assert(engine.register_component('Shared', {value='number'}))
        return {systems={{name='reader', all={'Shared'}, reads={'Shared'}, writes={},
            phase=0, order=0, update=function() engine.log('reader') end}}}
    )lua", "reader");
    test::require(second.has_value(), "a declared earlier phase should resolve the access conflict");
    test::require(scripts.update(0.01f).has_value(), "ordered systems should run");
    test::require(events == std::vector<std::string>{"reader", "writer", "write"},
        "systems should execute in phase/order sequence");
    test::require(scripts.unload(*first) && schemas.contains("Shared"),
        "unloading one owner must retain a shared schema");
    test::require(scripts.unload(*second) && !schemas.contains("Shared"),
        "unloading the last owner should release an unused schema");

    const auto undeclared = scripts.load_string(R"lua(return {systems={{name='undeclared',
        all={'Shared'}, reads={'Shared'}, writes={}, update=function(entity)
            engine.set_component(entity, 'Shared', {value=2})
        end}}})lua", "undeclared");
    test::require(undeclared && !scripts.update(0.01f),
        "a system must not write a component absent from its declared writes");

    ECS ecs;
    const Entity owner = ecs.createEntity();
    EngineScriptApi native;
    native.register_component = [&](std::string_view name, const std::vector<LuaComponentField>&)
    { return ecs.registerDynamicComponent(name, {{"value", ecs::DynamicFieldType::Number}}); };
    native.has_component_schema = [&](std::string_view name)
    { return ecs.hasDynamicComponentSchema(name); };
    native.unregister_component = [&](std::string_view name)
    { return ecs.unregisterDynamicComponent(name); };
    native.rollback_component_schema = [&](std::string_view name)
    { return ecs.rollbackDynamicComponentSchema(name); };
    native.set_component = [&](std::int64_t packed, std::string_view name, const LuaComponentValues& values)
    { return ecs.setDynamicComponent(Entity::fromPacked(static_cast<std::uint64_t>(packed)), name, values); };
    LuaScriptSystem nativeScripts(std::move(native));
    const auto failedWithRow = nativeScripts.load_string(R"lua(
        assert(engine.register_component('Temporary', {value='number'}))
        return {on_create=function()
            assert(self.set_component('Temporary', {value=7}))
            error('candidate failed')
        end}
    )lua", LuaScriptContext{.owner = static_cast<std::int64_t>(owner.packed())}, "row_rollback");
    test::require(!failedWithRow && !ecs.hasDynamicComponentSchema("Temporary") &&
        !ecs.getDynamicComponent(owner, "Temporary"),
        "failed candidate loading should remove both its new schema and native component rows");
    const auto discarded = nativeScripts.load_string(R"lua(
        assert(engine.register_component('ReplacementOnly', {value='number'}))
        return {on_create=function() assert(self.set_component('ReplacementOnly', {value=4})) end}
    )lua", LuaScriptContext{.owner = static_cast<std::int64_t>(owner.packed())}, "discarded_candidate");
    test::require(discarded && nativeScripts.discardCandidate(*discarded) &&
        !ecs.hasDynamicComponentSchema("ReplacementOnly") &&
        !ecs.getDynamicComponent(owner, "ReplacementOnly"),
        "discarding a prepared replacement should clear its candidate-only schema and rows");
}

void test_lua_position_access_declarations()
{
    const auto exercise = [](std::string_view callback, std::string_view reads,
        std::string_view writes, const bool allowed, int expectedReads, int expectedWrites)
    {
        int readCalls = 0, writeCalls = 0;
        EngineScriptApi api;
        api.query_components = [](const std::vector<std::string>&)
        { return std::vector<std::int64_t>{1}; };
        api.get_position = [&](std::int64_t)
        { ++readCalls; return std::optional<std::array<float, 3>>{{1.0f, 2.0f, 3.0f}}; };
        api.set_position = [&](std::int64_t, float, float, float)
        { ++writeCalls; return true; };
        LuaScriptSystem scripts(std::move(api));
        const std::string source = "return {systems={{name='position', all={'Health'}, reads=" +
            std::string(reads) + ", writes=" + std::string(writes) +
            ", update=function(entity) " + std::string(callback) + " end}}}";
        const auto loaded = scripts.load_string(source, LuaScriptContext{.owner = 1}, "position_access");
        test::require(loaded.has_value(), "position access test system should parse");
        const auto result = scripts.update(0.01f);
        test::require(result.has_value() == allowed, "position APIs should enforce declared system access");
        test::require(readCalls == expectedReads && writeCalls == expectedWrites,
            "rejected position access should not call the native host");
    };
    exercise("engine.get_position(entity)", "{'Health'}", "{}", false, 0, 0);
    exercise("engine.set_position(entity, 1, 2, 3)", "{'Health'}", "{}", false, 0, 0);
    exercise("self.get_position()", "{'Health'}", "{}", false, 0, 0);
    exercise("self.set_position(1, 2, 3)", "{'Health'}", "{}", false, 0, 0);
    exercise("engine.get_position(entity); self.get_position(); "
        "engine.set_position(entity, 1, 2, 3); self.set_position(1, 2, 3)",
        "{'Health', 'Position3D'}", "{'Position3D'}", true, 2, 2);

    EngineScriptApi api;
    api.query_components = [](const std::vector<std::string>&)
    { return std::vector<std::int64_t>{1}; };
    LuaScriptSystem scripts(std::move(api));
    const auto writer = scripts.load_string(R"lua(return {systems={{name='writer',
        all={'Health'}, reads={'Health'}, writes={'Position3D'},
        update=function() end}}})lua", "position_writer");
    const auto reader = scripts.load_string(R"lua(return {systems={{name='reader',
        all={'Health'}, reads={'Health','Position3D'}, writes={},
        update=function() end}}})lua", "position_reader");
    test::require(writer && !reader && reader.error().find("conflicting") != std::string::npos,
        "unordered same-phase Position3D read/write conflict must be rejected");
}

void test_lua_project_declaration_preflight()
{
    using Sources = std::vector<std::pair<std::string, std::string>>;
    const auto valid = LuaScriptSystem::validate_declarations(Sources{{R"lua(
        assert(engine.register_component('Health', {hp='number'}))
        return {on_create=function() error('must not execute during validation') end,
            systems={{name='heal', all={'Health'}, reads={'Health','Position3D'},
                writes={'Health'}, phase=1, order=2, update=function() end}}}
    )lua", "@valid.lua"}}, std::filesystem::current_path());
    test::require(valid.has_value(), "declaration preflight should inspect valid systems without running on_create");
    const auto badAccess = LuaScriptSystem::validate_declarations(Sources{{R"lua(
        assert(engine.register_component('Health', {hp='number'}))
        return {systems={{name='bad', all={'Health'}, reads={}, writes={}, update=function() end}}}
    )lua", "@bad-access.lua"}}, std::filesystem::current_path());
    test::require(!badAccess && badAccess.error().starts_with("@bad-access.lua: "),
        "invalid system access should report its source path");
    const auto badPhase = LuaScriptSystem::validate_declarations(Sources{{R"lua(
        assert(engine.register_component('Health', {hp='number'}))
        return {systems={{name='bad', all={'Health'}, reads={'Health'}, writes={},
            phase='late', update=function() end}}}
    )lua", "@bad-phase.lua"}}, std::filesystem::current_path());
    test::require(!badPhase && badPhase.error().find("phase") != std::string::npos,
        "noninteger system phases should fail declaration preflight");
    const auto conflict = LuaScriptSystem::validate_declarations(Sources{
        {R"lua(assert(engine.register_component('Health', {hp='number'}))
            return {systems={{name='first', all={'Health'}, reads={}, writes={'Health'},
                update=function() end}}})lua", "@first.lua"},
        {R"lua(return {systems={{name='second', all={'Health'}, reads={'Health'}, writes={},
            update=function() end}}})lua", "@second.lua"}}, std::filesystem::current_path());
    test::require(!conflict && conflict.error().starts_with("@second.lua: "),
        "cross-script same-phase conflicts should identify the second declaration");
    const auto blockedIo = LuaScriptSystem::validate_declarations(Sources{{
        "os.execute('echo forbidden'); return {}", "@unsafe.lua"}}, std::filesystem::current_path());
    test::require(!blockedIo, "declaration preflight should not expose host OS APIs");
    const auto allowedLog = LuaScriptSystem::validate_declarations(Sources{{
        "print('print log'); io.write('io log\\n'); io.stdout:write('stream log\\n'); return {}",
        "@logging.lua"}}, std::filesystem::current_path());
    test::require(allowedLog.has_value(), "top-level log calls should validate without writing output");
    const auto blockedFile = LuaScriptSystem::validate_declarations(Sources{{
        "io.open('outside.txt', 'w'); return {}", "@file-io.lua"}}, std::filesystem::current_path());
    test::require(!blockedFile, "preflight must not expose file I/O while accepting log calls");
    const std::string paritySource = R"lua(
        assert(os == nil and io.open == nil and package == nil and debug == nil and load == nil)
        assert(math.random == nil and math.randomseed == nil and math.abs(-4) == 4)
        print('print log')
        io.write('io log\n')
        io.stdout:write('stream log\n')
        return {on_update=function()
            assert(math.random == nil and math.randomseed == nil and math.floor(1.7) == 1)
            engine.log('callback-safe')
        end}
    )lua";
    const auto root = std::filesystem::current_path();
    const std::string rootChunk = "@" + (root / "safe-root.lua").string();
    test::require(LuaScriptSystem::validate_declarations(Sources{{paritySource, rootChunk}}, root).has_value(),
        "preflight should accept safe top-level branches and silent logging");
    std::vector<std::string> runtimeLogs;
    EngineScriptApi safeHost;
    safeHost.module_root = root;
    safeHost.log = [&](std::string_view message) { runtimeLogs.emplace_back(message); };
    safeHost.redirect_standard_output = true;
    LuaScriptSystem safeRuntime(std::move(safeHost));
    test::require(safeRuntime.load_string(paritySource, rootChunk).has_value() &&
        runtimeLogs.size() == 3 && runtimeLogs[0] == "print log" &&
        runtimeLogs[1].starts_with("io log") && runtimeLogs[2].starts_with("stream log"),
        "runtime should take the same restricted branch and route safe log writes");
    test::require(safeRuntime.update(0.01f).has_value() && runtimeLogs.back() == "callback-safe",
        "project callbacks should retain deterministic math while random APIs remain unavailable");
    test::require(!safeRuntime.load_string("io.open('outside.txt', 'w'); return {}", rootChunk),
        "runtime root scripts must reject file I/O just like preflight");
    const auto randomBranch = LuaScriptSystem::validate_declarations(Sources{{
        "math.randomseed(1); return {}", rootChunk}}, root);
    test::require(!randomBranch && !safeRuntime.load_string("math.random(); return {}", rootChunk),
        "root declarations must not access RNG functions in either Lua state");
    const auto bounded = LuaScriptSystem::validate_declarations(Sources{{
        "while true do end; return {}", "@loop.lua"}}, std::filesystem::current_path());
    test::require(!bounded && bounded.error().find("instruction limit") != std::string::npos,
        "declaration preflight should bound top-level execution");

    const auto directory = std::filesystem::current_path() / "build" / "declaration-module-fixture";
    const auto scriptDirectory = directory / "scripts";
    const auto sharedDirectory = directory / "shared";
    std::filesystem::create_directories(scriptDirectory);
    std::filesystem::create_directories(sharedDirectory);
    const auto module = sharedDirectory / "helper.lua";
    const auto randomModule = sharedDirectory / "random.lua";
    const auto oversizedModule = sharedDirectory / "oversized.lua";
    struct Cleanup
    {
        std::filesystem::path file, randomFile, oversizedFile, shared, scripts, directory;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(file, ignored);
            std::filesystem::remove(randomFile, ignored);
            std::filesystem::remove(oversizedFile, ignored);
            std::filesystem::remove(shared, ignored); std::filesystem::remove(scripts, ignored);
            std::filesystem::remove(directory, ignored); }
    } cleanup{module, randomModule, oversizedModule, sharedDirectory, scriptDirectory, directory};
    { std::ofstream output(module, std::ios::binary | std::ios::trunc);
        output << "assert(os == nil and coroutine == nil and math.random == nil and math.randomseed == nil); "
            "return { system_name='from-module' }\n";
        output.close(); test::require(output.good(), "local module fixture should be writable"); }
    const std::string source = R"lua(
        local helper = require('shared.helper')
        assert(engine.register_component('Health', {hp='number'}))
        return {systems={{name=helper.system_name, all={'Health'}, reads={'Health'},
            writes={}, update=function() end}}}
    )lua";
    const auto scriptName = "@" + (scriptDirectory / "main.lua").string();
    const auto localModule = LuaScriptSystem::validate_declarations(Sources{{source, scriptName}}, directory);
    test::require(localModule.has_value(), "preflight should allow a sibling module inside the project root");
    EngineScriptApi runtimeApi;
    runtimeApi.module_root = directory;
    runtimeApi.register_component = [](std::string_view, const std::vector<LuaComponentField>&) { return true; };
    LuaScriptSystem runtimeScripts(std::move(runtimeApi));
    test::require(runtimeScripts.load_string(source, scriptName).has_value(),
        "runtime and preflight should load the same project-local module source");
    { std::ofstream output(randomModule, std::ios::binary | std::ios::trunc);
        output << "math.random(); return {}\n";
        output.close(); test::require(output.good(), "random module fixture should be writable"); }
    const std::string randomSource = "require('shared.random'); return {}";
    test::require(!LuaScriptSystem::validate_declarations(Sources{{randomSource, scriptName}}, directory) &&
        !runtimeScripts.load_string(randomSource, scriptName),
        "modules must not call RNG functions in validation or runtime");
    { std::ofstream output(oversizedModule, std::ios::binary | std::ios::trunc);
        output.seekp(static_cast<std::streamoff>(scripting::max_lua_source_bytes));
        output.put('x'); output.close();
        test::require(output.good(), "oversized module fixture should be writable"); }
    const std::string oversizedSource = "require('shared.oversized'); return {}";
    const auto oversizedPreflight = LuaScriptSystem::validate_declarations(
        Sources{{oversizedSource, scriptName}}, directory);
    const auto oversizedRuntime = runtimeScripts.load_string(oversizedSource, scriptName);
    const auto oversizedDirect = runtimeScripts.load_file(oversizedModule.string());
    test::require(!oversizedPreflight && !oversizedRuntime && !oversizedDirect &&
        oversizedPreflight.error().find("1 MiB limit") != std::string::npos &&
        oversizedRuntime.error().find("1 MiB limit") != std::string::npos &&
        oversizedDirect.error().find("1 MiB limit") != std::string::npos,
        "oversized modules and direct scripts must be rejected before source allocation");
    const auto traversal = LuaScriptSystem::validate_declarations(Sources{{
        "require('..escape'); return {}", scriptName}}, directory);
    test::require(!traversal, "project module resolver must reject traversal-like module names");
}
