#include "lua_script_system.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <iterator>
#include <utility>

extern "C"
{
#include <lauxlib.h>
#include <lualib.h>
}

struct LuaScriptSystem::Impl
{
    struct Script { LuaScriptId id; int table_ref = LUA_NOREF; };

    lua_State* state = luaL_newstate();
    EngineScriptApi api;
    // Preserve load order for predictable update and teardown behavior.
    std::map<std::uint64_t, Script> scripts;
    std::uint64_t next_id = 1;
    unsigned lua_execution_depth = 0;
    bool closed = false;

    struct ExecutionScope
    {
        Impl& owner;
        explicit ExecutionScope(Impl& impl) : owner(impl) { ++owner.lua_execution_depth; }
        ~ExecutionScope() { --owner.lua_execution_depth; }
    };

    int protected_call(const int argument_count, const int result_count)
    {
        ExecutionScope scope(*this);
        return lua_pcall(state, argument_count, result_count, 0);
    }

    explicit Impl(EngineScriptApi services) : api(std::move(services))
    {
        if (state)
            luaL_openlibs(state);
    }

    ~Impl()
    {
        const auto result = close();
        (void)result;
    }

    static Impl* from_upvalue(lua_State* lua)
    {
        return static_cast<Impl*>(lua_touserdata(lua, lua_upvalueindex(1)));
    }

    template<class Fn>
    static int invoke(lua_State* lua, Fn&& fn)
    {
        int result = 0;
        bool failed = false;
        std::array<char, 512> error_message{};
        try { result = fn(); }
        catch (const std::exception& error)
        {
            failed = true;
            const char* message = error.what();
            const size_t length = std::min(std::strlen(message), error_message.size() - 1);
            std::memcpy(error_message.data(), message, length);
        }
        catch (...)
        {
            failed = true;
            constexpr char message[] = "native engine callback failed";
            std::memcpy(error_message.data(), message, sizeof(message));
        }
        // Lua may use longjmp for errors, so raise after the caught exception unwinds.
        // The remaining stack locals are trivially destructible.
        if (failed)
        {
            lua_pushstring(lua, error_message.data());
            return lua_error(lua);
        }
        return result;
    }

    static int log(lua_State* lua)
    {
        return invoke(lua, [&]
        {
            auto* self = from_upvalue(lua);
            size_t length = 0;
            const char* text = luaL_checklstring(lua, 1, &length);
            if (self->api.log) self->api.log(std::string_view(text, length));
            return 0;
        });
    }

    static int create_entity(lua_State* lua)
    {
        return invoke(lua, [&]
        {
            auto* self = from_upvalue(lua);
            if (!self->api.create_entity) return luaL_error(lua, "engine.create_entity is unavailable");
            lua_pushinteger(lua, static_cast<lua_Integer>(self->api.create_entity()));
            return 1;
        });
    }

    static std::int64_t entity_arg(lua_State* lua, int index)
    {
        return static_cast<std::int64_t>(luaL_checkinteger(lua, index));
    }

    static int is_alive(lua_State* lua)
    {
        return invoke(lua, [&]
        {
            auto* self = from_upvalue(lua);
            const bool alive = self->api.is_entity_alive && self->api.is_entity_alive(entity_arg(lua, 1));
            lua_pushboolean(lua, alive);
            return 1;
        });
    }

    static int destroy_entity(lua_State* lua)
    {
        return invoke(lua, [&]
        {
            auto* self = from_upvalue(lua);
            const bool destroyed = self->api.destroy_entity && self->api.destroy_entity(entity_arg(lua, 1));
            lua_pushboolean(lua, destroyed);
            return 1;
        });
    }

    static int set_position(lua_State* lua)
    {
        return invoke(lua, [&]
        {
            auto* self = from_upvalue(lua);
            if (!self->api.set_position) return luaL_error(lua, "engine.set_position is unavailable");
            const bool changed = self->api.set_position(entity_arg(lua, 1),
                static_cast<float>(luaL_checknumber(lua, 2)),
                static_cast<float>(luaL_checknumber(lua, 3)),
                static_cast<float>(luaL_checknumber(lua, 4)));
            lua_pushboolean(lua, changed);
            return 1;
        });
    }

