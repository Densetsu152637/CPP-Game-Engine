#include "runtime.h"

#include "../async/threadpool.h"
#include "../components/alias.h"
#include "../ecs/processor.h"
#include "../scripting/lua_script_system.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <fstream>
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
        std::unordered_set<std::int64_t> spawnedEntities;
        std::map<std::string, LuaScriptId> scriptInstances;
        struct PendingReload { std::string entityId; std::string source; std::string chunkName; };
        std::deque<PendingReload> pendingReloads;
        const InputSnapshot* input = nullptr;
        std::uint64_t ticks = 0;
        bool started = false;
        bool faulted = false;

        bool onOwnerThread() const noexcept { return ownerThread == std::this_thread::get_id(); }

        Impl(Project value, RuntimeOptions config) : project(std::move(value)), options(config) {}

        Entity find(std::int64_t packed) const
        {
            return packed < 0 ? Entity{} : Entity::fromPacked(static_cast<std::uint64_t>(packed));
        }

        EngineScriptApi makeApi()
        {
            EngineScriptApi api;
            api.log = [this](std::string_view message)
            {
                if (options.log) options.log(message);
                else std::clog << "[project script] " << message << '\n';
            };
            api.redirect_standard_output = true;
            api.create_entity = [this]() -> std::int64_t
            {
                const auto packed = simulator.ecs().createEntity().packed();
                if (packed > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return -1;
                const auto id = static_cast<std::int64_t>(packed);
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
                if (packed < 0) return false;
                auto& ecs = simulator.ecs();
                const Entity entity = find(packed);
                if (!ecs.knowsEntityHandle(entity)) return false;
                ecs.setComponent<Position3D>(entity, Vector3f{x, y, z});
                return true;
            };
            api.get_position = [this](std::int64_t packed) -> std::optional<std::array<float, 3>>
            {
                if (packed < 0) return std::nullopt;
                const auto* value = simulator.ecs().try_get<Position3D>(find(packed));
                if (!value) return std::nullopt;
                const auto& p = value->read();
                return std::array<float, 3>{p.x, p.y, p.z};
            };
            api.register_component = [this](std::string_view name, const std::vector<LuaComponentField>& fields)
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
                    schema.push_back({field.name, type});
                }
                std::sort(schema.begin(), schema.end(), [](const auto& left, const auto& right)
                { return left.name < right.name; });
                return simulator.ecs().registerDynamicComponent(name, schema);
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
                std::vector<std::int64_t> result;
                for (const Entity& entity : simulator.ecs().queryDynamicComponents(all))
                {
                    const auto packed = entity.packed();
                    if (packed <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                        result.push_back(static_cast<std::int64_t>(packed));
                }
                return result;
            };
            return api;
        }

        LuaScriptContext context(std::int64_t owner)
        {
            LuaScriptContext result;
            result.owner = owner;
            const auto has = [this](const std::set<std::string> InputSnapshot::*field, std::string_view action)
            {
                return input && (input->*field).contains(std::string(action));
            };
            result.pressed = [has](std::string_view action) { return has(&InputSnapshot::pressed, action); };
            result.held = [has](std::string_view action) { return has(&InputSnapshot::held, action); };
            result.released = [has](std::string_view action) { return has(&InputSnapshot::released, action); };
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
        if (!m_impl->project.inputActions.empty())
        {
            m_impl->options.allowedActions.clear();
            for (const auto& [action, key] : m_impl->project.inputActions)
            {
                (void)key;
                m_impl->options.allowedActions.insert(action);
            }
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
            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                (void)state.scripts->shutdown();
                state.scripts.reset();
                return std::unexpected(runtimeError("runtime.script.read", path, "Unable to open referenced Lua script"));
            }
            std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            if (file.bad())
            {
                (void)state.scripts->shutdown();
                state.scripts.reset();
                return std::unexpected(runtimeError("runtime.script.read", path, "Unable to read referenced Lua script"));
            }
            const std::string chunkName = "@" + path.string();
            const auto valid = state.scripts->validate_string(source, chunkName);
            if (!valid)
            {
                (void)state.scripts->shutdown();
                state.scripts.reset();
                return std::unexpected(runtimeError("runtime.script.compile", path, valid.error()));
            }
            scriptSources.emplace(authored.id, std::move(source));
        }
        auto& ecs = state.simulator.ecs();
        try
        {
            for (const SceneEntity& authored : state.project.scene.entities)
            {
                const Entity entity = ecs.createEntity();
                const auto packed = entity.packed();
                if (packed > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                    throw std::runtime_error("runtime entity handle cannot be represented by Lua bridge");
                const auto id = static_cast<std::int64_t>(packed);
                state.entities.emplace(authored.id, id);
                if (authored.transform)
                {
                    const auto& p = authored.transform->position;
                    ecs.emplaceComponent<Position3D>(entity, Vector3f{p[0], p[1], p[2]});
                }
            }

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
            return {};
        }
        catch (const std::exception& error)
        {
            if (state.scripts) (void)state.scripts->shutdown();
            state.scripts.reset();
            state.scriptInstances.clear();
            state.destroyEntities();
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
                state.faulted = true;
                return std::unexpected(runtimeError("runtime.script.reload.invalid", pending.chunkName, candidate.error()));
            }
            const auto retired = state.scripts->unloadRetainingOnDestroyFailure(active->second);
            if (!retired)
            {
                const auto discarded = state.scripts->discardCandidate(*candidate);
                state.faulted = true;
                std::string message = "current script teardown failed; previous instance remains active: " + retired.error();
                if (!discarded) message += "; candidate cleanup reported: " + discarded.error();
                return std::unexpected(runtimeError("runtime.script.reload.teardown", pending.chunkName, std::move(message)));
            }
            active->second = *candidate;
        }
        struct InputScope
        {
            Impl& owner;
            explicit InputScope(Impl& state, const InputSnapshot& snapshot) : owner(state) { owner.input = &snapshot; }
            ~InputScope() { owner.input = nullptr; }
        } inputScope(state, input);
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
            state.faulted = true;
            return std::unexpected(runtimeError("runtime.script.update", state.project.scene.source, error.what()));
        }
        catch (...)
        {
            ecs.endStructuralDeferral();
            ecs.discardDeferredStructuralChanges();
            state.faulted = true;
            return std::unexpected(runtimeError("runtime.script.update", state.project.scene.source,
                "Unknown exception while running Lua callbacks"));
        }
        ecs.endStructuralDeferral();
        if (!updated)
        {
            ecs.discardDeferredStructuralChanges();
            state.faulted = true;
            return std::unexpected(runtimeError("runtime.script.update", state.project.scene.source, updated.error()));
        }
        try
        {
            ecs.flushDeferredStructuralChanges();
        }
        catch (const std::exception& error)
        {
            state.faulted = true;
            return std::unexpected(runtimeError("runtime.ecs.commit", state.project.scene.source, error.what()));
        }
        state.simulator.simulate();
        ++state.ticks;
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
            if (packed >= 0 && ecs.hasEntity(Entity::fromPacked(static_cast<std::uint64_t>(packed)))) ++count;
        }
        for (const auto packed : m_impl->spawnedEntities)
            if (packed >= 0 && ecs.hasEntity(Entity::fromPacked(static_cast<std::uint64_t>(packed)))) ++count;
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
        const Entity entity = Entity::fromPacked(static_cast<std::uint64_t>(found->second));
        const auto* value = m_impl->simulator.ecs().try_get<Position3D>(entity);
        if (!value) return std::nullopt;
        const auto& p = value->read();
        return std::array<float, 3>{p.x, p.y, p.z};
    }

    const std::map<std::string, std::int64_t>& Runtime::runtimeEntities() const
    {
        static const std::map<std::string, std::int64_t> empty;
        return m_impl && m_impl->onOwnerThread() ? m_impl->entities : empty;
    }
}
