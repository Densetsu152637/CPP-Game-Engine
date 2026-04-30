//
// Created by Nicholas on 01/05/26.
//

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdexcept>
#include <thread>

#include "../core/engine.h"

void runEngine(Engine* e)
{
    if (nullptr == e) return;

    e->run();
}

void glfw_main(Engine* engine, IDisplayManager* display)
{
    // 1. Init GLFW
    if (!glfwInit())
        throw std::runtime_error("Failed to init GLFW");

    // 2. Tell GLFW we are NOT using OpenGL
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    // 3. Create window
    // example: GLFWwindow* window = glfwCreateWindow(800, 600, "Vulkan Window", nullptr, nullptr);

    if (!display->createDisplay())
    {
        glfwTerminate();
    }

    // create thread for running the engine
    std::thread engineThread(runEngine, engine);

    // 4. Main thread loop
    while ( !display->isClosed() ) {
        glfwPollEvents();
    }

    // await engine to terminate when the window should close
    engineThread.join();
    display->closeDisplay();

    // 5. Cleanup
    glfwTerminate();
}
