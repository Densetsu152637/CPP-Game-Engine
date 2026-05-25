//
// Created by Nicholas on 26/04/26.
//

#include "engine.h"

#include <chrono>
#include <thread>

void enginePeriodicFunction(
    Engine* e,
    void (*fnPtr)(Engine*),
    int (*intervalPtr)(Engine*),
    const std::atomic<bool>* running
)
{
    if (!e || !fnPtr || !intervalPtr || !running) return;

    using clock = std::chrono::steady_clock;
    auto next = clock::now();

    while (running->load(std::memory_order_acquire))
    {
        auto interval = intervalPtr(e);
        next += std::chrono::nanoseconds(interval);
        fnPtr(e);

        const auto now = clock::now();
        if (now < next)
        {
            std::this_thread::sleep_until(next);
        }
        else
        {
            // If the work took longer than the interval, restart timing from now.
            next = now;
        }
    }
}

void Engine::run()
{
    for (auto& startupListener : m_startupListeners)
    {
        startupListener();
    }

    void (*logicAction)(Engine*) = [](Engine* e)
    {
        e->logicAction();
    };

    void (*renderAction)(Engine*) = [](Engine* e)
    {
        e->renderAction();
    };

    int (*logicTiming)(Engine*) = [](Engine* e)
    {
        return e->upsDur();
    };

    int (*renderTiming)(Engine*) = [](Engine* e)
    {
        return e->fpsDur();
    };

    m_running = true;

    std::thread logicThread(enginePeriodicFunction, this, logicAction, logicTiming, &m_running);
    std::thread renderThread(enginePeriodicFunction, this, renderAction, renderTiming, &m_running);

    awaitTermination();
    m_running = false;

    logicThread.join();
    renderThread.join();

    for (auto& shutdownListener : m_shutdownListeners)
    {
        shutdownListener();
    }
}

void Engine::finishExecutionOnCond(const bool cond)
{
    if (cond)
        m_finisher.signal();
}

void Engine::finishExecution()
{
    finishExecutionOnCond(true);
}

void Engine::awaitTermination()
{
    m_finisher.await();
}

void Engine::logicAction() const
{
    if (m_simulator)
        m_simulator->simulate();
}

void Engine::renderAction() const
{
    // do something here I guess
}

Engine& Engine::setSimulator(ECSSimulator* sim)
{
    if (sim) m_simulator = sim;
    return *this;
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

Engine& Engine::addShutdownListener(const std::function<void()>& fn)
{
    m_shutdownListeners.append(fn);
    return *this;
}

Engine& Engine::setUPS(const int val)
{
    UPS = val;
    return *this;
}

Engine& Engine::setFPS(const int val)
{
    FPS = val;
    return *this;
}

Engine& Engine::syncUPSFPS(const int val)
{
    setUPS(val);
    setFPS(val);
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