    static int get_position(lua_State* lua)
    {
        return invoke(lua, [&]
        {
            auto* self = from_upvalue(lua);
            const auto position = self->api.get_position
                ? self->api.get_position(entity_arg(lua, 1)) : std::nullopt;
            if (!position) { lua_pushnil(lua); return 1; }
            lua_createtable(lua, 0, 3);
            lua_pushnumber(lua, (*position)[0]); lua_setfield(lua, -2, "x");
            lua_pushnumber(lua, (*position)[1]); lua_setfield(lua, -2, "y");
            lua_pushnumber(lua, (*position)[2]); lua_setfield(lua, -2, "z");
            return 1;
        });
    }

    void add_function(const char* name, lua_CFunction function)
    {
        lua_pushlightuserdata(state, this);
        lua_pushcclosure(state, function, 1);
        lua_setfield(state, -2, name);
    }

    void push_engine_api()
    {
        lua_createtable(state, 0, 6);
        add_function("log", log);
        add_function("create_entity", create_entity);
        add_function("is_alive", is_alive);
        add_function("destroy_entity", destroy_entity);
        add_function("set_position", set_position);
        add_function("get_position", get_position);
    }

    std::string pop_error()
    {
        const char* message = lua_tostring(state, -1);
        std::string result = message ? message : "unknown Lua error";
        lua_pop(state, 1);
        return result;
    }

    Result call(const Script& script, const char* method, const int arguments = 0, const float delta = 0.0f)
    {
        lua_rawgeti(state, LUA_REGISTRYINDEX, script.table_ref);
        lua_pushstring(state, method);
        lua_rawget(state, -2); // avoid executing a Lua __index metamethod outside lua_pcall
        lua_remove(state, -2);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); return {}; }
        if (!lua_isfunction(state, -1))
        {
            lua_pop(state, 1);
            return std::unexpected(std::string("script lifecycle member '") + method + "' must be a function");
        }
        if (arguments != 0) lua_pushnumber(state, delta);
        if (protected_call(arguments, 0) != LUA_OK)
            return std::unexpected(std::string(method) + ": " + pop_error());
        return {};
    }

    LoadResult load(std::string_view source, std::string_view name)
    {
        if (closed || !state) return std::unexpected("Lua runtime is shut down");
        const std::string chunk_name(name);
        if (luaL_loadbuffer(state, source.data(), source.size(), chunk_name.c_str()) != LUA_OK)
            return std::unexpected(pop_error());

        // Give each script a private global table while retaining standard Lua libraries.
        lua_newtable(state);
        push_engine_api();
        lua_setfield(state, -2, "engine");
        lua_newtable(state);
        lua_pushglobaltable(state);
        lua_setfield(state, -2, "__index");
        lua_setmetatable(state, -2);
        if (lua_setupvalue(state, -2, 1) == nullptr)
        {
            lua_pop(state, 1);
            return std::unexpected("Lua chunk has no environment upvalue");
        }

        if (protected_call(0, 1) != LUA_OK)
            return std::unexpected(pop_error());
        if (!lua_istable(state, -1))
        {
            lua_pop(state, 1);
            return std::unexpected("Lua script chunk must return a lifecycle table");
        }
        if (next_id == std::numeric_limits<std::uint64_t>::max())
        {
            lua_pop(state, 1);
            return std::unexpected("Lua script id space exhausted");
        }

        Script script{LuaScriptId{next_id++}, luaL_ref(state, LUA_REGISTRYINDEX)};
        auto [it, inserted] = scripts.emplace(script.id.value, script);
        (void)inserted;
        auto created = call(it->second, "on_create");
        if (!created)
        {
            // Give partially initialized scripts a chance to undo native side effects.
            (void)call(it->second, "on_destroy");
            luaL_unref(state, LUA_REGISTRYINDEX, script.table_ref);
            scripts.erase(it);
            return std::unexpected(created.error());
        }
        return script.id;
    }

    Result remove(const LuaScriptId id)
    {
        auto it = scripts.find(id.value);
        if (it == scripts.end()) return std::unexpected("unknown Lua script id");
        auto destroyed = call(it->second, "on_destroy");
        luaL_unref(state, LUA_REGISTRYINDEX, it->second.table_ref);
        scripts.erase(it);
        return destroyed;
    }

    Result close()
    {
        if (closed) return {};
        // Mark the runtime unavailable before lua_close: Lua runs table __gc
        // finalizers there, and native callbacks may reenter this object.
        closed = true;
        std::string first_error;
        if (state)
        {
            for (const auto& [id, script] : scripts)
            {
                (void)id;
                auto result = call(script, "on_destroy");
                if (!result && first_error.empty()) first_error = result.error();
            }
            scripts.clear();
            lua_close(state);
            state = nullptr;
        }
        if (!first_error.empty()) return std::unexpected(std::move(first_error));
        return {};
    }
};

