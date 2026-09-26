#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include "components/alias.h"
#include "core/engine.h"
#include "ecs/processor.h"
#include "scripting/lua_script_system.h"
#include "test/test_assertions.h"

int main()
{
    try
    {
        Threadpool workers(2, "engine tests");
        ECSProcessor simulator(workers);
        Engine engine;
        engine.setSimulator(&simulator).setUPS(60).enableScripting();
        const auto script = engine.scripts()->load_file("assets/scripts/moving_entity.lua");
        test::require(script.has_value(), "sample script should load through the Engine API");
        for (int tick = 0; tick < 60; ++tick) engine.logicAction();
        unsigned count = 0;
        simulator.ecs().view<Position3D>().each([&](const Entity&, const Vector3f& position)
        {
            ++count;
            test::require(std::abs(position.x - 1.0f) < 0.001f, "Lua update should move the EnTT entity one unit per second");
        });
        test::require(count == 1, "Lua on_create should create one positioned entity");
        test::require(engine.scripts()->shutdown().has_value(), "Engine scripts should shut down");
        simulator.ecs().view<Position3D>().each([&](const Entity&, const Vector3f&) { ++count; });
        test::require(count == 1, "Lua on_destroy should remove its entity");
        bool invalidRateRejected = false;
        try { engine.setUPS(0); } catch (const std::invalid_argument&) { invalidRateRejected = true; }
        test::require(invalidRateRejected, "invalid update rates must not divide by zero");
        Engine bounded;
        ECSProcessor boundedSimulator(workers);
        bounded.setSimulator(&boundedSimulator).syncUPSFPS(1000).setLogicTickLimit(3).enableScripting();
        bool created = false;
        bool destroyed = false;
        const auto owner = std::this_thread::get_id();
        bounded.addStartupListener([&]
        {
            test::require(std::this_thread::get_id() == owner, "startup must run on the owner thread");
            created = bounded.scripts()->load_file("assets/scripts/moving_entity.lua").has_value();
        });
        bounded.addShutdownListener([&]
        {
            test::require(std::this_thread::get_id() == owner, "shutdown must run on the owner thread");
            unsigned survivors = 0;
            boundedSimulator.ecs().view<Position3D>().each([&](const Entity&, const Vector3f&) { ++survivors; });
            destroyed = survivors == 0;
        });
        bounded.run();
        bounded.awaitTermination();
        test::require(created && destroyed, "bounded Engine::run must create/update/shut down Lua entities");
        struct ThrowingLogger final : Logger
        {
            void log(const std::string&) override { throw std::runtime_error("logger failed"); }
        } throwingLogger;
        Engine failing;
        failing.setLogger(&throwingLogger).setLogicTickLimit(1).enableScripting();
        test::require(failing.scripts()->load_string("return {on_destroy=function() error('destroy failed') end}").has_value(),
            "shutdown failure test should load");
        bool shutdownListenerCalled = false;
        failing.addShutdownListener([&] { shutdownListenerCalled = true; });
        bool loggerFailureReported = false;
        try { failing.run(); }
        catch (const std::runtime_error& error) { loggerFailureReported = std::string(error.what()) == "logger failed"; }
        failing.awaitTermination();
        test::require(loggerFailureReported && shutdownListenerCalled,
            "shutdown errors must preserve completion notification and remaining cleanup");
        std::cout << "[PASS] Engine Lua/EnTT integration tests\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
