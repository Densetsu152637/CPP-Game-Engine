#include "runtime.h"

#include "../async/threadpool.h"
#include "../components/alias.h"
#include "../ecs/processor.h"
#include "../scripting/lua_script_system.h"
#include "../scripting/lua_source_file.h"
#include "../rendering/sprite2d.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <iterator>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <utility>

namespace project
{
    struct Runtime::Impl
    {
        Project project;
        RuntimeOptions options;
        const std::thread::id ownerThread = std::this_thread::get_id();
        Threadpool pool{1, std::string("project runtime")};
        ECSProcessor simulator{pool};
        std::unique_ptr<LuaScriptSystem> scripts;
        std::map<std::string, std::int64_t> entities;
        struct Handles { std::int64_t next = std::int64_t{1} << 32; std::uint64_t scope = 0; };
        std::shared_ptr<Handles> handles = std::make_shared<Handles>();
        std::map<std::int64_t, Entity> nativeHandles;
        std::map<std::uint64_t, std::int64_t> publicHandles;
        std::map<std::int64_t, std::array<float, 3>> positions;
        gameplay2d::World world;
        ui::UiModel ui;
        std::vector<gameplay2d::TriggerEvent> triggerEvents;
        std::map<std::string, SpriteRenderer> spriteState;
        std::map<std::string, std::uint32_t> forcedFrames;
        std::map<std::string, std::shared_ptr<const audio::Clip>> clips;
        struct SceneRequest { std::string asset, spawn, traveller; };
        std::optional<SceneRequest> pendingScene;
        std::uint64_t revision = 1;
        std::string audioOwner;
        bool preparing = false;
        picojson::object sharedState;
        std::set<std::string> controlLocks;
        std::optional<std::array<float, 2>> cameraCenter;
        std::unordered_set<std::int64_t> spawnedEntities;
        std::map<std::string, LuaScriptId> scriptInstances;
        struct PendingReload { std::string entityId; std::string source; std::string chunkName; };
        std::deque<PendingReload> pendingReloads;
        const InputSnapshot* input = nullptr;
        std::uint64_t ticks = 0;
        std::uint64_t sceneTicks = 0;
        bool started = false;
        bool faulted = false;

        bool onOwnerThread() const noexcept { return ownerThread == std::this_thread::get_id(); }

        Impl(Project value, RuntimeOptions config) : project(std::move(value)), options(config), ui(config.glyphAdvance) {}

        Entity find(std::int64_t packed) const
        {
            const auto found = nativeHandles.find(packed);
            return found == nativeHandles.end() ? Entity{} : found->second;
        }

        std::int64_t expose(Entity entity)
        {
            if (handles->next == std::numeric_limits<std::int64_t>::max()) throw std::runtime_error("runtime entity handle limit exceeded");
            const auto id = handles->next++;
            nativeHandles.emplace(id, entity); publicHandles.emplace(entity.packed(), id); return id;
        }

        std::string stable(std::int64_t handle) const
        { for (const auto& [id, value] : entities) if (value == handle) return id; return {}; }

        void synchronizeWorld()
        {
            for (const auto& authored : project.scene.entities)
            {
                if (!authored.collider2D) continue;
                const auto found = entities.find(authored.id);
                const auto position = found == entities.end() ? positions.end() : positions.find(found->second);
                if (position == positions.end() || !simulator.ecs().hasEntity(find(found->second))) { world.remove(authored.id); continue; }
                const auto& p = position->second; const auto& collider = *authored.collider2D;
                const auto result = world.upsert(authored.id, {{p[0], p[1]}, collider.size, collider.offset, collider.trigger, collider.layer, collider.mask});
                if (!result) throw std::runtime_error(result.error());
            }
        }

        static std::expected<picojson::object, std::string> parseObject(std::string_view text)
        {
            if (text.size() > 1024 * 1024) return std::unexpected("save exceeds byte limit");
            picojson::value value; const auto error = picojson::parse(value, std::string(text));
            if (!error.empty() || !value.is<picojson::object>()) return std::unexpected("expected a valid JSON object");
            return value.get<picojson::object>();
        }

        std::expected<audio::Bus, std::string> bus(std::string_view name) const
        {
            if (name == "master") return audio::Bus::Master;
            if (name == "music") return audio::Bus::Music;
            if (name == "effects") return audio::Bus::Effects;
            if (name == "dialogue") return audio::Bus::Dialogue;
            return std::unexpected("unknown audio bus");
        }

