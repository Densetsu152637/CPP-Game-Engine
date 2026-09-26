//
// Created by Nicholas on 26/04/26.
//

#include "engine.h"

#include <chrono>
#include <limits>
#include <string>
#include <stdexcept>
#include <thread>

#include "../components/alias.h"
#include "../ecs/processor.h"
#include "../scripting/lua_script_system.h"

void Engine::run()
{
    // Lua mutation and renderer traversal share one owner thread. ECS jobs may
    // still execute in their pools, but each phase waits before the next starts.
    using clock = std::chrono::steady_clock;
    m_finisher.reset_signal();
    m_logicTickCount = 0;
    m_running = true;
    std::exception_ptr failure;
    try
    {
        for (auto& listener : m_startupListeners) listener();
        auto nextLogic = clock::now();
        auto nextRender = nextLogic;
        while (m_running.load(std::memory_order_acquire))
        {
            for (auto* pollable : m_pollables) pollable->poll();
            if (m_display && m_display->isClosed()) finishExecution();
            if (!m_running.load(std::memory_order_acquire)) break;
            const auto now = clock::now();
            if (now >= nextLogic)
            {
                logicAction();
                nextLogic = clock::now() + std::chrono::nanoseconds(upsDur());
            }
            if (m_running.load(std::memory_order_acquire) && now >= nextRender)
            {
                renderAction();
                nextRender = clock::now() + std::chrono::nanoseconds(fpsDur());
            }
            if (m_running.load(std::memory_order_acquire))
                std::this_thread::sleep_until(std::min(nextLogic, nextRender));
        }
    }
    catch (...) { failure = std::current_exception(); }
    m_running = false;
    try { if (m_renderFrameCoordinator) m_renderFrameCoordinator->waitIdle(); }
    catch (...) { if (!failure) failure = std::current_exception(); }
    try
    {
        if (m_scripts)
        {
            const auto result = m_scripts->shutdown();
            if (!result && m_logger) m_logger->log("Lua shutdown: " + result.error());
        }
    }
    catch (...) { if (!failure) failure = std::current_exception(); }
    for (auto& listener : m_shutdownListeners)
    {
        try { listener(); }
        catch (...) { if (!failure) failure = std::current_exception(); }
    }
    m_finisher.signal();
    if (failure) std::rethrow_exception(failure);
}
void Engine::finishExecutionOnCond(const bool cond)
{
    if (cond) m_running.store(false, std::memory_order_release);
}

void Engine::finishExecution()
{
    finishExecutionOnCond(true);
}

void Engine::awaitTermination()
{
    m_finisher.await();
}

void Engine::logicAction()
{
    if (m_scripts)
    {
        constexpr float nanosecondsPerSecond = 1'000'000'000.0f;
        const auto result = m_scripts->update(static_cast<float>(upsDur()) / nanosecondsPerSecond);
        if (!result && m_logger)
            m_logger->log("Lua update: " + result.error());
    }

    if (m_simulator)
        m_simulator->simulate();

    const uint64_t tick = ++m_logicTickCount;
    const uint64_t limit = m_logicTickLimit.load(std::memory_order_relaxed);
    if (limit != 0 && tick >= limit)
        finishExecution();
}

void Engine::renderAction() const
{
    bool frameStarted = false;
    if (m_renderFrameCoordinator)
    {
        frameStarted = m_renderFrameCoordinator->beginRenderFrame();
        if (!frameStarted)
            return;
    }

    try
    {
        if (m_simulator)
            m_simulator->render();
    }
    catch (...)
    {
        if (frameStarted)
            m_renderFrameCoordinator->cancelRenderFrame();
        throw;
    }

    if (frameStarted)
        m_renderFrameCoordinator->endRenderFrame();
}

Engine::Engine() = default;
Engine::~Engine() = default;

Engine& Engine::setSimulator(ECSProcessor* sim)
{
    if (sim) m_simulator = sim;
    return *this;
}

