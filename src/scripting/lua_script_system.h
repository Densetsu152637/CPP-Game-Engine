#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <functional>

struct LuaScriptId
{
    std::uint64_t value = 0;
    explicit operator bool() const noexcept { return value != 0; }
    bool operator==(const LuaScriptId&) const = default;
};

// Engine services are injected so scripts do not depend on a particular ECS implementation.
struct EngineScriptApi
{
    std::function<void(std::string_view)> log;
    std::function<std::int64_t()> create_entity;
    std::function<bool(std::int64_t)> is_entity_alive;
    std::function<bool(std::int64_t)> destroy_entity;
    std::function<bool(std::int64_t, float, float, float)> set_position;
    std::function<std::optional<std::array<float, 3>>(std::int64_t)> get_position;
    // Project CLI mode routes ordinary Lua output away from machine-readable stdout.
    bool redirect_standard_output = false;
};

// Per-instance capabilities supplied to an entity-owned script. The owner is
// a generation-aware runtime handle; it is never a persistent project ID.
struct LuaScriptContext
{
    std::optional<std::int64_t> owner;
    std::function<bool(std::string_view)> pressed;
    std::function<bool(std::string_view)> held;
    std::function<bool(std::string_view)> released;
};

class LuaScriptSystem
{
public:
    using Result = std::expected<void, std::string>;
    using LoadResult = std::expected<LuaScriptId, std::string>;

    explicit LuaScriptSystem(EngineScriptApi api);
    ~LuaScriptSystem();
    LuaScriptSystem(const LuaScriptSystem&) = delete;
    LuaScriptSystem& operator=(const LuaScriptSystem&) = delete;
    LuaScriptSystem(LuaScriptSystem&&) = delete;
    LuaScriptSystem& operator=(LuaScriptSystem&&) = delete;

    // A script chunk returns a table containing optional on_create, on_update(dt), and on_destroy functions.
    LoadResult load_file(const std::string& path);
    LoadResult load_file(const std::string& path, LuaScriptContext context);
    // Parse/compile source without executing its chunk or lifecycle callbacks.
    Result validate_string(std::string_view source, std::string_view chunk_name = "script");
    LoadResult load_string(std::string_view source, std::string_view chunk_name = "script");
    LoadResult load_string(std::string_view source, LuaScriptContext context, std::string_view chunk_name = "script");
    Result update(float delta_seconds);
    Result unload(LuaScriptId id);
    // Used by transactional replacement: retain the script table when its
    // on_destroy callback fails so the caller can discard the candidate.
    Result unloadRetainingOnDestroyFailure(LuaScriptId id);
    Result shutdown();

private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;
};