        std::expected<void, std::string> applySettings(const picojson::object& object, bool persist)
        {
            std::optional<interaction::BindingMap> bindings;
            std::vector<std::pair<audio::Bus, float>> gains;
            if (const auto item = object.find("bindings"); item != object.end())
            {
                if (!options.inputMapper || !item->second.is<picojson::object>()) return std::unexpected("settings bindings require an available mapper and object");
                bindings = interaction::BindingMap{};
                for (const auto& [action, value] : item->second.get<picojson::object>())
                {
                    if (!options.allowedActions.contains(action) || !value.is<picojson::array>()) return std::unexpected("settings contain an undeclared action or malformed bindings");
                    auto& tokens = (*bindings)[action];
                    for (const auto& token : value.get<picojson::array>())
                    { if (!token.is<std::string>()) return std::unexpected("binding tokens must be strings"); tokens.push_back(token.get<std::string>()); }
                }
                const auto validation = interaction::validateBindings(*bindings);
                if (!validation.valid()) return std::unexpected(validation.errors.front());
            }
            if (const auto item = object.find("audio"); item != object.end())
            {
                if (!options.audio || !item->second.is<picojson::object>()) return std::unexpected("settings audio requires an available mixer and object");
                for (const auto& [channel, value] : item->second.get<picojson::object>())
                {
                    const auto channelBus = bus(channel); if (!channelBus) return std::unexpected(channelBus.error());
                    if (!value.is<double>() || !std::isfinite(value.get<double>()) || value.get<double>() < 0 || value.get<double>() > 1) return std::unexpected("audio gains must be in [0,1]");
                    gains.emplace_back(*channelBus, static_cast<float>(value.get<double>()));
                }
            }
            if (persist)
            {
                if (!options.persistence) return std::unexpected("settings persistence unavailable");
                const auto saved = options.persistence->saveSettings(object);
                if (!saved) return std::unexpected(saved.error().front().message);
            }
            if (bindings) options.inputMapper->rebind(std::move(*bindings));
            for (const auto& [channel, gain] : gains) options.audio->setBusGain(channel, gain);
            return {};
        }

        void fault()
        { faulted = true; ui.clear(); pendingScene.reset(); controlLocks.clear(); cameraCenter.reset(); if (options.audio) options.audio->stopOwner(audioOwner); }

