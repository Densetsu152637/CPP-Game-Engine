#include "lua_script_system.h"
#include "lua_source_file.h"
#include "../ecs/core/dynamic_component_storage.h"
#include "../../third_party/picojson/picojson.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <iterator>
#include <utility>

extern "C"
{
#include <lauxlib.h>
#include <lualib.h>
}

namespace
{
    bool valid_registration_name(const std::string_view name)
    {
        if (name.empty() || name.size() > ecs::max_dynamic_name_bytes) return false;
        const auto letter = [](const char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
        if (!letter(name.front()) && name.front() != '_') return false;
        return std::all_of(name.begin() + 1, name.end(), [&](const char c)
        { return letter(c) || (c >= '0' && c <= '9') || c == '_'; });
    }
}

struct LuaScriptSystem::Impl
{
    struct System
    {
        std::string name;
        std::vector<std::string> all, reads, writes;
        int phase = 0, order = 0;
        int update_ref = LUA_NOREF;
    };
    struct Script
    {
        LuaScriptId id;
        int table_ref = LUA_NOREF;
        LuaScriptContext context;
        std::vector<System> systems;
        std::vector<std::string> registrations;
        std::filesystem::path module_directory;
    };

    lua_State* state = nullptr;
    EngineScriptApi api;
    bool declaration_validation = false;
    size_t validation_bytes = 0;
    int validation_hook_ticks = 0;
    std::filesystem::path module_root, module_directory;
    std::map<std::string, int> project_modules;
    // Preserve load order for predictable update and teardown behavior.
    std::map<std::uint64_t, Script> scripts;
    std::uint64_t next_id = 1;
    unsigned lua_execution_depth = 0;
    bool closed = false;
    const LuaScriptContext* active_context = nullptr;
    const System* active_system = nullptr;
    bool allows_read(const std::string_view component) const
    {
        if (!active_system) return true;
        return std::find(active_system->reads.begin(), active_system->reads.end(), component) != active_system->reads.end() ||
            std::find(active_system->writes.begin(), active_system->writes.end(), component) != active_system->writes.end();
    }
    bool allows_write(const std::string_view component) const
    {
        return !active_system ||
            std::find(active_system->writes.begin(), active_system->writes.end(), component) != active_system->writes.end();
    }
    struct Registration { std::string name; bool created; };
    std::vector<Registration>* loading_registrations = nullptr;
    std::map<std::string, size_t> schema_owners;
    std::set<std::string> script_created_schemas;

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

    static void* limited_allocator(void* context, void* pointer, size_t oldSize, size_t newSize)
    {
        auto& owner = *static_cast<Impl*>(context);
        if (!pointer) oldSize = 0;
        if (newSize == 0)
        {
            std::free(pointer);
            owner.validation_bytes -= oldSize;
            return nullptr;
        }
        constexpr size_t limit = 16 * 1024 * 1024;
        if (newSize > oldSize && newSize - oldSize > limit - owner.validation_bytes) return nullptr;
        void* result = std::realloc(pointer, newSize);
        if (result) owner.validation_bytes = owner.validation_bytes - oldSize + newSize;
        return result;
    }

    static void validation_hook(lua_State* lua, lua_Debug*)
    {
        auto* owner = *static_cast<Impl**>(lua_getextraspace(lua));
        if (++owner->validation_hook_ticks > 200)
            luaL_error(lua, "declaration validation instruction limit exceeded");
    }