LuaScriptSystem::LuaScriptSystem(EngineScriptApi api) : m_impl(std::make_shared<Impl>(std::move(api)))
{
    if (!m_impl->state) throw std::runtime_error("failed to create Lua state");
}

LuaScriptSystem::~LuaScriptSystem() = default;

LuaScriptSystem::LoadResult LuaScriptSystem::load_file(const std::string& path)
{
    const auto impl = m_impl;
    if (!impl || impl->closed) return std::unexpected("Lua runtime is shut down");
    if (impl->lua_execution_depth != 0) return std::unexpected("cannot load scripts during Lua execution");
    Impl::ExecutionScope execution(*impl);
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::unexpected("unable to open Lua script: " + path);
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (file.bad()) return std::unexpected("unable to read Lua script: " + path);
    return impl->load(source, "@" + path);
}

LuaScriptSystem::LoadResult LuaScriptSystem::load_string(const std::string_view source, const std::string_view chunk_name)
{
    const auto impl = m_impl;
    if (!impl) return std::unexpected("Lua runtime unavailable");
    if (impl->closed) return std::unexpected("Lua runtime is shut down");
    if (impl->lua_execution_depth != 0) return std::unexpected("cannot load scripts during Lua execution");
    Impl::ExecutionScope execution(*impl);
    return impl->load(source, chunk_name);
}

LuaScriptSystem::Result LuaScriptSystem::update(const float delta_seconds)
{
    const auto impl = m_impl;
    if (!impl || impl->closed) return std::unexpected("Lua runtime is shut down");
    if (impl->lua_execution_depth != 0) return std::unexpected("cannot update scripts during Lua execution");
    Impl::ExecutionScope execution(*impl);
    std::string first_error;
    for (const auto& [id, script] : impl->scripts)
    {
        (void)id;
        auto result = impl->call(script, "on_update", 1, delta_seconds);
        if (!result && first_error.empty()) first_error = result.error();
    }
    if (!first_error.empty()) return std::unexpected(std::move(first_error));
    return {};
}

LuaScriptSystem::Result LuaScriptSystem::unload(const LuaScriptId id)
{
    const auto impl = m_impl;
    if (!impl || impl->closed) return std::unexpected("Lua runtime is shut down");
    if (impl->lua_execution_depth != 0) return std::unexpected("cannot unload scripts during Lua execution");
    Impl::ExecutionScope execution(*impl);
    return impl->remove(id);
}

LuaScriptSystem::Result LuaScriptSystem::shutdown()
{
    const auto impl = m_impl;
    if (!impl) return {};
    if (impl->lua_execution_depth != 0) return std::unexpected("cannot shut down Lua during script execution");
    return impl->close();
}
