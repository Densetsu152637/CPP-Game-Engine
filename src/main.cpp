#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include "components/alias.h"
#include "core/engine.h"
#include "ecs/processor.h"
#include "scripting/lua_script_system.h"
#include "vulcan/vulkan_renderer.h"

namespace
{
    class ConsoleLogger final : public Logger
    {
        void log(const std::string& message) override { std::cout << message << '\n'; }
    };
    struct Options
    {
        bool headless = false;
        unsigned ticks = 0;
        std::string script = "assets/scripts/moving_entity.lua";
        std::filesystem::path shaders = "build/debug-vk1/shaders";
    };
    Options parseOptions(int argc, char** argv)
    {
        Options result;
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--headless") result.headless = true;
            else if ((arg == "--ticks" || arg == "--script" || arg == "--shaders") && i + 1 < argc)
            {
                const std::string value = argv[++i];
                if (arg == "--ticks")
                {
                    size_t consumed = 0;
                    const auto count = std::stoul(value, &consumed);
                    if (consumed != value.size() || count == 0 || count > 1'000'000)
                        throw std::invalid_argument("--ticks must be between 1 and 1000000");
                    result.ticks = static_cast<unsigned>(count);
                }
                else if (arg == "--script") result.script = value;
                else result.shaders = value;
            }
            else throw std::invalid_argument("Usage: CPPGameEngine [--headless] [--ticks N] [--script file.lua] [--shaders directory]");
        }
#ifndef CPP_GAME_ENGINE_USE_VULKAN
        result.headless = true;
#endif
        if (result.headless && result.ticks == 0) result.ticks = 120;
        return result;
    }
}
int main(int argc, char** argv)
{
#ifdef CPP_GAME_ENGINE_USE_VULKAN
    GLFWwindow* window = nullptr;
#endif
    try
    {
        const Options config = parseOptions(argc, argv);
        Threadpool workers(2, "simulation");
        ECSProcessor simulator(workers);
        ConsoleLogger logger;
        Engine engine;
        engine.setSimulator(&simulator).setLogger(&logger).enableScripting();
        const auto loaded = engine.scripts()->load_file(config.script);
        if (!loaded) throw std::runtime_error(loaded.error());
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        vulkan::VulkanRenderer renderer;
        vulkan::VulkanShaderProgram shader("scripted triangle");
        if (!config.headless)
        {
            if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            window = glfwCreateWindow(960, 600, "Lua + EnTT + Vulkan", nullptr, nullptr);
            if (!window) throw std::runtime_error("GLFW window creation failed");
            renderer.initialize(window);
            shader.addSpirv(rendering::ShaderStage::Vertex, config.shaders / "triangle.vert.spv");
            shader.addSpirv(rendering::ShaderStage::Fragment, config.shaders / "triangle.frag.spv");
        }
#endif
        unsigned tick = 0;
        while (config.ticks == 0 || tick < config.ticks)
        {
            const auto next = std::chrono::steady_clock::now() + std::chrono::nanoseconds(engine.upsDur());
#ifdef CPP_GAME_ENGINE_USE_VULKAN
            if (window)
            {
                glfwPollEvents();
                if (glfwWindowShouldClose(window)) break;
            }
#endif
            engine.logicAction();
#ifdef CPP_GAME_ENGINE_USE_VULKAN
            if (window && renderer.beginRenderFrame())
            {
                try
                {
                    simulator.ecs().view<Position3D>().each([&](const Entity&, const Vector3f& position)
                    {
                        const float x = std::fmod(position.x + 0.8f, 1.6f) - 0.8f;
                        const std::array<float, 16> transform {0.4f,0,0,0, 0,0.4f,0,0, 0,0,1,0, x,position.y,0,1};
                        const std::array<float, 4> color {0.15f,0.7f,1.0f,1.0f};
                        renderer.upload(shader, "transform", transform, tick, {0,0});
                        renderer.upload(shader, "color", color, tick, {0,1});
                        renderer.render(shader);
                    });
                    renderer.endRenderFrame();
                }
                catch (...) { renderer.cancelRenderFrame(); throw; }
            }
#endif
            ++tick;
            if (!config.headless) std::this_thread::sleep_until(next);
        }
        simulator.ecs().view<Position3D>().each([](const Entity& entity, const Vector3f& position)
        {
            std::cout << "Entity " << entity.packed() << " position: " << position.x << ", " << position.y << ", " << position.z << '\n';
        });
        const auto stopped = engine.scripts()->shutdown();
        if (!stopped) throw std::runtime_error(stopped.error());
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        renderer.shutdown();
        if (window) { glfwDestroyWindow(window); window = nullptr; glfwTerminate(); }
#endif
        std::cout << "Completed " << tick << " scripted engine ticks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (window) glfwDestroyWindow(window);
        glfwTerminate();
#endif
        std::cerr << error.what() << '\n';
        return 1;
    }
}
