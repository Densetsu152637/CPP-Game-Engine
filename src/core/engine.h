//
// Created by Nicholas on 26/04/26.
//

#pragma once
#include <atomic>

#include "interfaces.h"
#include "../async/lock.h"
#include "../ecs/ecs.h"
#include "../structs/arraylist.h"
#include "../logging/logger.h"

class Engine
{

    ArrayList<IPollable*> m_pollables;
    ArrayList<std::function<void()>> m_startupListeners;
    ArrayList<std::function<void()>> m_shutdownListeners;

    ECSProcessor* m_simulator = nullptr;
    IDisplayManager* m_display = nullptr;
    Logger* m_logger = nullptr;
    Waiter m_finisher;
    std::atomic<bool> m_running = false;
    std::atomic<int> UPS = 60;
    std::atomic<int> FPS = 60;

public:

    ~Engine() = default;
    Engine() = default;
    Engine(const Engine& e) = delete; // do not move this struct please :D
    Engine(Engine&&) = delete;

    Engine& operator=(const Engine&) = delete;
    Engine& operator=(Engine&&) = delete;

    void run();
    void awaitTermination();
    void finishExecutionOnCond(bool cond);
    void finishExecution();
    void logicAction() const;
    void renderAction() const;

    Engine& setSimulator(ECSProcessor* sim);
    Engine& setLogger(Logger* logger);
    Engine& setDisplay(IDisplayManager* display);
    Engine& addEventListener(WindowEventListener* l);
    Engine& addPollable(IPollable* p);
    Engine& addStartupListener(const std::function<void()>& fn);
    Engine& addShutdownListener(const std::function<void()>& fn);

    Engine& setUPS(int val);
    Engine& setFPS(int val);
    Engine& syncUPSFPS(int val);
    int upsDur() const;
    int fpsDur() const;
    float upsMs() const;
    float fpsMs() const;

};

void enginePeriodicFunction(Engine* e, void (*fnPtr)(Engine*), int (*intervalPtr)(Engine*), const std::atomic<bool>* running);