    explicit Impl(EngineScriptApi services, const bool validateDeclarations = false)
        : api(std::move(services)), declaration_validation(validateDeclarations)
    {
        if (!api.module_root.empty()) module_root = std::filesystem::weakly_canonical(api.module_root);
        state = declaration_validation ? lua_newstate(limited_allocator, this) : luaL_newstate();
        if (state)
        {
            luaL_openlibs(state);
            *static_cast<Impl**>(lua_getextraspace(state)) = this;
            if (declaration_validation)
            {
                for (const char* name : {"io", "os", "package", "debug", "require", "dofile",
                    "loadfile", "load", "collectgarbage", "print"})
                { lua_pushnil(state); lua_setglobal(state, name); }
                lua_sethook(state, validation_hook, LUA_MASKCOUNT, 1000);
            }
            if (declaration_validation || !module_root.empty())
            {
                lua_pushlightuserdata(state, this);
                lua_pushcclosure(state, validation_require, 1);
                lua_setglobal(state, "require");
            }
            if (api.redirect_standard_output)
            {
                lua_pushlightuserdata(state, this);
                lua_pushcclosure(state, print_to_log, 1);
                lua_setglobal(state, "print");
                lua_getglobal(state, "io");
                lua_getfield(state, -1, "stderr");
                lua_getfield(state, -2, "output");
                lua_pushvalue(state, -2);
                lua_call(state, 1, 0);
                lua_pop(state, 2);
            }
        }
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

    static int silent_validation_log(lua_State*) { return 0; }

    static int project_io_write(lua_State* lua)
    {
        return invoke(lua, [&]
        {
            auto* self = from_upvalue(lua);
            std::array<char, 4096> output{};
            size_t used = 0;
            const int first = lua_istable(lua, 1) ? 2 : 1;
            for (int index = first; index <= lua_gettop(lua); ++index)
            {
                size_t length = 0;
                const char* value = luaL_tolstring(lua, index, &length);
                while (length != 0)
                {
                    const size_t count = std::min(length, output.size() - used);
                    std::memcpy(output.data() + used, value, count);
                    used += count;
                    value += count;
                    length -= count;
                    if (used == output.size())
                    {
                        if (self->api.log) self->api.log(std::string_view(output.data(), used));
                        used = 0;
                    }
                }
                lua_pop(lua, 1);
            }
            if (used != 0 && self->api.log) self->api.log(std::string_view(output.data(), used));
            return 0;
        });
    }

    static void push_safe_math(lua_State* lua)
    {
        lua_getglobal(lua, "math");
        lua_newtable(lua);
        for (const char* name : {"abs", "acos", "asin", "atan", "ceil", "cos", "deg", "exp",
            "floor", "fmod", "huge", "log", "max", "maxinteger", "min", "mininteger",
            "modf", "pi", "rad", "sin", "sqrt", "tan", "tointeger", "type", "ult"})
        {
            lua_getfield(lua, -2, name);
            lua_setfield(lua, -2, name);
        }
        lua_remove(lua, -2);
    }

    void populate_host_globals(bool includeOwner)
    {
        push_engine_api();
        lua_setfield(state, -2, "engine");
        for (const char* space : {"ui", "audio", "save", "settings", "state"})
        {
            lua_newtable(state);
            const std::vector<const char*> names = std::string_view(space) == "ui" ?
                std::vector<const char*>{"open", "close", "set_text", "scroll", "event"} : std::string_view(space) == "audio" ?
                std::vector<const char*>{"play", "stop", "volume"} : std::string_view(space) == "save" ? std::vector<const char*>{"read", "write", "recover"} : std::vector<const char*>{"read", "write"};
            for (const auto* field : names) { const auto service = std::string(space) + "." + field; add_service(field, service.c_str()); }
            lua_setfield(state, -2, space);
        }
        if (includeOwner)
        {
            lua_newtable(state);
            add_function("set_position", self_set_position);
            add_function("get_position", self_get_position);
            add_function("set_component", self_set_component);
            add_function("get_component", self_get_component);
            add_function("remove_component", self_remove_component);
            add_service("id", "self.id"); add_service("move", "self.move"); add_service("overlaps", "self.overlaps");
            add_service("safe_position", "self.safe_position");
            lua_setfield(state, -2, "self");
            lua_newtable(state);
            for (const char* mode : {"pressed", "held", "released", "value", "pointer", "wheel"})
            {
                lua_pushlightuserdata(state, this);
                lua_pushstring(state, mode);
                lua_pushcclosure(state, input_action, 2);
                lua_setfield(state, -2, mode);
            }
            lua_setfield(state, -2, "input");
        }
    }

    void push_root_safe_globals()
    {
        lua_newtable(state);
        for (const char* name : {"assert", "error", "ipairs", "pairs", "next", "pcall", "xpcall",
            "select", "tonumber", "tostring", "type", "rawequal", "rawget", "rawset",
            "rawlen", "setmetatable", "getmetatable", "math", "string", "table", "utf8",
            "require", "_VERSION"})
        {
            if (std::strcmp(name, "math") == 0) push_safe_math(state);
            else lua_getglobal(state, name);
            lua_setfield(state, -2, name);
        }
        if (declaration_validation) lua_pushcfunction(state, silent_validation_log);
        else { lua_pushlightuserdata(state, this); lua_pushcclosure(state, print_to_log, 1); }
        lua_setfield(state, -2, "print");
        lua_newtable(state);
        if (declaration_validation) lua_pushcfunction(state, silent_validation_log);
        else { lua_pushlightuserdata(state, this); lua_pushcclosure(state, project_io_write, 1); }
        lua_setfield(state, -2, "write");
        for (const char* stream : {"stdout", "stderr"})
        {
            lua_newtable(state);
            if (declaration_validation) lua_pushcfunction(state, silent_validation_log);
            else { lua_pushlightuserdata(state, this); lua_pushcclosure(state, project_io_write, 1); }
            lua_setfield(state, -2, "write");
            lua_setfield(state, -2, stream);
        }
        lua_setfield(state, -2, "io");
        lua_pushvalue(state, -1);
        lua_setfield(state, -2, "_G");
    }

    static int validation_require(lua_State* lua)
    {
        return invoke(lua, [&]
        {
            auto* self = from_upvalue(lua);
            if (lua_type(lua, 1) != LUA_TSTRING)
                throw std::runtime_error("require expects a module name string");
            size_t length = 0;
            const char* rawName = lua_tolstring(lua, 1, &length);
            std::string module(rawName, length);
            if (module.empty() || module.find("..") != std::string::npos ||
                module.front() == '.' || module.back() == '.' ||
                module.find_first_of("/\\:") != std::string::npos)
                throw std::runtime_error("module path is invalid");
            std::replace(module.begin(), module.end(), '.', '/');
            auto withinRoot = [&](const std::filesystem::path& path)
            {
                const auto relative = path.lexically_relative(self->module_root);
                return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
            };
            std::filesystem::path resolved;
            for (const auto& candidate : {self->module_directory / (module + ".lua"),
                self->module_directory / module / "init.lua",
                self->module_root / (module + ".lua"), self->module_root / module / "init.lua"})
            {
                std::error_code error;
                const auto canonical = std::filesystem::canonical(candidate, error);
                if (!error && withinRoot(canonical) && std::filesystem::is_regular_file(canonical))
                { resolved = canonical; break; }
            }
            if (resolved.empty()) throw std::runtime_error("module is missing or outside the project root: " + module);
            const std::string key = resolved.generic_string();
            if (const auto found = self->project_modules.find(key); found != self->project_modules.end())
            {
                lua_rawgeti(lua, LUA_REGISTRYINDEX, found->second);
                return 1;
            }
            const auto source = scripting::read_lua_source_file(resolved);
            if (!source) throw std::runtime_error(std::string(scripting::lua_source_error_message(source.error())) +
                ": " + key);
            if (luaL_loadbuffer(lua, source->data(), source->size(), ("@" + key).c_str()) != LUA_OK)
                throw std::runtime_error(self->pop_error());
            // Give modules the same restricted globals in preflight and at
            // runtime, so validation cannot resolve a different module path
            // or take an OS-dependent branch before gameplay begins.
            self->push_root_safe_globals();
            self->populate_host_globals(true);
            lua_pushvalue(lua, -1);
            lua_setfield(lua, -2, "_G");
            lua_setupvalue(lua, -2, 1);
            const auto previousDirectory = self->module_directory;
            self->module_directory = resolved.parent_path();
            const auto previousHook = lua_gethook(lua);
            const int previousMask = lua_gethookmask(lua);
            const int previousCount = lua_gethookcount(lua);
            if (!self->declaration_validation && !previousHook)
            {
                self->validation_hook_ticks = 0;
                lua_sethook(lua, validation_hook, LUA_MASKCOUNT, 1000);
            }
            const int status = self->protected_call(0, 1);
            if (!self->declaration_validation && !previousHook)
                lua_sethook(lua, previousHook, previousMask, previousCount);
            self->module_directory = previousDirectory;
            if (status != LUA_OK) throw std::runtime_error(self->pop_error());
            if (lua_isnil(lua, -1)) { lua_pop(lua, 1); lua_pushboolean(lua, true); }
            lua_pushvalue(lua, -1);
            self->project_modules.emplace(key, luaL_ref(lua, LUA_REGISTRYINDEX));
            return 1;
        });
    }

    static int print_to_log(lua_State* lua)
    {
        return invoke(lua, [&]
        {
            auto* self = from_upvalue(lua);
            std::array<char, 4096> output{};
            size_t used = 0;
            const int count = lua_gettop(lua);
            for (int index = 1; index <= count; ++index)
            {
                if (index > 1 && used < output.size()) output[used++] = '\t';
                size_t length = 0;
                const char* value = luaL_tolstring(lua, index, &length);
                const size_t copyLength = std::min(length, output.size() - used);
                std::memcpy(output.data() + used, value, copyLength);
                used += copyLength;
                lua_pop(lua, 1);
            }
            if (self->api.log) self->api.log(std::string_view(output.data(), used));
            return 0;
        });
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
            if (!self->allows_write("Position3D"))
                return luaL_error(lua, "system wrote an undeclared Position3D component");
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
            if (!self->allows_read("Position3D"))
                return luaL_error(lua, "system read an undeclared Position3D component");
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

    static bool read_values(lua_State* lua, int index, LuaComponentValues& values)
    {
        if (!lua_istable(lua, index)) return false;
        index = lua_absindex(lua, index);
        lua_pushnil(lua);
        while (lua_next(lua, index) != 0)
        {
            if (lua_type(lua, -2) != LUA_TSTRING) { lua_pop(lua, 2); return false; }
            size_t length = 0;
            const char* key = lua_tolstring(lua, -2, &length);
            LuaComponentValue value;
            switch (lua_type(lua, -1))
            {
            case LUA_TNUMBER: value = static_cast<double>(lua_tonumber(lua, -1)); break;
            case LUA_TBOOLEAN: value = static_cast<bool>(lua_toboolean(lua, -1)); break;
            case LUA_TSTRING:
            {
                size_t valueLength = 0; const char* text = lua_tolstring(lua, -1, &valueLength);
                value = std::string(text, valueLength); break;
            }
            default: lua_pop(lua, 2); return false;
            }
            values.emplace_back(std::string(key, length), std::move(value));
            lua_pop(lua, 1);
        }
        return true;
    }

    static int register_component(lua_State* lua)
    {
        return invoke(lua, [&]
        {
            auto* self = from_upvalue(lua);
            size_t nameLength = 0; const char* name = luaL_checklstring(lua, 1, &nameLength);
            if (!lua_istable(lua, 2)) return luaL_error(lua, "component schema must be a table");
            std::uint32_t schemaVersion = 1;
            if (!lua_isnoneornil(lua, 3))
            {
                if (!lua_isinteger(lua, 3) || lua_tointeger(lua, 3) <= 0 ||
                    static_cast<std::uint64_t>(lua_tointeger(lua, 3)) > std::numeric_limits<std::uint32_t>::max())
                    throw std::runtime_error("component version must be a positive 32-bit integer");
                schemaVersion = static_cast<std::uint32_t>(lua_tointeger(lua, 3));
            }
            std::vector<LuaComponentField> fields;
            lua_pushnil(lua);
            while (lua_next(lua, 2) != 0)
            {
                if (lua_type(lua, -2) != LUA_TSTRING)
                    throw std::runtime_error("component field names must be strings");
                size_t fieldLength = 0; const char* fieldName = lua_tolstring(lua, -2, &fieldLength);
                if (!valid_registration_name(std::string_view(fieldName, fieldLength)))
                    throw std::runtime_error("component field name must be a bounded identifier");
                LuaComponentField field{std::string(fieldName, fieldLength), LuaComponentFieldType::Number};
                const int spec = lua_absindex(lua, -1);
                if (lua_istable(lua, spec))
                {
                    lua_pushnil(lua);
                    while (lua_next(lua, spec) != 0)
                    {
                        if (lua_type(lua, -2) != LUA_TSTRING)
                            throw std::runtime_error("component field options must have string keys");
                        const std::string_view key(lua_tostring(lua, -2));
                        if (key != "type" && key != "version" && key != "default")
                            throw std::runtime_error("unsupported component field option");
                        lua_pop(lua, 1);
                    }
                    lua_pushliteral(lua, "version"); lua_rawget(lua, spec);
                    if (!lua_isnil(lua, -1))
                    {
                        if (!lua_isinteger(lua, -1) || lua_tointeger(lua, -1) <= 0 ||
                            static_cast<std::uint64_t>(lua_tointeger(lua, -1)) > schemaVersion)
                            throw std::runtime_error("component field version must be positive and at most the component version");
                        field.version = static_cast<std::uint32_t>(lua_tointeger(lua, -1));
                    }
                    lua_pop(lua, 1);
                    lua_pushliteral(lua, "type"); lua_rawget(lua, spec);
                }
                else lua_pushvalue(lua, spec);
                if (lua_type(lua, -1) != LUA_TSTRING)
                    throw std::runtime_error("component field type must be number, boolean, or string");
                const char* typeName = lua_tostring(lua, -1);
                LuaComponentFieldType type;
                if (std::strcmp(typeName, "number") == 0) type = LuaComponentFieldType::Number;
                else if (std::strcmp(typeName, "boolean") == 0) type = LuaComponentFieldType::Boolean;
                else if (std::strcmp(typeName, "string") == 0) type = LuaComponentFieldType::String;
                else throw std::runtime_error("unsupported component field type");
                lua_pop(lua, 1);
                field.type = type;
                if (lua_istable(lua, spec))
                {
                    lua_pushliteral(lua, "default"); lua_rawget(lua, spec);
                    if (!lua_isnil(lua, -1))
                    {
                        if (type == LuaComponentFieldType::Number && lua_type(lua, -1) == LUA_TNUMBER &&
                            std::isfinite(lua_tonumber(lua, -1)))
                            field.default_value = static_cast<double>(lua_tonumber(lua, -1));
                        else if (type == LuaComponentFieldType::Boolean && lua_type(lua, -1) == LUA_TBOOLEAN)
                            field.default_value = lua_toboolean(lua, -1) != 0;
                        else if (type == LuaComponentFieldType::String && lua_type(lua, -1) == LUA_TSTRING)
                        {
                            size_t length = 0; const char* text = lua_tolstring(lua, -1, &length);
                            if (length > ecs::max_dynamic_string_bytes)
                                throw std::runtime_error("component string default is too long");
                            field.default_value = std::string(text, length);
                        }
                        else throw std::runtime_error("component field default has the wrong type or is not finite");
                    }
                    lua_pop(lua, 1);
                }
                fields.push_back(std::move(field));
                if (fields.size() > ecs::max_dynamic_fields_per_component)
                    throw std::runtime_error("component field limit exceeded");
                lua_pop(lua, 1);
            }
            if (fields.empty()) return luaL_error(lua, "component schema must define at least one field");
            if (!self->loading_registrations)
                return luaL_error(lua, "component schemas can only be registered while loading a script");
            const std::string componentName(name, nameLength);
            if (!valid_registration_name(componentName))
                throw std::runtime_error("component name must be a bounded identifier");
            if (componentName == "Position3D")
            {
                lua_pushboolean(lua, false);
                return 1;
            }
            const bool existed = self->api.has_component_schema && self->api.has_component_schema(componentName);
            const bool registered = self->api.register_component &&
                self->api.register_component(componentName, fields, schemaVersion);
            if (registered && std::none_of(self->loading_registrations->begin(), self->loading_registrations->end(),
                [&](const Registration& value) { return value.name == componentName; }))
                self->loading_registrations->push_back({componentName, !existed});
            lua_pushboolean(lua, registered); return 1;
        });
    }

    static int set_component(lua_State* lua)
    {
        return invoke(lua, [&]
        {
            auto* self = from_upvalue(lua); size_t length = 0;
            const char* name = luaL_checklstring(lua, 2, &length);
            LuaComponentValues values;
            if (!read_values(lua, 3, values)) return luaL_error(lua, "component values must be a table of primitive values");
            if (self->active_system && std::find(self->active_system->writes.begin(),
                self->active_system->writes.end(), std::string_view(name, length)) == self->active_system->writes.end())
                return luaL_error(lua, "system wrote an undeclared component");
            const bool result = self->api.set_component && self->api.set_component(entity_arg(lua, 1),
                std::string_view(name, length), values);
            lua_pushboolean(lua, result); return 1;
        });
    }

    static int get_component(lua_State* lua)
    {
        return invoke(lua, [&]
        {
            auto* self = from_upvalue(lua); size_t length = 0;
            const char* name = luaL_checklstring(lua, 2, &length);
            if (self->active_system &&
                std::find(self->active_system->reads.begin(), self->active_system->reads.end(),
                    std::string_view(name, length)) == self->active_system->reads.end() &&
                std::find(self->active_system->writes.begin(), self->active_system->writes.end(),
                    std::string_view(name, length)) == self->active_system->writes.end())
                return luaL_error(lua, "system read an undeclared component");
            const auto values = self->api.get_component ? self->api.get_component(entity_arg(lua, 1),
                std::string_view(name, length)) : std::nullopt;
            if (!values) { lua_pushnil(lua); return 1; }
            lua_createtable(lua, 0, static_cast<int>(values->size()));
            for (const auto& [key, value] : *values)
            {
                std::visit([&](const auto& v) { using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, std::string>) lua_pushlstring(lua, v.data(), v.size());
                    else if constexpr (std::is_same_v<T, bool>) lua_pushboolean(lua, v);
                    else lua_pushnumber(lua, v); }, value);
                lua_setfield(lua, -2, key.c_str());
            }
            return 1;
        });
    }

    static int remove_component(lua_State* lua)
    {
        return invoke(lua, [&]
        {
            auto* self = from_upvalue(lua); size_t length = 0;
            const char* name = luaL_checklstring(lua, 2, &length);
            if (self->active_system && std::find(self->active_system->writes.begin(),
                self->active_system->writes.end(), std::string_view(name, length)) == self->active_system->writes.end())
                return luaL_error(lua, "system removed an undeclared component");
            lua_pushboolean(lua, self->api.remove_component && self->api.remove_component(
                entity_arg(lua, 1), std::string_view(name, length))); return 1;
        });
    }

    static int self_set_position(lua_State* lua)
    {
        return invoke(lua, [&]
        {
            auto* self = from_upvalue(lua);
            if (!self->allows_write("Position3D"))
                return luaL_error(lua, "system wrote an undeclared Position3D component");
            if (!self->active_context || !self->active_context->owner || !self->api.set_position)
                return luaL_error(lua, "self position is unavailable");
            const bool changed = self->api.set_position(*self->active_context->owner,
                static_cast<float>(luaL_checknumber(lua, 1)), static_cast<float>(luaL_checknumber(lua, 2)),
                static_cast<float>(luaL_checknumber(lua, 3)));
            lua_pushboolean(lua, changed);
            return 1;
        });
    }

    static int self_get_position(lua_State* lua)
    {
        return invoke(lua, [&]
        {
            auto* self = from_upvalue(lua);
            if (!self->allows_read("Position3D"))
                return luaL_error(lua, "system read an undeclared Position3D component");
            if (!self->active_context || !self->active_context->owner || !self->api.get_position)
            { lua_pushnil(lua); return 1; }
            const auto position = self->api.get_position(*self->active_context->owner);
            if (!position) { lua_pushnil(lua); return 1; }
            lua_createtable(lua, 0, 3);
            lua_pushnumber(lua, (*position)[0]); lua_setfield(lua, -2, "x");
            lua_pushnumber(lua, (*position)[1]); lua_setfield(lua, -2, "y");
            lua_pushnumber(lua, (*position)[2]); lua_setfield(lua, -2, "z");
            return 1;
        });
    }

    static int self_set_component(lua_State* lua)
    {
        auto* self = from_upvalue(lua);
        if (!self->active_context || !self->active_context->owner) { lua_pushboolean(lua, false); return 1; }
        lua_pushinteger(lua, static_cast<lua_Integer>(*self->active_context->owner));
        lua_insert(lua, 1);
        return set_component(lua);
    }

    static int self_get_component(lua_State* lua)
    {
        auto* self = from_upvalue(lua);
        if (!self->active_context || !self->active_context->owner) { lua_pushnil(lua); return 1; }
        lua_pushinteger(lua, static_cast<lua_Integer>(*self->active_context->owner));
        lua_insert(lua, 1);
        return get_component(lua);
    }

    static int self_remove_component(lua_State* lua)
    {
        auto* self = from_upvalue(lua);
        if (!self->active_context || !self->active_context->owner) { lua_pushboolean(lua, false); return 1; }
        lua_pushinteger(lua, static_cast<lua_Integer>(*self->active_context->owner));
        lua_insert(lua, 1);
        return remove_component(lua);
    }

    static int input_action(lua_State* lua)
    {
        return invoke(lua, [&]
        {
            auto* self = from_upvalue(lua);
            const char* mode = lua_tostring(lua, lua_upvalueindex(2));
            if (std::string_view(mode) == "pointer" || std::string_view(mode) == "wheel")
            {
                const auto query = self->active_context ? (std::string_view(mode) == "pointer" ? self->active_context->pointer : self->active_context->wheel) : std::function<std::array<float, 2>()>{};
                const auto point = query ? query() : std::array<float, 2>{};
                lua_newtable(lua); lua_pushnumber(lua, point[0]); lua_setfield(lua, -2, "x"); lua_pushnumber(lua, point[1]); lua_setfield(lua, -2, "y"); return 1;
            }
            const char* action = luaL_checkstring(lua, 1);
            if (std::string_view(mode) == "value")
            { lua_pushnumber(lua, self->active_context && self->active_context->value ? self->active_context->value(action) : 0); return 1; }
            if (!self->active_context) { lua_pushboolean(lua, false); return 1; }
            const auto& query = std::string_view(mode) == "pressed" ? self->active_context->pressed
                : std::string_view(mode) == "held" ? self->active_context->held : self->active_context->released;
            lua_pushboolean(lua, query && query(action));
            return 1;
        });
    }

    void add_function(const char* name, lua_CFunction function)
    {
        lua_pushlightuserdata(state, this);
        lua_pushcclosure(state, function, 1);
        lua_setfield(state, -2, name);
    }

    static std::string string_arg(lua_State* lua, int index)
    {
        size_t size = 0;
        const char* value = luaL_checklstring(lua, index, &size);
        if (size > 65536) throw std::runtime_error("string exceeds service limit");
        return std::string(value, size);
    }

    static picojson::value json_value(lua_State* lua, int index, unsigned depth,
        size_t& nodes, std::set<const void*>& ancestors)
    {
        if (!lua_checkstack(lua, 4)) throw std::runtime_error("save conversion exceeds Lua stack capacity");
        if (depth > 24 || ++nodes > 65536) throw std::runtime_error("save object exceeds depth or node limit");
        switch (lua_type(lua, index))
        {
        case LUA_TBOOLEAN: return picojson::value(bool(lua_toboolean(lua, index)));
        case LUA_TNUMBER:
        {
            const double value = lua_tonumber(lua, index);
            if (!std::isfinite(value)) throw std::runtime_error("save numbers must be finite");
            return picojson::value(value);
        }
        case LUA_TSTRING: return picojson::value(string_arg(lua, index));
        case LUA_TTABLE: break;
        default: throw std::runtime_error("save values must be JSON-compatible objects, arrays, strings, booleans or numbers");
        }
        index = lua_absindex(lua, index);
        const void* identity = lua_topointer(lua, index);
        if (!ancestors.insert(identity).second) throw std::runtime_error("save objects cannot contain cycles");
        struct Erase { std::set<const void*>& set; const void* key; ~Erase() { set.erase(key); } } erase{ancestors, identity};
        picojson::object object;
        std::map<size_t, picojson::value> array;
        lua_pushnil(lua);
        while (lua_next(lua, index))
        {
            auto value = json_value(lua, -1, depth + 1, nodes, ancestors);
            if (lua_type(lua, -2) == LUA_TSTRING) object.emplace(string_arg(lua, -2), std::move(value));
            else if (lua_isinteger(lua, -2) && lua_tointeger(lua, -2) > 0 && lua_tointeger(lua, -2) <= 65536)
                array.emplace(static_cast<size_t>(lua_tointeger(lua, -2)), std::move(value));
            else throw std::runtime_error("save object keys must be strings or positive array indices");
            lua_pop(lua, 1);
        }
        if (!array.empty())
        {
            if (!object.empty() || array.rbegin()->first != array.size()) throw std::runtime_error("save arrays must have contiguous indices");
            picojson::array result;
            for (auto& [key, value] : array) { (void)key; result.push_back(std::move(value)); }
            return picojson::value(result);
        }
        return picojson::value(object);
    }

    static void push_json(lua_State* lua, const picojson::value& value)
    {
        if (!lua_checkstack(lua, 4)) throw std::runtime_error("save conversion exceeds Lua stack capacity");
        if (value.is<bool>()) lua_pushboolean(lua, value.get<bool>());
        else if (value.is<double>()) lua_pushnumber(lua, value.get<double>());
        else if (value.is<std::string>()) { const auto& text = value.get<std::string>(); lua_pushlstring(lua, text.data(), text.size()); }
        else if (value.is<picojson::object>())
        {
            lua_newtable(lua);
            for (const auto& [key, item] : value.get<picojson::object>()) { lua_pushlstring(lua, key.data(), key.size()); push_json(lua, item); lua_settable(lua, -3); }
        }
        else if (value.is<picojson::array>())
        {
            lua_newtable(lua); size_t index = 1;
            for (const auto& item : value.get<picojson::array>()) { push_json(lua, item); lua_rawseti(lua, -2, index++); }
        }
        else lua_pushnil(lua);
    }

    static void validate_json(const picojson::value& value, unsigned depth, size_t& nodes)
    {
        if (depth > 24 || ++nodes > 65536) throw std::runtime_error("save object exceeds depth or node limit");
        if (value.is<picojson::null>()) throw std::runtime_error("JSON null is unsupported by the Lua save bridge");
        if (value.is<double>() && !std::isfinite(value.get<double>())) throw std::runtime_error("save numbers must be finite");
        if (value.is<std::string>() && value.get<std::string>().size()>65536) throw std::runtime_error("string exceeds service limit");
        if (value.is<picojson::object>()) for(const auto& [key,item]:value.get<picojson::object>())
        { if(key.size()>65536) throw std::runtime_error("string exceeds service limit");validate_json(item,depth+1,nodes); }
        if (value.is<picojson::array>()) for(const auto& item:value.get<picojson::array>()) validate_json(item,depth+1,nodes);
    }

    static void preflight_json(std::string_view json)
    {
        if(json.size()>1024*1024) throw std::runtime_error("save exceeds byte limit");
        unsigned depth=0;bool quoted=false,escape=false;
        for(char c:json)
        {
            if(quoted) { if(escape) escape=false;else if(c=='\\') escape=true;else if(c=='"') quoted=false; }
            else if(c=='"') quoted=true;
            else if(c=='{'||c=='[') { if(++depth>25) throw std::runtime_error("save object exceeds depth limit"); }
            else if(c=='}'||c==']') { if(!depth) throw std::runtime_error("host returned invalid save object");--depth; }
        }
    }

    static int desktop_service(lua_State* lua)
    {
        return invoke(lua, [&]() -> int
        {
            auto* self = from_upvalue(lua);
            const std::string_view service = lua_tostring(lua, lua_upvalueindex(2));
            if (self->declaration_validation) throw std::runtime_error("runtime services cannot be called during declaration validation");
            auto& api = self->api;
            const auto unavailable = [&] { throw std::runtime_error(std::string(service) + " is unavailable"); };
            const auto result_error = [&](const std::string& error) { lua_pushnil(lua); lua_pushlstring(lua, error.data(), error.size()); return 2; };
            const auto result_void = [&](const auto& result) { if (!result) return result_error(result.error()); lua_pushboolean(lua, true); return 1; };
            const auto owner = [&]() -> std::int64_t
            { if (!self->active_context || !self->active_context->owner) throw std::runtime_error("self has no owner"); return *self->active_context->owner; };
            if (service == "find_entity")
            {
                if (!api.find_entity) unavailable();
                const auto result = api.find_entity(string_arg(lua, 1));
                if (result) lua_pushinteger(lua, *result); else lua_pushnil(lua); return 1;
            }
            if (service == "self.id") { lua_pushinteger(lua, owner()); return 1; }
            if (service == "lock_controls") { if (!api.lock_controls) unavailable(); lua_pushboolean(lua, api.lock_controls(string_arg(lua, 1), bool(lua_toboolean(lua, 2)))); return 1; }
            if (service == "set_camera") { if (!api.set_camera) unavailable(); lua_pushboolean(lua, api.set_camera(static_cast<float>(luaL_checknumber(lua, 1)), static_cast<float>(luaL_checknumber(lua, 2)))); return 1; }
            if (service == "reset_camera") { if (!api.reset_camera) unavailable(); api.reset_camera(); lua_pushboolean(lua, true); return 1; }
            if (service == "safe_position" || service == "self.safe_position")
            {
                if (!api.safe_position) unavailable(); const bool own = service == "self.safe_position";
                const auto result = api.safe_position(own ? owner() : entity_arg(lua, 1), static_cast<float>(luaL_checknumber(lua, own ? 1 : 2)), static_cast<float>(luaL_checknumber(lua, own ? 2 : 3)));
                if (!result) return result_error(result.error()); if (!*result) { lua_pushnil(lua); return 1; }
                lua_newtable(lua); lua_pushnumber(lua, (**result)[0]); lua_setfield(lua, -2, "x"); lua_pushnumber(lua, (**result)[1]); lua_setfield(lua, -2, "y"); return 1;
            }
            if (service == "triggers")
            {
                if (!api.triggers) unavailable(); lua_newtable(lua); size_t index = 1;
                for (const auto& event : api.triggers())
                {
                    lua_newtable(lua); lua_pushlstring(lua, event.first.data(), event.first.size()); lua_setfield(lua, -2, "first");
                    lua_pushlstring(lua, event.second.data(), event.second.size()); lua_setfield(lua, -2, "second");
                    lua_pushlstring(lua, event.phase.data(), event.phase.size()); lua_setfield(lua, -2, "phase"); lua_rawseti(lua, -2, index++);
                }
                return 1;
            }
            if (service == "move" || service == "self.move")
            {
                if (!api.move) unavailable();
                if (!self->allows_write("Position3D")) throw std::runtime_error("system wrote an undeclared Position3D component");
                const bool own = service == "self.move";
                const auto result = api.move(own ? owner() : entity_arg(lua, 1),
                    static_cast<float>(luaL_checknumber(lua, own ? 1 : 2)), static_cast<float>(luaL_checknumber(lua, own ? 2 : 3)));
                if (!result) return result_error(result.error());
                lua_createtable(lua, 0, 3);
                lua_pushnumber(lua, result->position[0]); lua_setfield(lua, -2, "x");
                lua_pushnumber(lua, result->position[1]); lua_setfield(lua, -2, "y");
                lua_newtable(lua); size_t index = 1;
                for (const auto& contact : result->contacts) { lua_pushlstring(lua, contact.data(), contact.size()); lua_rawseti(lua, -2, index++); }
                lua_setfield(lua, -2, "contacts"); return 1;
            }
            if (service == "overlaps" || service == "self.overlaps")
            {
                if (!api.overlaps) unavailable();
                const auto result = api.overlaps(service == "self.overlaps" ? owner() : entity_arg(lua, 1));
                if (!result) return result_error(result.error());
                lua_newtable(lua); size_t index = 1;
                for (const auto& id : *result) { lua_pushlstring(lua, id.data(), id.size()); lua_rawseti(lua, -2, index++); } return 1;
            }
            if (service == "change_scene")
            { if (!api.change_scene) unavailable(); return result_void(api.change_scene(string_arg(lua, 1), lua_gettop(lua) >= 2 ? string_arg(lua, 2) : "", lua_gettop(lua) >= 3 ? string_arg(lua, 3) : "")); }
            if (service == "sprite_frame" || service == "sprite_visible")
            {
                const auto entity = entity_arg(lua, 1);
                bool result = false;
                if (service == "sprite_frame")
                { if (!api.set_sprite_frame) unavailable(); const auto frame = luaL_checkinteger(lua, 2); if (frame < 0 || frame > UINT32_MAX) throw std::runtime_error("invalid sprite frame"); result = api.set_sprite_frame(entity, static_cast<uint32_t>(frame)); }
                else { if (!api.set_sprite_visible) unavailable(); result = api.set_sprite_visible(entity, bool(lua_toboolean(lua, 2))); }
                lua_pushboolean(lua, result); return 1;
            }
            if (service == "ui.open")
            {
                if (!api.ui_open) unavailable(); luaL_checktype(lua, 1, LUA_TTABLE);
                LuaUiPanel panel;
                const auto field = [&](const char* name) { lua_getfield(lua, 1, name); auto value = string_arg(lua, -1); lua_pop(lua, 1); return value; };
                panel.id = field("id"); panel.font = field("font"); panel.text = field("text");
                const auto optional_number = [&](const char* name, float& value)
                { lua_getfield(lua, 1, name); if (!lua_isnil(lua, -1)) value = static_cast<float>(luaL_checknumber(lua, -1)); lua_pop(lua, 1); };
                optional_number("x", panel.rect[0]); optional_number("y", panel.rect[1]); optional_number("width", panel.rect[2]); optional_number("height", panel.rect[3]); optional_number("scale", panel.scale);
                lua_getfield(lua, 1, "modal"); if (!lua_isnil(lua, -1)) { luaL_checktype(lua, -1, LUA_TBOOLEAN); panel.modal = bool(lua_toboolean(lua, -1)); } lua_pop(lua, 1);
                lua_getfield(lua, 1, "choices");
                if (!lua_isnil(lua, -1)) { luaL_checktype(lua, -1, LUA_TTABLE); if (lua_rawlen(lua, -1) > 64) throw std::runtime_error("too many UI choices"); for (size_t index = 1; index <= lua_rawlen(lua, -1); ++index) { lua_rawgeti(lua, -1, index); panel.choices.push_back(string_arg(lua, -1)); lua_pop(lua, 1); } } lua_pop(lua, 1);
                return result_void(api.ui_open(panel));
            }
            if (service == "ui.close" || service == "ui.set_text" || service == "ui.scroll")
            {
                const auto id = string_arg(lua, 1); bool result = false;
                if (service == "ui.close") { if (!api.ui_close) unavailable(); result = api.ui_close(id); }
                else if (service == "ui.set_text") { if (!api.ui_set_text) unavailable(); result = api.ui_set_text(id, string_arg(lua, 2)); }
                else { if (!api.ui_scroll) unavailable(); result = api.ui_scroll(id, static_cast<float>(luaL_checknumber(lua, 2))); }
                lua_pushboolean(lua, result); return 1;
            }
            if (service == "ui.event")
            {
                if (!api.ui_event) unavailable(); const auto event = api.ui_event();
                if (!event) { lua_pushnil(lua); return 1; }
                lua_newtable(lua); lua_pushlstring(lua, event->panel.data(), event->panel.size()); lua_setfield(lua, -2, "panel");
                lua_pushlstring(lua, event->type.data(), event->type.size()); lua_setfield(lua, -2, "type"); lua_pushinteger(lua, event->selection + 1); lua_setfield(lua, -2, "selection"); return 1;
            }
            if (service == "audio.play")
            {
                if (!api.audio_play) unavailable(); const auto asset = string_arg(lua, 1);
                const auto result = api.audio_play(asset, lua_gettop(lua) >= 2 && lua_toboolean(lua, 2), lua_gettop(lua) >= 3 ? string_arg(lua, 3) : "effects", lua_gettop(lua) >= 4 ? static_cast<float>(luaL_checknumber(lua, 4)) : 1);
                if (!result) return result_error(result.error()); lua_pushinteger(lua, *result); return 1;
            }
            if (service == "audio.stop") { if (!api.audio_stop) unavailable(); lua_pushboolean(lua, api.audio_stop(static_cast<uint64_t>(entity_arg(lua, 1)))); return 1; }
            if (service == "audio.volume") { if (!api.audio_volume) unavailable(); return result_void(api.audio_volume(string_arg(lua, 1), static_cast<float>(luaL_checknumber(lua, 2)))); }
            if (service == "save.recover") { if (!api.save_recover) unavailable(); return result_void(api.save_recover(string_arg(lua, 1))); }
            if (service == "save.write" || service == "settings.write" || service == "state.write")
            {
                const int originalTop=lua_gettop(lua);
                try {
                size_t nodes = 0; std::set<const void*> ancestors;
                const auto value = json_value(lua, service == "save.write" ? 2 : 1, 0, nodes, ancestors);
                if (!value.is<picojson::object>()) throw std::runtime_error("save root must be an object");
                const auto json = value.serialize(); if (json.size() > 1024 * 1024) throw std::runtime_error("save exceeds byte limit");
                if (service == "save.write") { if (!api.save_write) unavailable(); return result_void(api.save_write(string_arg(lua, 1), json)); }
                if (service == "state.write") { if (!api.state_write) unavailable(); return result_void(api.state_write(json)); }
                if (!api.settings_write) unavailable(); return result_void(api.settings_write(json));
                } catch(const std::exception& error) { lua_settop(lua,originalTop);return result_error(error.what()); }
            }
            if (service == "save.read" || service == "settings.read" || service == "state.read")
            {
                std::expected<std::string, std::string> result;
                if (service == "save.read") { if (!api.save_read) unavailable(); result = api.save_read(string_arg(lua, 1), lua_gettop(lua) >= 2 && lua_toboolean(lua, 2)); }
                else if (service == "state.read") { if (!api.state_read) unavailable(); result = api.state_read(); }
                else { if (!api.settings_read) unavailable(); result = api.settings_read(); }
                if (!result) { lua_pushnil(lua); lua_pushlstring(lua, result.error().data(), result.error().size()); return 2; }
                const int originalTop=lua_gettop(lua);
                try {
                preflight_json(*result);
                picojson::value value; const auto error = picojson::parse(value, *result);
                if (!error.empty() || !value.is<picojson::object>()) throw std::runtime_error("host returned invalid save object");
                size_t nodes=0;validate_json(value,0,nodes);
                push_json(lua, value); return 1;
                } catch(const std::exception& error) { lua_settop(lua,originalTop);return result_error(error.what()); }
            }
            throw std::runtime_error("unknown runtime service");
        });
    }

    void add_service(const char* field, const char* service)
    {
        lua_pushlightuserdata(state, this); lua_pushstring(state, service);
        lua_pushcclosure(state, desktop_service, 2); lua_setfield(state, -2, field);
    }

    void push_engine_api()
    {
        lua_createtable(state, 0, 12);
        add_function("log", log);
        add_function("create_entity", create_entity);
        add_function("is_alive", is_alive);
        add_function("destroy_entity", destroy_entity);
        add_function("set_position", set_position);
        add_function("get_position", get_position);
        add_function("register_component", register_component);
        add_function("set_component", set_component);
        add_function("get_component", get_component);
        add_function("remove_component", remove_component);
        for (const char* name : {"find_entity", "move", "overlaps", "change_scene", "sprite_frame", "sprite_visible", "lock_controls", "set_camera", "reset_camera", "safe_position", "triggers"}) add_service(name, name);
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
        struct ContextScope
        {
            Impl& impl; const LuaScriptContext* previous;
            std::filesystem::path previousDirectory;
            ContextScope(Impl& owner, const Script& script) : impl(owner),
                previous(owner.active_context), previousDirectory(owner.module_directory)
            { impl.active_context = &script.context; impl.module_directory = script.module_directory; }
            ~ContextScope() { impl.active_context = previous; impl.module_directory = previousDirectory; }
        } contextScope(*this, script);
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

    Result read_system_names(const int item, const char* key, std::vector<std::string>& names)
    {
        lua_pushstring(state, key); lua_rawget(state, item);
        if (!lua_istable(state, -1))
        {
            lua_pop(state, 1);
            return std::unexpected(std::string("system ") + key + " member must be an array of component names");
        }
        const int list = lua_absindex(state, -1);
        for (lua_Integer index = 1;; ++index)
        {
            lua_rawgeti(state, list, index);
            if (lua_isnil(state, -1)) { lua_pop(state, 1); break; }
            if (lua_type(state, -1) != LUA_TSTRING)
            {
                lua_pop(state, 2);
                return std::unexpected(std::string("system ") + key + " entries must be strings");
            }
            names.emplace_back(lua_tostring(state, -1));
            lua_pop(state, 1);
        }
        lua_pop(state, 1);
        std::sort(names.begin(), names.end());
        if (std::adjacent_find(names.begin(), names.end()) != names.end())
            return std::unexpected(std::string("system ") + key + " contains duplicate components");
        return {};
    }

    static bool system_conflicts(const System& left, const System& right)
    {
        if (left.phase != right.phase || left.order != right.order) return false;
        const auto overlaps = [](const auto& first, const auto& second)
        {
            for (const auto& name : first)
                if (std::find(second.begin(), second.end(), name) != second.end()) return true;
            return false;
        };
        return overlaps(left.writes, right.reads) || overlaps(left.writes, right.writes) ||
            overlaps(right.writes, left.reads);
    }

    Result parse_systems(const int tableIndex, Script& script)
    {
        const int originalTop = lua_gettop(state);
        struct StackRestore { lua_State* state; int top; ~StackRestore() { lua_settop(state, top); } } restore{state, originalTop};
        const int table = lua_absindex(state, tableIndex);
        lua_pushliteral(state, "systems"); lua_rawget(state, table);
        if (lua_isnil(state, -1)) return {};
        if (!lua_istable(state, -1)) return std::unexpected("script systems member must be an array");
        const int list = lua_absindex(state, -1);
        for (lua_Integer i = 1;; ++i)
        {
            lua_rawgeti(state, list, i);
            if (lua_isnil(state, -1)) { lua_pop(state, 1); break; }
            if (!lua_istable(state, -1)) return std::unexpected("each system must be a table");
            const int item = lua_absindex(state, -1);
            lua_pushliteral(state, "name"); lua_rawget(state, item);
            if (lua_type(state, -1) != LUA_TSTRING || !*lua_tostring(state, -1))
                return std::unexpected("system name must be a non-empty string");
            System system; system.name = lua_tostring(state, -1); lua_pop(state, 1);
            for (auto [key, names] : {std::pair{"all", &system.all},
                std::pair{"reads", &system.reads}, std::pair{"writes", &system.writes}})
            {
                auto parsed = read_system_names(item, key, *names);
                if (!parsed) return parsed;
            }
            if (system.all.empty()) return std::unexpected("system must require at least one component");
            for (const auto& name : system.all)
                if (std::find(system.reads.begin(), system.reads.end(), name) == system.reads.end() &&
                    std::find(system.writes.begin(), system.writes.end(), name) == system.writes.end())
                    return std::unexpected("system query component must be declared in reads or writes");
            for (const char* key : {"phase", "order"})
            {
                lua_pushstring(state, key); lua_rawget(state, item);
                if (!lua_isnil(state, -1))
                {
                    int valid = 0;
                    const auto value = lua_tointegerx(state, -1, &valid);
                    if (!valid || value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
                        return std::unexpected(std::string("system ") + key + " must be an integer");
                    if (std::strcmp(key, "phase") == 0) system.phase = static_cast<int>(value);
                    else system.order = static_cast<int>(value);
                }
                lua_pop(state, 1);
            }
            lua_pushliteral(state, "update"); lua_rawget(state, item);
            if (!lua_isfunction(state, -1)) return std::unexpected("system update member must be a function");
            system.update_ref = luaL_ref(state, LUA_REGISTRYINDEX);
            for (const auto& other : script.systems)
                if (system_conflicts(system, other))
                {
                    luaL_unref(state, LUA_REGISTRYINDEX, system.update_ref);
                    return std::unexpected("unordered systems have conflicting component access");
                }
            for (const auto& [id, loaded] : scripts)
            {
                (void)id;
                for (const auto& other : loaded.systems)
                    if (system_conflicts(system, other))
                    {
                        luaL_unref(state, LUA_REGISTRYINDEX, system.update_ref);
                        return std::unexpected("unordered systems have conflicting component access");
                    }
            }
            script.systems.push_back(std::move(system));
            lua_pop(state, 1);
        }
        return {};
    }

    Result validate_lifecycle(const int tableIndex)
    {
        const int table = lua_absindex(state, tableIndex);
        for (const char* method : {"on_create", "on_update", "on_destroy"})
        {
            lua_pushstring(state, method); lua_rawget(state, table);
            const bool valid = lua_isnil(state, -1) || lua_isfunction(state, -1);
            lua_pop(state, 1);
            if (!valid) return std::unexpected(std::string("script lifecycle member '") + method +
                "' must be a function");
        }
        return {};
    }

    Result run_system(const Script& script, const System& system, const float delta)
    {
        if (!api.query_components) return {};
        struct Scope
        {
            Impl& owner; const LuaScriptContext* previousContext; const System* previousSystem;
            std::filesystem::path previousDirectory;
            Scope(Impl& owner, const Script& script, const System& system) : owner(owner),
                previousContext(owner.active_context), previousSystem(owner.active_system),
                previousDirectory(owner.module_directory)
            { owner.active_context = &script.context; owner.active_system = &system;
                owner.module_directory = script.module_directory; }
            ~Scope() { owner.active_context = previousContext; owner.active_system = previousSystem;
                owner.module_directory = previousDirectory; }
        } scope(*this, script, system);
        auto entities = api.query_components(system.all);
        std::sort(entities.begin(), entities.end());
        entities.erase(std::unique(entities.begin(), entities.end()), entities.end());
        for (const auto entity : entities)
        {
            lua_rawgeti(state, LUA_REGISTRYINDEX, system.update_ref);
            lua_pushinteger(state, static_cast<lua_Integer>(entity));
            lua_pushnumber(state, delta);
            if (protected_call(2, 0) != LUA_OK)
                return std::unexpected("system '" + system.name + "': " + pop_error());
        }
        return {};
    }

    LoadResult load(std::string_view source, std::string_view name, LuaScriptContext context = {})
    {
        if (closed || !state) return std::unexpected("Lua runtime is shut down");
        if (declaration_validation) validation_hook_ticks = 0;
        if (!module_root.empty())
        {
            const std::string chunk(name);
            const std::filesystem::path sourcePath(chunk.starts_with('@') ? chunk.substr(1) : chunk);
            module_directory = sourcePath.has_parent_path()
                ? std::filesystem::weakly_canonical(sourcePath.parent_path()) : module_root;
            const auto relative = module_directory.lexically_relative(module_root);
            if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
                return std::unexpected("script is outside the project module root");
        }
        struct RegistrationScope
        {
            Impl& owner;
            std::vector<Registration> pending;
            std::vector<Registration>* previous;
            bool committed = false;
            explicit RegistrationScope(Impl& value) : owner(value), previous(value.loading_registrations)
            { owner.loading_registrations = &pending; }
            ~RegistrationScope()
            {
                owner.loading_registrations = previous;
                if (!committed)
                    for (auto it = pending.rbegin(); it != pending.rend(); ++it)
                        if (it->created)
                        {
                            if (owner.api.rollback_component_schema) owner.api.rollback_component_schema(it->name);
                            else if (owner.api.unregister_component) owner.api.unregister_component(it->name);
                        }
            }
            void commit(Script& script)
            {
                for (const auto& item : pending)
                {
                    script.registrations.push_back(item.name);
                    ++owner.schema_owners[item.name];
                    if (item.created) owner.script_created_schemas.insert(item.name);
                }
                committed = true;
            }
        } registrations(*this);
        const std::string chunk_name(name);
        if (luaL_loadbuffer(state, source.data(), source.size(), chunk_name.c_str()) != LUA_OK)
            return std::unexpected(pop_error());

        // Give each script a private global table while retaining standard Lua libraries.
        lua_newtable(state);
        populate_host_globals(bool(context.owner));
        lua_newtable(state);
        if (declaration_validation || !module_root.empty()) push_root_safe_globals();
        else lua_pushglobaltable(state);
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
        const auto lifecycle = validate_lifecycle(-1);
        if (!lifecycle) { lua_pop(state, 1); return std::unexpected(lifecycle.error()); }
        if (next_id == std::numeric_limits<std::uint64_t>::max())
        {
            lua_pop(state, 1);
            return std::unexpected("Lua script id space exhausted");
        }

        Script script{LuaScriptId{next_id++}, LUA_NOREF, std::move(context), {}, {}, module_directory};
        auto parsedSystems = parse_systems(-1, script);
        if (!parsedSystems)
        {
            for (const System& system : script.systems) luaL_unref(state, LUA_REGISTRYINDEX, system.update_ref);
            lua_pop(state, 1);
            return std::unexpected(parsedSystems.error());
        }
        script.table_ref = luaL_ref(state, LUA_REGISTRYINDEX);
        auto [it, inserted] = scripts.emplace(script.id.value, script);
        (void)inserted;
        auto created = declaration_validation ? Result{} : call(it->second, "on_create");
        if (!created)
        {
            // Give partially initialized scripts a chance to undo native side effects.
            if (!declaration_validation) (void)call(it->second, "on_destroy");
            luaL_unref(state, LUA_REGISTRYINDEX, script.table_ref);
            for (const System& system : script.systems) luaL_unref(state, LUA_REGISTRYINDEX, system.update_ref);
            scripts.erase(it);
            return std::unexpected(created.error());
        }
        registrations.commit(it->second);
        return script.id;
    }

    void release_schemas(const Script& script, const bool discardNew = false)
    {
        for (const auto& name : script.registrations)
        {
            auto found = schema_owners.find(name);
            if (found == schema_owners.end()) continue;
            if (--found->second != 0) continue;
            schema_owners.erase(found);
            if (script_created_schemas.contains(name))
            {
                const bool removed = discardNew && api.rollback_component_schema
                    ? api.rollback_component_schema(name)
                    : api.unregister_component && api.unregister_component(name);
                if (removed) script_created_schemas.erase(name);
            }
        }
    }

    Result remove(const LuaScriptId id, const bool retainOnDestroyFailure = false,
        const bool discardNewSchemas = false)
    {
        auto it = scripts.find(id.value);
        if (it == scripts.end()) return std::unexpected("unknown Lua script id");
        auto destroyed = declaration_validation ? Result{} : call(it->second, "on_destroy");
        if (!destroyed && retainOnDestroyFailure) return destroyed;
        luaL_unref(state, LUA_REGISTRYINDEX, it->second.table_ref);
        for (const System& system : it->second.systems) luaL_unref(state, LUA_REGISTRYINDEX, system.update_ref);
        release_schemas(it->second, discardNewSchemas);
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
                auto result = declaration_validation ? Result{} : call(script, "on_destroy");
                if (!result && first_error.empty()) first_error = result.error();
                release_schemas(script);
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
    return load_file(path, {});
}

LuaScriptSystem::LoadResult LuaScriptSystem::load_file(const std::string& path, LuaScriptContext context)
{
    const auto impl = m_impl;
    if (!impl || impl->closed) return std::unexpected("Lua runtime is shut down");
    if (impl->lua_execution_depth != 0) return std::unexpected("cannot load scripts during Lua execution");
    Impl::ExecutionScope execution(*impl);
    auto source = scripting::read_lua_source_file(path);
    if (!source) return std::unexpected(
        std::string(scripting::lua_source_error_message(source.error())) + ": " + path);
    return impl->load(*source, "@" + path, std::move(context));
}

LuaScriptSystem::LoadResult LuaScriptSystem::load_string(const std::string_view source, const std::string_view chunk_name)
{
    return load_string(source, {}, chunk_name);
}

LuaScriptSystem::LoadResult LuaScriptSystem::load_string(const std::string_view source, LuaScriptContext context, const std::string_view chunk_name)
{
    const auto impl = m_impl;
    if (!impl) return std::unexpected("Lua runtime unavailable");
    if (impl->closed) return std::unexpected("Lua runtime is shut down");
    if (impl->lua_execution_depth != 0) return std::unexpected("cannot load scripts during Lua execution");
    Impl::ExecutionScope execution(*impl);
    return impl->load(source, chunk_name, std::move(context));
}

LuaScriptSystem::Result LuaScriptSystem::validate_string(const std::string_view source, const std::string_view chunk_name)
{
    const auto impl = m_impl;
    if (!impl || impl->closed || !impl->state) return std::unexpected("Lua runtime is shut down");
    if (impl->lua_execution_depth != 0) return std::unexpected("cannot validate scripts during Lua execution");
    Impl::ExecutionScope execution(*impl);
    const std::string name(chunk_name);
    if (luaL_loadbuffer(impl->state, source.data(), source.size(), name.c_str()) != LUA_OK)
        return std::unexpected(impl->pop_error());
    lua_pop(impl->state, 1);
    return {};
}

LuaScriptSystem::Result LuaScriptSystem::validate_declarations(
    const std::vector<std::pair<std::string, std::string>>& sources,
    const std::filesystem::path& project_root)
{
    using DeclaredSchema = std::pair<std::uint32_t, std::vector<LuaComponentField>>;
    std::map<std::string, DeclaredSchema> schemas;
    std::string schemaError;
    EngineScriptApi api;
    api.register_component = [&](std::string_view name, const std::vector<LuaComponentField>& fields,
        std::uint32_t version)
    {
        auto ordered = fields;
        std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right)
        { return left.name < right.name; });
        const auto found = schemas.find(std::string(name));
        if (found == schemas.end())
        {
            size_t properties = fields.size();
            for (const auto& [registeredName, schema] : schemas)
            { (void)registeredName; properties += schema.second.size(); }
            if (schemas.size() >= ecs::max_dynamic_component_types ||
                properties > ecs::max_dynamic_properties_per_project)
            {
                schemaError = "project component registration limit exceeded";
                return false;
            }
            schemas.emplace(std::string(name), DeclaredSchema{version, std::move(ordered)});
            return true;
        }
        const bool matching = found->second.first == version && found->second.second == ordered;
        if (!matching) schemaError = "component schema conflicts with another script: " + std::string(name);
        return matching;
    };
    api.has_component_schema = [&](std::string_view name) { return schemas.contains(std::string(name)); };
    api.unregister_component = api.rollback_component_schema = [&](std::string_view name)
    { return schemas.erase(std::string(name)) != 0; };
    api.module_root = project_root;
    Impl validator(std::move(api), true);
    if (!validator.state) return std::unexpected("unable to allocate declaration validator");
    std::map<std::uint64_t, std::string> names;
    for (const auto& [source, chunkName] : sources)
    {
        schemaError.clear();
        auto loaded = validator.load(source, chunkName, LuaScriptContext{.owner = 1});
        if (!loaded) return std::unexpected(chunkName + ": " + loaded.error());
        if (!schemaError.empty()) return std::unexpected(chunkName + ": " + schemaError);
        names.emplace(loaded->value, chunkName);
    }
    for (const auto& [id, script] : validator.scripts)
    {
        for (const auto& system : script.systems)
        {
            for (const auto* components : {&system.all, &system.reads, &system.writes})
                for (const auto& name : *components)
                    if (name != "Position3D" && !schemas.contains(name))
                        return std::unexpected(names.at(id) + ": system '" + system.name +
                            "' references an undeclared component: " + name);
        }
    }
    return {};
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
    struct Scheduled { const Impl::Script* script; const Impl::System* system; };
    std::vector<Scheduled> schedule;
    for (const auto& [id, script] : impl->scripts)
    {
        (void)id;
        for (const auto& system : script.systems) schedule.push_back({&script, &system});
    }
    std::stable_sort(schedule.begin(), schedule.end(), [](const Scheduled& left, const Scheduled& right)
    {
        if (left.system->phase != right.system->phase) return left.system->phase < right.system->phase;
        return left.system->order < right.system->order;
    });
    if (first_error.empty())
        for (const auto& scheduled : schedule)
        {
            auto result = impl->run_system(*scheduled.script, *scheduled.system, delta_seconds);
            if (!result) { first_error = result.error(); break; }
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

LuaScriptSystem::Result LuaScriptSystem::discardCandidate(const LuaScriptId id)
{
    const auto impl = m_impl;
    if (!impl || impl->closed) return std::unexpected("Lua runtime is shut down");
    if (impl->lua_execution_depth != 0) return std::unexpected("cannot unload scripts during Lua execution");
    Impl::ExecutionScope execution(*impl);
    return impl->remove(id, false, true);
}

LuaScriptSystem::Result LuaScriptSystem::unloadRetainingOnDestroyFailure(const LuaScriptId id)
{
    const auto impl = m_impl;
    if (!impl || impl->closed) return std::unexpected("Lua runtime is shut down");
    if (impl->lua_execution_depth != 0) return std::unexpected("cannot unload scripts during Lua execution");
    Impl::ExecutionScope execution(*impl);
    return impl->remove(id, true);
}

LuaScriptSystem::Result LuaScriptSystem::shutdown()
{
    const auto impl = m_impl;
    if (!impl) return {};
    if (impl->lua_execution_depth != 0) return std::unexpected("cannot shut down Lua during script execution");
    return impl->close();
}
