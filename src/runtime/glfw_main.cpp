#ifdef CPP_GAME_ENGINE_USE_VULKAN
#define GLFW_INCLUDE_VULKAN
#endif
#include <GLFW/glfw3.h>
#include <stdexcept>
#include "glfw_main.h"
#include "../core/display.h"

void runEngine(Engine* engine)
{
    if (engine) engine->run();
}

void glfw_main(Engine* engine, IDisplayManager* display, Logger* logger)
{
    if (!engine || !display) throw std::invalid_argument("glfw_main requires an engine and display");
    if (!glfwInit()) throw std::runtime_error("Failed to initialize GLFW");
    struct EventPoller final : IPollable
    {
        void poll() override { glfwPollEvents(); }
    } poller;
    try
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        initialiseMonitorEnvironment();
        if (!display->createDisplay()) throw std::runtime_error("Failed to create GLFW display");
        engine->setDisplay(display).setLogger(logger).addPollable(&poller);
        // Polling, Lua mutation and frame coordination all remain on this thread.
        engine->run();
        engine->removePollable(&poller);
        display->closeDisplay();
        glfwTerminate();
    }
    catch (...)
    {
        engine->removePollable(&poller);
        display->closeDisplay();
        glfwTerminate();
        throw;
    }
}