        EngineScriptApi makeApi()
        {
            EngineScriptApi api;
            api.log = [this](std::string_view message)
            {
                if (options.log) options.log(message);
                else std::clog << "[project script] " << message << '\n';
            };
            api.redirect_standard_output = true;
            api.module_root = project.root;
            api.create_entity = [this]() -> std::int64_t
            {
                const auto id = expose(simulator.ecs().createEntity());
                spawnedEntities.insert(id);
                return id;
            };
            api.is_entity_alive = [this](std::int64_t packed)
            { return packed >= 0 && simulator.ecs().hasEntity(find(packed)); };
            api.destroy_entity = [this](std::int64_t packed)
            {
                if (packed < 0) return false;
                auto& ecs = simulator.ecs();
                const Entity entity = find(packed);
                if (!ecs.knowsEntityHandle(entity)) return false;
                ecs.destroyEntity(entity);
                spawnedEntities.erase(packed);
                return true;
            };
            api.set_position = [this](std::int64_t packed, float x, float y, float z)
            {
                if (packed < 0 || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
                auto& ecs = simulator.ecs();
                const Entity entity = find(packed);
                if (!ecs.knowsEntityHandle(entity)) return false;
                ecs.setComponent<Position3D>(entity, Vector3f{x, y, z});
                positions[packed] = {x, y, z};
                return true;
            };
            api.get_position = [this](std::int64_t packed) -> std::optional<std::array<float, 3>>
            {
                if (packed < 0) return std::nullopt;
                if (!simulator.ecs().knowsEntityHandle(find(packed))) return std::nullopt;
                const auto found = positions.find(packed);
                return found == positions.end() ? std::nullopt : std::optional(found->second);
            };
            api.register_component = [this](std::string_view name, const std::vector<LuaComponentField>& fields,
                std::uint32_t version)
            {
                std::vector<ecs::DynamicField> schema;
                schema.reserve(fields.size());
                for (const auto& field : fields)
                {
                    ecs::DynamicFieldType type = ecs::DynamicFieldType::String;
                    switch (field.type)
                    {
                    case LuaComponentFieldType::Number: type = ecs::DynamicFieldType::Number; break;
                    case LuaComponentFieldType::Boolean: type = ecs::DynamicFieldType::Boolean; break;
                    case LuaComponentFieldType::String: type = ecs::DynamicFieldType::String; break;
                    }
                    schema.push_back({field.name, type, field.version, field.default_value});
                }
                std::sort(schema.begin(), schema.end(), [](const auto& left, const auto& right)
                { return left.name < right.name; });
                return simulator.ecs().registerDynamicComponent(name, schema, version);
            };
            api.has_component_schema = [this](std::string_view name)
            { return simulator.ecs().hasDynamicComponentSchema(name); };
            api.unregister_component = [this](std::string_view name)
            { return simulator.ecs().unregisterDynamicComponent(name); };
            api.rollback_component_schema = [this](std::string_view name)
            { return simulator.ecs().rollbackDynamicComponentSchema(name); };
            api.set_component = [this](std::int64_t packed, std::string_view name, const LuaComponentValues& values)
            {
                if (packed < 0) return false;
                auto& ecs = simulator.ecs();
                const Entity entity = find(packed);
                if (!ecs.knowsEntityHandle(entity)) return false;
                return ecs.setDynamicComponent(entity, name, values);
            };
            api.get_component = [this](std::int64_t packed, std::string_view name) -> std::optional<LuaComponentValues>
            {
                if (packed < 0) return std::nullopt;
                return simulator.ecs().getDynamicComponent(find(packed), name);
            };
            api.remove_component = [this](std::int64_t packed, std::string_view name)
            {
                if (packed < 0) return false;
                auto& ecs = simulator.ecs();
                const Entity entity = find(packed);
                if (!ecs.knowsEntityHandle(entity)) return false;
                return ecs.removeDynamicComponent(entity, name);
            };
            api.query_components = [this](const std::vector<std::string>& all)
            {
                auto& ecs = simulator.ecs();
                const bool needsPosition = std::find(all.begin(), all.end(), "Position3D") != all.end();
                std::vector<std::string> dynamicNames;
                dynamicNames.reserve(all.size());
                for (const auto& name : all)
                    if (name != "Position3D") dynamicNames.push_back(name);
                std::vector<Entity> matching;
                if (dynamicNames.empty() && needsPosition)
                {
                    const auto positioned = ecs.matchingEntities<Position3D>();
                    matching.assign(positioned.begin(), positioned.end());
                }
                else matching = ecs.queryDynamicComponents(dynamicNames);
                std::vector<std::int64_t> result;
                for (const Entity& entity : matching)
                {
                    if (needsPosition && !ecs.hasComponent<Position3D>(entity)) continue;
                    const auto found = publicHandles.find(entity.packed());
                    if (found != publicHandles.end()) result.push_back(found->second);
                }
                std::sort(result.begin(), result.end());
                return result;
            };
            api.find_entity = [this](std::string_view id) -> std::optional<std::int64_t>
            { const auto found = entities.find(std::string(id)); if (found == entities.end() || !simulator.ecs().hasEntity(find(found->second))) return std::nullopt; return found->second; };
            api.move = [this](std::int64_t handle, float x, float y) -> std::expected<LuaMoveResult, std::string>
            {
                if (!std::isfinite(x) || !std::isfinite(y)) return std::unexpected("movement must be finite");
                synchronizeWorld(); const auto id = stable(handle);
                const auto moved = world.move(id, {x, y}); if (!moved) return std::unexpected(moved.error());
                auto* position = simulator.ecs().try_get_mut<Position3D>(find(handle));
                if (!position) return std::unexpected("entity has no position");
                const auto old = positions.at(handle); *position = Vector3f{moved->position[0], moved->position[1], old[2]};
                positions[handle] = {moved->position[0], moved->position[1], old[2]};
                return LuaMoveResult{moved->position, moved->contacts};
            };
            api.overlaps = [this](std::int64_t handle) -> std::expected<std::vector<std::string>, std::string>
            { synchronizeWorld(); return world.overlaps(stable(handle)); };
            api.change_scene = [this](std::string_view asset, std::string_view spawn, std::string_view traveller) -> std::expected<void, std::string>
            {
                if (preparing) return std::unexpected("scene preparation cannot request another transition");
                const auto found = project.assets.find(asset);
                if (found == project.assets.end() || found->second.kind != "scene") return std::unexpected("scene asset is not declared");
                if (pendingScene) return std::unexpected("a scene transition is already pending");
                pendingScene = SceneRequest{std::string(asset), std::string(spawn), std::string(traveller)}; return {};
            };
            api.set_sprite_frame = [this](std::int64_t handle, std::uint32_t frame)
            { const auto id = stable(handle); const auto found = spriteState.find(id); if (found == spriteState.end() || (!found->second.frames.empty() ? frame >= found->second.frames.size() : frame != 0)) return false; forcedFrames[id] = frame; return true; };
            api.set_sprite_visible = [this](std::int64_t handle, bool visible)
            { const auto found = spriteState.find(stable(handle)); if (found == spriteState.end()) return false; found->second.visible = visible; return true; };
            api.ui_open = [this](const LuaUiPanel& value) -> std::expected<void, std::string>
            {
                const auto font = project.assets.find(value.font);
                if (font == project.assets.end() || font->second.kind != "font") return std::unexpected("UI font asset is not declared");
                ui::PanelOptions panel; panel.id = value.id; panel.fontAsset = value.font; panel.text = value.text;
                panel.rect = {value.rect[0], value.rect[1], value.rect[2], value.rect[3]}; panel.color = value.color;
                panel.scale = value.scale; panel.modal = value.modal; panel.choices = value.choices;
                if (options.fontLineHeight) panel.lineHeight = options.fontLineHeight(value.font);
                if (!ui.open(std::move(panel))) return std::unexpected("invalid UI panel or capacity exceeded"); return {};
            };
            api.ui_close = [this](std::string_view id) { return ui.close(id); };
            api.ui_set_text = [this](std::string_view id, std::string text) { return ui.setText(id, std::move(text)); };
            api.ui_scroll = [this](std::string_view id, float delta) { return ui.scroll(id, delta); };
            api.ui_event = [this]() -> std::optional<LuaUiEvent> { const auto event = ui.pollEvent(); if (!event) return std::nullopt; return LuaUiEvent{event->panel, event->type, event->selection}; };
            api.audio_play = [this](std::string_view asset, bool loop, std::string_view channel, float gain) -> std::expected<std::uint64_t, std::string>
            {
                if (!options.audio) return std::unexpected("audio service unavailable");
                if (preparing) return std::unexpected("play audio after the scene commits, in on_update");
                if (!std::isfinite(gain) || gain < 0 || gain > 1) return std::unexpected("audio gain must be in [0,1]");
                const auto clip = clips.find(std::string(asset)); if (clip == clips.end()) return std::unexpected("audio asset is not loaded");
                const auto channelBus = bus(channel); if (!channelBus) return std::unexpected(channelBus.error());
                const auto played = options.audio->play(clip->second, {loop, *channelBus, gain, audioOwner});
                if (!played) return std::unexpected(played.error().message); return *played;
            };
            api.audio_stop = [this](std::uint64_t voice) { return !preparing && options.audio && options.audio->stop(voice); };
            api.audio_volume = [this](std::string_view channel, float gain) -> std::expected<void, std::string>
            {
                if (preparing || !options.audio) return std::unexpected("audio service unavailable during scene preparation");
                const auto channelBus = bus(channel); if (!channelBus) return std::unexpected(channelBus.error());
                if (!std::isfinite(gain) || gain < 0 || gain > 1) return std::unexpected("audio gain must be in [0,1]");
                options.audio->setBusGain(*channelBus, gain); return {};
            };
            api.save_write = [this](std::string_view slot, std::string_view json) -> std::expected<void, std::string>
            {
                if (preparing || !options.persistence) return std::unexpected("save service unavailable during scene preparation");
                const auto object = parseObject(json); if (!object) return std::unexpected(object.error());
                const auto saved = options.persistence->save(std::string(slot), *object); if (!saved) return std::unexpected(saved.error().front().message); return {};
            };
            api.save_read = [this](std::string_view slot, bool backup) -> std::expected<std::string, std::string>
            {
                if (!options.persistence) return std::unexpected("save service unavailable");
                const auto loaded = backup ? options.persistence->loadBackup(std::string(slot)) : options.persistence->load(std::string(slot));
                if (!loaded) return std::unexpected(loaded.error().front().message); return picojson::value(*loaded).serialize();
            };
            api.save_recover = [this](std::string_view slot) -> std::expected<void, std::string>
            { if (preparing || !options.persistence) return std::unexpected("save recovery unavailable during scene preparation"); const auto recovered = options.persistence->recoverBackup(std::string(slot)); if (!recovered) return std::unexpected(recovered.error().front().message); return {}; };
            api.settings_write = [this](std::string_view json) -> std::expected<void, std::string>
            {
                if (preparing || !options.persistence) return std::unexpected("settings service unavailable during scene preparation");
                const auto object = parseObject(json); if (!object) return std::unexpected(object.error());
                return applySettings(*object, true);
            };
            api.settings_read = [this]() -> std::expected<std::string, std::string>
            { if (!options.persistence) return std::unexpected("settings service unavailable"); const auto loaded = options.persistence->loadSettings(); if (!loaded) return std::unexpected(loaded.error().front().message); if (!preparing) { const auto applied = applySettings(*loaded, false); if (!applied) return std::unexpected(applied.error()); } return picojson::value(*loaded).serialize(); };
            api.state_read = [this] { return picojson::value(sharedState).serialize(); };
            api.state_write = [this](std::string_view json) -> std::expected<void, std::string>
            { const auto object = parseObject(json); if (!object) return std::unexpected(object.error()); sharedState = *object; return {}; };
            api.lock_controls = [this](std::string_view key, bool lock) { if (key.empty() || key.size() > 64) return false; if (lock) { if (controlLocks.size() >= 64) return false; controlLocks.emplace(key); } else controlLocks.erase(std::string(key)); return true; };
            api.set_camera = [this](float x, float y) { if (!std::isfinite(x) || !std::isfinite(y)) return false; cameraCenter = {x, y}; return true; };
            api.reset_camera = [this] { cameraCenter.reset(); };
            api.safe_position = [this](std::int64_t handle, float radius, float step) -> std::expected<std::optional<std::array<float, 2>>, std::string>
            { synchronizeWorld(); const auto id = stable(handle); const auto body = world.body(id); if (!body) return std::unexpected("entity has no collider"); return world.findSafe(*body, radius, step, id); };
            api.triggers = [this] { std::vector<LuaTriggerEvent> result; for (const auto& event : triggerEvents) result.push_back({event.first, event.second, event.phase == gameplay2d::TriggerPhase::Enter ? "enter" : event.phase == gameplay2d::TriggerPhase::Stay ? "stay" : "exit"}); return result; };
            return api;
        }

        LuaScriptContext context(std::int64_t owner)
        {
            LuaScriptContext result;
            result.owner = owner;
            const auto has = [this](const std::set<std::string> InputSnapshot::*field, std::string_view action)
            {
                if (!input || !controlLocks.empty()) return false;
                const auto frame = ui.gameplayFrame();
                if (field == &InputSnapshot::pressed) return frame.pressed.contains(std::string(action));
                if (field == &InputSnapshot::held) return frame.held.contains(std::string(action));
                return frame.released.contains(std::string(action));
            };
            result.pressed = [has](std::string_view action) { return has(&InputSnapshot::pressed, action); };
            result.held = [has](std::string_view action) { return has(&InputSnapshot::held, action); };
            result.released = [has](std::string_view action) { return has(&InputSnapshot::released, action); };
            result.value = [this](std::string_view action) { if (!input || !controlLocks.empty()) return 0.0f; const auto frame = ui.gameplayFrame(); const auto found = frame.values.find(std::string(action)); return found == frame.values.end() ? (frame.held.contains(std::string(action)) ? 1.0f : 0.0f) : found->second; };
            result.pointer = [this] { const auto frame = ui.gameplayFrame(); return controlLocks.empty() ? std::array<float, 2>{frame.pointerX, frame.pointerY} : std::array<float, 2>{}; };
            result.wheel = [this] { const auto frame = ui.gameplayFrame(); return controlLocks.empty() ? std::array<float, 2>{frame.wheelX, frame.wheelY} : std::array<float, 2>{}; };
            return result;
        }

        void destroyEntities() noexcept
        {
            auto& ecs = simulator.ecs();
            for (const auto& [id, packed] : entities)
            {
                (void)id;
                if (packed >= 0)
                {
                    const Entity entity = find(packed);
                    if (ecs.hasEntity(entity)) ecs.destroyEntity(entity);
                }
            }
            for (const auto packed : spawnedEntities)
            {
                if (packed < 0) continue;
                const Entity entity = find(packed);
                if (ecs.hasEntity(entity)) ecs.destroyEntity(entity);
            }
            spawnedEntities.clear();
            entities.clear();
            nativeHandles.clear(); publicHandles.clear(); positions.clear(); world.clear(); spriteState.clear(); forcedFrames.clear(); triggerEvents.clear(); controlLocks.clear(); cameraCenter.reset();
        }
    };

