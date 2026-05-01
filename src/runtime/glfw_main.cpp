//
// Created by Nicholas on 01/05/26.
//

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdexcept>
#include <thread>

#include "../core/engine.h"
#include "../core/display.h"

void runEngine(Engine* e)
{
    if (nullptr == e) return;
    e->run();
}

void glfw_main(Engine* engine, IDisplayManager* display, Logger* logger)
{
    // Init GLFW & engine / display
    if (!glfwInit())
        throw std::runtime_error("Failed to init GLFW");

    if (!engine)
        throw std::runtime_error("Failed to initialize engine: nullptr");

    if (!display)
        throw std::runtime_error("Failed to initialize display: nullptr");

    // GLFW flags
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    // Create window

    if (!display->createDisplay())
    {
        glfwTerminate();
    }

    initialiseMonitorEnvironment();
    // create thread for running the engine
    std::thread engineThread(runEngine, engine);

    // Main thread loop
    while ( !display->isClosed() )
    {
        try
        { glfwPollEvents(); }
        catch (std::exception& e)
        { logger->error(e); }
    }

    // await engine to terminate when the window should close
    engine->finishExecution();
    try
    { engineThread.join(); }
    catch (std::exception& e)
    { logger->error(e); }

    display->closeDisplay();

    // Cleanup
    glfwTerminate();
}
