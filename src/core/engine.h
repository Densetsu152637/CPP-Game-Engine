//
// Created by Nicholas on 26/04/26.
//

#pragma once
#include <atomic>
#include <cstdint>
#include <memory>

#include "interfaces.h"
#include "../async/lock.h"
#include "../ecs/ecs.h"
#include "../rendering/render_frame.h"
#include "../structs/arraylist.h"
#include "../logging/logger.h"

class LuaScriptSystem;

class Engine
{

    ArrayList<IPollable*> m_pollables;
    ArrayList<std::function<void()>> m_startupListeners;
    ArrayList<std::function<void()>> m_shutdownListeners;

    ECSProcessor* m_simulator = nullptr;
    std::unique_ptr<LuaScriptSystem> m_scripts;
    IDisplayManager* m_display = nullptr;
    rendering::IRenderFrameCoordinator* m_renderFrameCoordinator = nullptr;
    Logger* m_logger = nullptr;
    Waiter m_finisher;
    std::atomic<bool> m_running = false;
    std::atomic<int> UPS = 60;
    std::atomic<int> FPS = 60;
    std::atomic<uint64_t> m_logicTickLimit = 0;
    std::atomic<uint64_t> m_logicTickCount = 0;

public:

    ~Engine();
    Engine();
    Engine(const Engine& e) = delete; // do not move this struct please :D
    Engine(Engine&&) = delete;

    Engine& operator=(const Engine&) = delete;
    Engine& operator=(Engine&&) = delete;

    // Run on the display/renderer owning thread. Logic and render phases are serialized.
    void run();
    void awaitTermination();
    void finishExecutionOnCond(bool cond);
    void finishExecution();
    void logicAction();
    void renderAction() const;

    Engine& setSimulator(ECSProcessor* sim);
    Engine& enableScripting();
    LuaScriptSystem* scripts();
    Engine& setLogger(Logger* logger);
    Engine& setDisplay(IDisplayManager* display);
    Engine& setRenderFrameCoordinator(rendering::IRenderFrameCoordinator* coordinator);
    Engine& addEventListener(WindowEventListener* l);
    Engine& addPollable(IPollable* p);
    Engine& removePollable(IPollable* p);
    Engine& addStartupListener(const std::function<void()>& fn);
    Engine& addShutdownListener(const std::function<void()>& fn);

    Engine& setUPS(int val);
    Engine& setFPS(int val);
    Engine& syncUPSFPS(int val);
    Engine& setLogicTickLimit(uint64_t ticks);
    int upsDur() const;
    int fpsDur() const;
    float upsMs() const;
    float fpsMs() const;

};