    namespace
    {
        Diagnostics runtimeError(std::string code, std::filesystem::path file, std::string message)
        {
            return {{std::move(code), Severity::Error, std::move(file), {}, std::move(message)}};
        }
    }

    Runtime::Runtime(Project project, RuntimeOptions options) : m_impl(std::make_unique<Impl>(std::move(project), options))
    {
        if (!std::isfinite(m_impl->options.fixedDeltaSeconds) || m_impl->options.fixedDeltaSeconds <= 0.0f)
            throw std::invalid_argument("Runtime fixed delta must be finite and positive");
        if (!m_impl->project.inputActions.empty() || !m_impl->project.inputBindings.empty())
        {
            m_impl->options.allowedActions.clear();
            for (const auto& [action, key] : m_impl->project.inputActions)
            {
                (void)key;
                m_impl->options.allowedActions.insert(action);
            }
            for (const auto& [action, bindings] : m_impl->project.inputBindings)
            { (void)bindings; m_impl->options.allowedActions.insert(action); }
        }
    }

    Runtime::~Runtime() { (void)stop(); }
    Runtime::Runtime(Runtime&&) noexcept = default;
    Runtime& Runtime::operator=(Runtime&&) noexcept = default;

    Result<void> Runtime::start()
    {
        auto& state = *m_impl;
        if (!state.onOwnerThread())
            return std::unexpected(runtimeError("runtime.thread.owner", state.project.scene.source,
                "Runtime must be started on its owning thread"));
        if (state.faulted)
            return std::unexpected(runtimeError("runtime.faulted", state.project.scene.source,
                "Faulted runtime must be stopped before it can start again"));
        if (state.started) return {};
        std::map<std::string, std::filesystem::path> resolvedScripts;
        std::map<std::string, std::string> scriptSources;
        for (const SceneEntity& authored : state.project.scene.entities)
        {
            if (!authored.script) continue;
            auto resolved = resolveAsset(state.project, authored.script->asset);
            if (!resolved) return std::unexpected(resolved.error());
            resolvedScripts.emplace(authored.id, std::move(*resolved));
        }

        state.scripts = std::make_unique<LuaScriptSystem>(state.makeApi());
        for (const SceneEntity& authored : state.project.scene.entities)
        {
            if (!authored.script) continue;
            const auto& path = resolvedScripts.at(authored.id);
            auto source = scripting::read_lua_source_file(path);
            if (!source)
            {
                (void)state.scripts->shutdown();
                state.scripts.reset();
                return std::unexpected(runtimeError(
                    source.error() == scripting::LuaSourceReadError::TooLarge
                        ? "runtime.script.too_large" : "runtime.script.read",
                    path, std::string(scripting::lua_source_error_message(source.error()))));
            }
            const std::string chunkName = "@" + path.string();
            const auto valid = state.scripts->validate_string(*source, chunkName);
            if (!valid)
            {
                (void)state.scripts->shutdown();
                state.scripts.reset();
                return std::unexpected(runtimeError("runtime.script.compile", path, valid.error()));
            }
            scriptSources.emplace(authored.id, std::move(*source));
        }
        std::vector<std::pair<std::string, std::string>> declarations;
        for (const SceneEntity& authored : state.project.scene.entities)
            if (authored.script)
                declarations.emplace_back(scriptSources.at(authored.id),
                    "@" + resolvedScripts.at(authored.id).string());
        const auto checked = LuaScriptSystem::validate_declarations(declarations, state.project.root);
        if (!checked)
        {
            (void)state.scripts->shutdown();
            state.scripts.reset();
            return std::unexpected(runtimeError("runtime.script.declaration", state.project.scene.source,
                checked.error()));
        }
        auto& ecs = state.simulator.ecs();
        try
        {
            state.audioOwner = state.project.scene.id + "@" + std::to_string(++state.handles->scope);
            if (!state.preparing && state.options.persistence)
            {
                const auto settings = state.options.persistence->loadSettings();
                if (settings)
                { const auto applied = state.applySettings(*settings, false); if (!applied) state.makeApi().log("Settings rejected: " + applied.error()); }
                else if (settings.error().front().code != "persistence.missing") state.makeApi().log("Settings unavailable: " + settings.error().front().message);
            }
            for (const auto& [id, asset] : state.project.assets)
            {
                if (asset.kind != "audio") continue;
                const auto path = resolveAsset(state.project, id); if (!path) throw std::runtime_error(path.error().front().message);
                const auto clip = audio::loadWav(*path); if (!clip) throw std::runtime_error(clip.error().message);
                state.clips.emplace(id, *clip);
            }
            for (const SceneEntity& authored : state.project.scene.entities)
            {
                const Entity entity = ecs.createEntity();
                const auto id = state.expose(entity);
                state.entities.emplace(authored.id, id);
                if (authored.spriteRenderer) state.spriteState.emplace(authored.id, *authored.spriteRenderer);
                if (authored.transform)
                {
                    const auto& p = authored.transform->position;
                    ecs.emplaceComponent<Position3D>(entity, Vector3f{p[0], p[1], p[2]});
                    state.positions[id] = p;
                }
            }
            state.synchronizeWorld();

            for (const SceneEntity& authored : state.project.scene.entities)
            {
                if (!authored.script) continue;
                const auto owner = state.entities.at(authored.id);
                const auto& path = resolvedScripts.at(authored.id);
                const std::string chunkName = "@" + path.string();
                auto loaded = state.scripts->load_string(scriptSources.at(authored.id), state.context(owner), chunkName);
                if (!loaded)
                    throw std::runtime_error("entity '" + authored.id + "' script failed: " + loaded.error());
                state.scriptInstances.emplace(authored.id, *loaded);
            }
            state.started = true;
            state.faulted = false;
            state.ticks = 0;
            state.sceneTicks = 0;
            return {};
        }
        catch (const std::exception& error)
        {
            if (state.scripts) (void)state.scripts->shutdown();
            state.scripts.reset();
            state.scriptInstances.clear();
            state.destroyEntities();
            state.ui.clear(); state.clips.clear(); if (state.options.audio) state.options.audio->stopOwner(state.audioOwner);
            state.simulator.ecs().pruneEmptyDynamicComponentSchemas();
            return std::unexpected(runtimeError("runtime.start.failed", state.project.scene.source, error.what()));
        }
    }

