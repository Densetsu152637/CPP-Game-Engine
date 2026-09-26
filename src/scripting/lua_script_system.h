#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

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
    LoadResult load_string(std::string_view source, std::string_view chunk_name = "script");
    Result update(float delta_seconds);
    Result unload(LuaScriptId id);
    Result shutdown();

private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;
};