Engine& Engine::enableScripting()
{
    if (m_scripts)
        return *this;

    EngineScriptApi api;
    api.log = [this](const std::string_view message)
    {
        if (m_logger)
            m_logger->log(std::string(message));
    };
    api.create_entity = [this]() -> int64_t
    {
        if (!m_simulator)
            return -1;
        const uint64_t packed = m_simulator->ecs().createEntity().packed();
        return packed <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())
            ? static_cast<int64_t>(packed)
            : -1;
    };
    api.is_entity_alive = [this](const int64_t id)
    {
        return m_simulator && id >= 0 && m_simulator->ecs().hasEntity(Entity::fromPacked(static_cast<uint64_t>(id)));
    };
    api.destroy_entity = [this](const int64_t id)
    {
        if (!m_simulator || id < 0)
            return false;
        ECS& ecs = m_simulator->ecs();
        const Entity entity = Entity::fromPacked(static_cast<uint64_t>(id));
        if (!ecs.hasEntity(entity))
            return false;
        ecs.destroyEntity(entity);
        return true;
    };
    api.set_position = [this](const int64_t id, const float x, const float y, const float z)
    {
        if (!m_simulator || id < 0)
            return false;
        ECS& ecs = m_simulator->ecs();
        const Entity entity = Entity::fromPacked(static_cast<uint64_t>(id));
        if (!ecs.hasEntity(entity))
            return false;
        ecs.setComponent<Position3D>(entity, Vector3f{x, y, z});
        return true;
    };
    api.get_position = [this](const int64_t id) -> std::optional<std::array<float, 3>>
    {
        if (!m_simulator || id < 0)
            return std::nullopt;
        const Entity entity = Entity::fromPacked(static_cast<uint64_t>(id));
        const Position3D* position = m_simulator->ecs().try_get<Position3D>(entity);
        if (!position)
            return std::nullopt;
        const Vector3f& value = position->read();
        return std::array<float, 3>{value.x, value.y, value.z};
    };
    m_scripts = std::make_unique<LuaScriptSystem>(std::move(api));
    return *this;
}

LuaScriptSystem* Engine::scripts()
{
    return m_scripts.get();
}

Engine& Engine::setLogger(Logger* logger)
{
    if (logger) m_logger = logger;
    return *this;
}

Engine& Engine::setDisplay(IDisplayManager* display)
{
    if (display) m_display = display;
    return *this;
}

Engine& Engine::setRenderFrameCoordinator(rendering::IRenderFrameCoordinator* coordinator)
{
    if (coordinator) m_renderFrameCoordinator = coordinator;
    return *this;
}

Engine& Engine::addEventListener(WindowEventListener* l)
{
    if (!m_display) return *this;
    m_display->getWindowEventManager().addListener(l);
    return *this;
}

Engine& Engine::addPollable(IPollable* p)
{
    if (!p) return *this;
    m_pollables.append(p);
    return *this;
}

Engine& Engine::addStartupListener(const std::function<void()>& fn)
{
    m_startupListeners.append(fn);
    return *this;
}

Engine& Engine::removePollable(IPollable* p)
{
    m_pollables.remove(p);
    return *this;
}

Engine& Engine::addShutdownListener(const std::function<void()>& fn)
{
    m_shutdownListeners.append(fn);
    return *this;
}

Engine& Engine::setUPS(const int val)
{
    if (val <= 0 || val > 1'000'000'000)
        throw std::invalid_argument("UPS must be between 1 and 1000000000");
    UPS = val;
    return *this;
}

Engine& Engine::setFPS(const int val)
{
    if (val <= 0 || val > 1'000'000'000)
        throw std::invalid_argument("FPS must be between 1 and 1000000000");
    FPS = val;
    return *this;
}

Engine& Engine::syncUPSFPS(const int val)
{
    setUPS(val);
    setFPS(val);
    return *this;
}

Engine& Engine::setLogicTickLimit(const uint64_t ticks)
{
    m_logicTickCount.store(0, std::memory_order_relaxed);
    m_logicTickLimit.store(ticks, std::memory_order_relaxed);
    return *this;
}

int Engine::upsDur() const
{
    return 1000000000 / UPS.load(std::memory_order_acquire);
}

int Engine::fpsDur() const
{
    return 1000000000 / FPS.load(std::memory_order_acquire);
}

float Engine::upsMs() const
{
    return 1000.0f / static_cast<float>(UPS.load(std::memory_order_acquire));
};
float Engine::fpsMs() const
{
    return 1000.0f / static_cast<float>(FPS.load(std::memory_order_acquire));
};