    Result<void> Runtime::tick(const InputSnapshot& input)
    {
        auto& state = *m_impl;
        if (!state.onOwnerThread())
            return std::unexpected(runtimeError("runtime.thread.owner", state.project.scene.source,
                "Runtime must be ticked on its owning thread"));
        if (!state.started) return std::unexpected(runtimeError("runtime.not_started", state.project.scene.source, "Runtime has not started"));
        if (state.faulted) return std::unexpected(runtimeError("runtime.faulted", state.project.scene.source,
            "Runtime is faulted; stop it before further simulation ticks"));
        for (const auto* actions : {&input.pressed, &input.held, &input.released})
            for (const auto& action : *actions)
                if (!state.options.allowedActions.contains(action))
                    return std::unexpected(runtimeError("runtime.input.action.unknown", state.project.scene.source,
                        "Input snapshot contains undeclared action: " + action));
        if (!std::isfinite(input.pointerX) || !std::isfinite(input.pointerY) || !std::isfinite(input.wheelX) || !std::isfinite(input.wheelY))
            return std::unexpected(runtimeError("runtime.input.finite", state.project.scene.source, "Pointer and wheel values must be finite"));
        for (const auto& [action, value] : input.values)
            if (!state.options.allowedActions.contains(action) || !std::isfinite(value) || value < -1 || value > 1)
                return std::unexpected(runtimeError("runtime.input.value", state.project.scene.source, "Analog input values require declared actions and finite values in [-1,1]"));
        while (!state.pendingReloads.empty())
        {
            auto pending = std::move(state.pendingReloads.front());
            state.pendingReloads.pop_front();
            const auto owner = state.entities.find(pending.entityId);
            const auto active = state.scriptInstances.find(pending.entityId);
            if (owner == state.entities.end() || active == state.scriptInstances.end())
                return std::unexpected(runtimeError("runtime.script.reload.target", state.project.scene.source,
                    "Reload target does not have an active entity-owned script: " + pending.entityId));
            auto candidate = state.scripts->load_string(pending.source, state.context(owner->second), pending.chunkName);
            if (!candidate)
            {
                state.fault();
                return std::unexpected(runtimeError("runtime.script.reload.invalid", pending.chunkName, candidate.error()));
            }
            const auto retired = state.scripts->unloadRetainingOnDestroyFailure(active->second);
            if (!retired)
            {
                const auto discarded = state.scripts->discardCandidate(*candidate);
                state.fault();
                std::string message = "current script teardown failed; previous instance remains active: " + retired.error();
                if (!discarded) message += "; candidate cleanup reported: " + discarded.error();
                return std::unexpected(runtimeError("runtime.script.reload.teardown", pending.chunkName, std::move(message)));
            }
            active->second = *candidate;
        }
        struct InputScope
        {
            Impl& owner;
            bool active = true;
            explicit InputScope(Impl& state, const InputSnapshot& snapshot) : owner(state) { owner.input = &snapshot; }
            ~InputScope() { if (active) owner.input = nullptr; }
        } inputScope(state, input);
        interaction::ActionFrame actionFrame;
        actionFrame.pressed = input.pressed; actionFrame.held = input.held; actionFrame.released = input.released; actionFrame.values = input.values;
        actionFrame.pointerX = input.pointerX; actionFrame.pointerY = input.pointerY; actionFrame.wheelX = input.wheelX; actionFrame.wheelY = input.wheelY; actionFrame.focused = input.focused;
        state.ui.tick(actionFrame);
        auto& ecs = state.simulator.ecs();
        ecs.beginStructuralDeferral();
        LuaScriptSystem::Result updated;
        try
        {
            updated = state.scripts->update(state.options.fixedDeltaSeconds);
        }
        catch (const std::exception& error)
        {
            ecs.endStructuralDeferral();
            ecs.discardDeferredStructuralChanges();
            state.fault();
            return std::unexpected(runtimeError("runtime.script.update", state.project.scene.source, error.what()));
        }
        catch (...)
        {
            ecs.endStructuralDeferral();
            ecs.discardDeferredStructuralChanges();
            state.fault();
            return std::unexpected(runtimeError("runtime.script.update", state.project.scene.source,
                "Unknown exception while running Lua callbacks"));
        }
        ecs.endStructuralDeferral();
        if (!updated)
        {
            ecs.discardDeferredStructuralChanges();
            state.fault();
            return std::unexpected(runtimeError("runtime.script.update", state.project.scene.source, updated.error()));
        }
        try
        {
            ecs.flushDeferredStructuralChanges();
        }
        catch (const std::exception& error)
        {
            state.fault();
            return std::unexpected(runtimeError("runtime.ecs.commit", state.project.scene.source, error.what()));
        }
        try { state.simulator.simulate(); state.synchronizeWorld(); state.triggerEvents = state.world.advanceTriggers(); }
        catch (const std::exception& error) { state.fault(); return std::unexpected(runtimeError("runtime.simulation", state.project.scene.source, error.what())); }
        ++state.ticks;
        ++state.sceneTicks;
        if (state.pendingScene)
        {
            const auto request = std::move(*state.pendingScene); state.pendingScene.reset();
            const auto scenePath = resolveAsset(state.project, request.asset);
            if (!scenePath) return std::unexpected(scenePath.error());
            const auto scene = loadScene(*scenePath, state.project);
            if (!scene) return std::unexpected(scene.error());
            Project nextProject = state.project; nextProject.scene = *scene;
            if (!request.spawn.empty())
            {
                const auto spawn = std::find_if(nextProject.scene.entities.begin(), nextProject.scene.entities.end(), [&](const auto& item) { return item.id == request.spawn && item.transform; });
                if (spawn == nextProject.scene.entities.end()) return std::unexpected(runtimeError("runtime.scene.spawn", *scenePath, "Scene spawn must reference a positioned target entity"));
                if (!request.traveller.empty())
                {
                    const auto traveller = std::find_if(nextProject.scene.entities.begin(), nextProject.scene.entities.end(), [&](const auto& item) { return item.id == request.traveller && item.transform; });
                    if (traveller == nextProject.scene.entities.end()) return std::unexpected(runtimeError("runtime.scene.traveller", *scenePath, "Traveller must reference a positioned target entity"));
                    traveller->transform->position = spawn->transform->position;
                }
            }
            Runtime candidate(std::move(nextProject), state.options);
            candidate.m_impl->handles = state.handles; candidate.m_impl->preparing = true;
            candidate.m_impl->sharedState = state.sharedState;
            candidate.m_impl->ui.tick(actionFrame); candidate.m_impl->ui.clear();
            const auto prepared = candidate.start();
            if (!prepared) return std::unexpected(prepared.error());
            candidate.m_impl->synchronizeWorld();
            if (!request.traveller.empty()) if (const auto body = candidate.m_impl->world.body(request.traveller))
            {
                const auto safe = candidate.m_impl->world.isSafe(*body, request.traveller);
                if (!safe || !*safe) return std::unexpected(runtimeError("runtime.scene.spawn.blocked", *scenePath, "Target traveller placement overlaps a solid collider"));
            }
            candidate.m_impl->revision = state.revision + 1; candidate.m_impl->ticks = state.ticks;
            // Retain the old state until candidate initialization and validation succeed.
            const auto retired = stop();
            if (!retired) return retired;
            inputScope.active = false;
            candidate.m_impl->preparing = false;
            m_impl.swap(candidate.m_impl);
        }
        return {};
    }

