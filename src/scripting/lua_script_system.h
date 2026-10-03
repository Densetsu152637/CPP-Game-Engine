#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <functional>
#include <variant>
#include <vector>

struct LuaScriptId
{
    std::uint64_t value = 0;
    explicit operator bool() const noexcept { return value != 0; }
    bool operator==(const LuaScriptId&) const = default;
};

enum class LuaComponentFieldType { Number, Boolean, String };
using LuaComponentValue = std::variant<double, bool, std::string>;
struct LuaComponentField
{
    std::string name;
    LuaComponentFieldType type;
    std::uint32_t version = 1;
    std::optional<LuaComponentValue> default_value;
    bool operator==(const LuaComponentField&) const = default;
};
using LuaComponentValues = std::vector<std::pair<std::string, LuaComponentValue>>;

struct LuaMoveResult
{
    std::array<float, 2> position{};
    std::vector<std::string> contacts;
};
struct LuaUiPanel
{
    std::string id, font, text;
    std::array<float, 4> rect{8, 8, 304, 164};
    std::array<float, 4> color{1, 1, 1, 1};
    float scale = 1;
    bool modal = true;
    std::vector<std::string> choices;
};
struct LuaUiEvent { std::string panel, type; std::size_t selection = 0; };
struct LuaTriggerEvent { std::string first, second, phase; };

// Engine services are injected so scripts do not depend on a particular ECS implementation.
struct EngineScriptApi
{
    std::function<void(std::string_view)> log;
    std::function<std::int64_t()> create_entity;
    std::function<bool(std::int64_t)> is_entity_alive;
    std::function<bool(std::int64_t)> destroy_entity;
    std::function<bool(std::int64_t, float, float, float)> set_position;
    std::function<std::optional<std::array<float, 3>>(std::int64_t)> get_position;
    // Dynamic components are stored by the host ECS in dense columns. Query
    // results must be sorted by packed entity id and remain a snapshot.
    std::function<bool(std::string_view, const std::vector<LuaComponentField>&, std::uint32_t)> register_component;
    std::function<bool(std::string_view)> has_component_schema;
    std::function<bool(std::string_view)> unregister_component;
    std::function<bool(std::string_view)> rollback_component_schema;
    std::function<bool(std::int64_t, std::string_view, const LuaComponentValues&)> set_component;
    std::function<std::optional<LuaComponentValues>(std::int64_t, std::string_view)> get_component;
    std::function<bool(std::int64_t, std::string_view)> remove_component;
    std::function<std::vector<std::int64_t>(const std::vector<std::string>&)> query_components;
    std::function<std::optional<std::int64_t>(std::string_view)> find_entity;
    std::function<std::expected<LuaMoveResult, std::string>(std::int64_t, float, float)> move;
    std::function<std::expected<std::vector<std::string>, std::string>(std::int64_t)> overlaps;
    std::function<std::expected<void, std::string>(std::string_view, std::string_view, std::string_view)> change_scene;
    std::function<bool(std::int64_t, std::uint32_t)> set_sprite_frame;
    std::function<bool(std::int64_t, bool)> set_sprite_visible;
    std::function<std::expected<void, std::string>(const LuaUiPanel&)> ui_open;
    std::function<bool(std::string_view)> ui_close;
    std::function<bool(std::string_view, std::string)> ui_set_text;
    std::function<bool(std::string_view, float)> ui_scroll;
    std::function<std::optional<LuaUiEvent>()> ui_event;
    std::function<std::expected<std::uint64_t, std::string>(std::string_view, bool, std::string_view, float)> audio_play;
    std::function<bool(std::uint64_t)> audio_stop;
    std::function<std::expected<void, std::string>(std::string_view, float)> audio_volume;
    // JSON crosses the host boundary; wrappers expose bounded Lua objects.
    std::function<std::expected<void, std::string>(std::string_view, std::string_view)> save_write;
    std::function<std::expected<std::string, std::string>(std::string_view, bool)> save_read;
    std::function<std::expected<void, std::string>(std::string_view)> save_recover;
    std::function<std::expected<void, std::string>(std::string_view)> settings_write;
    std::function<std::expected<std::string, std::string>()> settings_read;
    std::function<std::expected<void, std::string>(std::string_view)> state_write;
    std::function<std::string()> state_read;
    std::function<bool(std::string_view, bool)> lock_controls;
    std::function<bool(float, float)> set_camera;
    std::function<void()> reset_camera;
    std::function<std::expected<std::optional<std::array<float, 2>>, std::string>(std::int64_t, float, float)> safe_position;
    std::function<std::vector<LuaTriggerEvent>()> triggers;
    // Project CLI mode routes ordinary Lua output away from machine-readable stdout.
    bool redirect_standard_output = false;
    // When set, require resolves only modules within this project root.
    std::filesystem::path module_root;
};

// Per-instance capabilities supplied to an entity-owned script. The owner is
// a generation-aware runtime handle; it is never a persistent project ID.
struct LuaScriptContext
{
    std::optional<std::int64_t> owner;
    std::function<bool(std::string_view)> pressed;
    std::function<bool(std::string_view)> held;
    std::function<bool(std::string_view)> released;
    std::function<float(std::string_view)> value;
    std::function<std::array<float, 2>()> pointer;
    std::function<std::array<float, 2>()> wheel;
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
    // Validate top-level schemas and system declarations in a restricted Lua
    // state without constructing entities or invoking lifecycle callbacks.
    static Result validate_declarations(const std::vector<std::pair<std::string, std::string>>& scripts,
        const std::filesystem::path& project_root);
    LoadResult load_string(std::string_view source, std::string_view chunk_name = "script");
    LoadResult load_string(std::string_view source, LuaScriptContext context, std::string_view chunk_name = "script");
    Result update(float delta_seconds);
    Result unload(LuaScriptId id);
    // Discard a prepared replacement whose predecessor could not be retired.
    Result discardCandidate(LuaScriptId id);
    // Used by transactional replacement: retain the script table when its
    // on_destroy callback fails so the caller can discard the candidate.
    Result unloadRetainingOnDestroyFailure(LuaScriptId id);
    Result shutdown();

private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;
};
