//
// Created by Nicholas on 26/04/26.
//

#pragma once
#include <atomic>

#include "interfaces.h"
#include "../async/lock.h"
#include "../structs/arraylist.h"
#include "../logging/logger.h"

class Engine
{

    ArrayList<IResourceManager*> m_resourceManagers;
    ArrayList<IPollable*> m_pollables;
    ArrayList<std::function<void()>> m_startupListeners;
    ArrayList<std::function<void()>> m_shutdownListeners;

    Logger* m_logger = nullptr;
    Waiter m_finisher;
    std::atomic<bool> m_running = false;

public:

    ~Engine()
    {
        m_resourceManagers.for_each([&](IResourceManager* r)
            { if (r) r->cleanUp(); }
        );
    }
    Engine() {}
    Engine(const Engine& e) = delete; // do not move this struct please :D
    Engine(Engine&&) = delete;

    Engine& operator=(const Engine&) = delete;
    Engine& operator=(Engine&&) = delete;

    Engine& setLogger(Logger* logger);
    Engine& addEventListener(WindowEventListener* l);
    Engine& addResourceManager(IResourceManager* rm);
    Engine& addPollable(IPollable* p);
    Engine& addStartupListener(std::function<void()>& fn);
    Engine& addShutdownListener(std::function<void()>& fn);

};