    Result<void> Runtime::stageScriptReload(std::string entityId, std::string source, std::string chunkName)
    {
        auto& state = *m_impl;
        if (!state.onOwnerThread())
            return std::unexpected(runtimeError("runtime.thread.owner", state.project.scene.source,
                "Script reloads must be staged on the runtime owning thread"));
        if (!state.started || state.faulted)
            return std::unexpected(runtimeError(state.faulted ? "runtime.faulted" : "runtime.not_started",
                state.project.scene.source, state.faulted ? "Stop the faulted runtime before staging reloads" : "Runtime has not started"));
        if (!state.scriptInstances.contains(entityId))
            return std::unexpected(runtimeError("runtime.script.reload.target", state.project.scene.source,
                "Reload target does not have an active entity-owned script: " + entityId));
        state.pendingReloads.push_back({std::move(entityId), std::move(source), std::move(chunkName)});
        return {};
    }

    Result<void> Runtime::runHeadless(const std::uint32_t ticks)
    {
        if (!m_impl->onOwnerThread())
            return std::unexpected(runtimeError("runtime.thread.owner", m_impl->project.scene.source,
                "Headless runtime must run on its owning thread"));
        if (ticks == 0)
            return std::unexpected(runtimeError("runtime.ticks.invalid", m_impl->project.scene.source, "Headless tick count must be positive"));
        if (!running())
        {
            auto started = start();
            if (!started) return started;
        }
        for (std::uint32_t i = 0; i < ticks; ++i)
        {
            auto result = tick();
            if (!result) return result;
        }
        return {};
    }

    Result<void> Runtime::stop()
    {
        if (!m_impl) return {};
        auto& state = *m_impl;
        if (!state.onOwnerThread())
            return std::unexpected(runtimeError("runtime.thread.owner", state.project.scene.source,
                "Runtime must be stopped on its owning thread"));
        std::string scriptError;
        if (state.scripts)
        {
            const auto closed = state.scripts->shutdown();
            if (!closed) scriptError = closed.error();
            state.scripts.reset();
        }
        state.scriptInstances.clear();
        state.pendingReloads.clear();
        state.destroyEntities();
        state.ui.clear(); state.pendingScene.reset(); state.clips.clear(); if (state.options.audio) state.options.audio->stopOwner(state.audioOwner);
        state.simulator.ecs().pruneEmptyDynamicComponentSchemas();
        state.started = false;
        state.faulted = false;
        state.input = nullptr;
        if (!scriptError.empty())
            return std::unexpected(runtimeError("runtime.script.stop", state.project.scene.source, std::move(scriptError)));
        return {};
    }

    bool Runtime::running() const noexcept { return m_impl && m_impl->onOwnerThread() && m_impl->started && !m_impl->faulted; }
    std::uint64_t Runtime::tickCount() const noexcept { return m_impl && m_impl->onOwnerThread() ? m_impl->ticks : 0; }
    std::size_t Runtime::liveEntityCount() const noexcept
    {
        if (!m_impl || !m_impl->onOwnerThread()) return 0;
        const auto& ecs = m_impl->simulator.ecs();
        std::size_t count = 0;
        for (const auto& [id, packed] : m_impl->entities)
        {
            (void)id;
            if (packed >= 0 && ecs.hasEntity(m_impl->find(packed))) ++count;
        }
        for (const auto packed : m_impl->spawnedEntities)
            if (packed >= 0 && ecs.hasEntity(m_impl->find(packed))) ++count;
        return count;
    }

    std::size_t Runtime::activeScriptCount() const noexcept
    {
        return m_impl && m_impl->onOwnerThread() ? m_impl->scriptInstances.size() : 0;
    }

    std::optional<std::array<float, 3>> Runtime::position(const std::string_view authoredId) const
    {
        if (!m_impl || !m_impl->onOwnerThread()) return std::nullopt;
        const auto found = m_impl->entities.find(std::string(authoredId));
        if (found == m_impl->entities.end() || found->second < 0) return std::nullopt;
        const Entity entity = m_impl->find(found->second);
        if (!m_impl->simulator.ecs().hasEntity(entity)) return std::nullopt;
        const auto value = m_impl->positions.find(found->second);
        return value == m_impl->positions.end() ? std::nullopt : std::optional(value->second);
    }

    const std::map<std::string, std::int64_t>& Runtime::runtimeEntities() const
    {
        static const std::map<std::string, std::int64_t> empty;
        return m_impl && m_impl->onOwnerThread() ? m_impl->entities : empty;
    }

    const Scene& Runtime::activeScene() const { return m_impl->project.scene; }
    std::uint64_t Runtime::sceneRevision() const noexcept { return m_impl ? m_impl->revision : 0; }
    std::optional<std::int64_t> Runtime::entityHandle(std::string_view id) const
    {
        if (!m_impl || !m_impl->onOwnerThread()) return std::nullopt;
        const auto found = m_impl->entities.find(std::string(id));
        if (found == m_impl->entities.end() || !m_impl->simulator.ecs().hasEntity(m_impl->find(found->second))) return std::nullopt;
        return found->second;
    }
    std::vector<SpriteSnapshot> Runtime::sprites() const
    {
        std::vector<SpriteSnapshot> result;
        if (!m_impl || !m_impl->onOwnerThread()) return result;
        for (const auto& [id, sprite] : m_impl->spriteState)
        {
            const auto p = position(id); if (!p || !sprite.visible) continue;
            const auto forced = m_impl->forcedFrames.find(id);
            const auto frame = forced != m_impl->forcedFrames.end() ? forced->second :
                sprite.frames.empty() || sprite.framesPerSecond == 0 ? 0 : rendering::animationFrame(
                    double(m_impl->sceneTicks) * m_impl->options.fixedDeltaSeconds, sprite.framesPerSecond, sprite.frames.size(), sprite.loop);
            result.push_back({id, *p, sprite, frame});
        }
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b)
        { return a.sprite.layer != b.sprite.layer ? a.sprite.layer < b.sprite.layer : a.id < b.id; });
        return result;
    }
    std::optional<Camera2D> Runtime::camera() const
    {
        if (!m_impl || !m_impl->onOwnerThread()) return std::nullopt;
        for (const auto& entity : m_impl->project.scene.entities) if (entity.camera2D)
        {
            auto camera = *entity.camera2D;
            if (m_impl->cameraCenter) camera.center = *m_impl->cameraCenter;
            else if (camera.follow) if (const auto p = position(*camera.follow)) camera.center = {(*p)[0], (*p)[1]};
            return camera;
        }
        return std::nullopt;
    }
    const std::vector<ui::PanelSnapshot>& Runtime::panels() const
    { return m_impl->ui.panels(); }
    const std::vector<gameplay2d::TriggerEvent>& Runtime::triggers() const
    { return m_impl->triggerEvents; }
    Result<void> Runtime::requestScene(std::string asset, std::string spawn, std::string traveller)
    {
        if (!m_impl || !m_impl->onOwnerThread()) return std::unexpected(runtimeError("runtime.thread.owner", {}, "Scene requests require the runtime owner thread"));
        if (!running()) return std::unexpected(runtimeError("runtime.not_started", m_impl->project.scene.source, "Scene requests require an active runtime"));
        const auto result = m_impl->makeApi().change_scene(asset, spawn, traveller);
        if (!result) return std::unexpected(runtimeError("runtime.scene.request", m_impl->project.scene.source, result.error()));
        return {};
    }
}
