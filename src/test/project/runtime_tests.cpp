#include "project/runtime.h"
#include "test/test_assertions.h"
#include "tooling/iteration.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

namespace
{
    project::Project sample()
    {
        auto loaded = project::loadProject("examples/first-project/project.json");
        test::require(loaded.has_value(), "first project fixture should parse and validate");
        test::require(!loaded->scene.entities.empty(), "sample scene should contain its authored player");
        test::require(loaded->assets.contains("asset:player-script") &&
            loaded->scene.entities.front().meshRenderer.has_value(),
            "sample should resolve stable script, mesh, and texture asset references");
        test::require(loaded->inputActions.contains("move_right") && loaded->inputActions.at("move_right") == "Right",
            "sample input map should author the move_right action");
        return std::move(*loaded);
    }

    float playerX(const project::Runtime& runtime)
    {
        const auto p = runtime.position("entity:player");
        test::require(p.has_value(), "player should remain alive during runtime");
        return (*p)[0];
    }

    void authoredInputMovesOwner()
    {
        project::Runtime runtime(sample());
        test::require(runtime.start().has_value(), "authored project should instantiate");
        test::require(runtime.runtimeEntities().size() == 1, "authored scene should create one entity");
        const auto before = runtime.position("entity:player");
        test::require(before && (*before)[0] == 0.0f, "authored transform should seed the player position");
        project::InputSnapshot unknown;
        unknown.pressed.insert("jump");
        test::require(!runtime.tick(unknown).has_value(), "undeclared input actions should be rejected");
        test::require(runtime.tickCount() == 0, "rejected input should not advance simulation");
        project::InputSnapshot input;
        input.pressed.insert("move_right");
        test::require(runtime.tick(input).has_value(), "action input should tick successfully");
        const auto after = runtime.position("entity:player");
        test::require(after && (*after)[0] == 1.0f, "entity-owned script should move its authored owner");
        test::require(runtime.tickCount() == 1, "runtime should count fixed simulation ticks");
        runtime.stop();
        test::require(!runtime.running() && !runtime.position("entity:player"), "stop should discard isolated runtime entities");
    }

    void authoredLuaSystemsUseDeferredSortedSnapshots()
    {
        auto project = sample();
        project.assets.at("asset:player-script").path = "scripts/system_fixture.lua";
        std::vector<std::string> logs;
        project::RuntimeOptions options;
        options.log = [&](std::string_view message) { logs.emplace_back(message); };
        project::Runtime runtime(std::move(project), std::move(options));
        test::require(runtime.start().has_value(), "Lua component/system project should start");
        test::require(runtime.liveEntityCount() == 2, "system fixture should own its authored and spawned entities");
        const auto authored = runtime.runtimeEntities().at("entity:player");
        const auto firstTick = runtime.tick();
        test::require(firstTick.has_value(), firstTick ? "Lua system snapshot should tick" :
            "Lua system snapshot should tick: " + firstTick.error().front().message);
        std::vector<std::string> stateLogs;
        for (const auto& entry : logs)
            if (entry.starts_with("state:")) stateLogs.push_back(entry);
        test::require(stateLogs.size() == 2 && stateLogs[0].starts_with("state:" + std::to_string(authored) + ":"),
            "system should visit its component query in stable packed-entity order");
        test::require(std::none_of(logs.begin(), logs.end(), [](const auto& entry) { return entry.starts_with("late:"); }),
            "entities/components added by one system must not enter another query until the tick boundary");

        const auto beforeSecondTickLogs = logs.size();
        test::require(runtime.tick().has_value(), "deferred structural edits should commit before the next tick");
        const auto next = logs.begin() + static_cast<std::ptrdiff_t>(beforeSecondTickLogs);
        test::require(std::count_if(next, logs.end(), [](const auto& entry) { return entry.starts_with("state:"); }) == 1,
            "a component removed during iteration should be absent from the next query snapshot");
        test::require(std::count_if(next, logs.end(), [](const auto& entry) { return entry.starts_with("late:"); }) == 1,
            "new components should enter matching systems after the successful boundary flush");
        test::require(runtime.liveEntityCount() == 3, "deferred entity creation should become live at the boundary");
        test::require(runtime.stop().has_value(), "system fixture should cleanly discard all runtime entities");
        test::require(runtime.liveEntityCount() == 0, "stop should remove dynamic-component entities and their rows");
    }

    void restartPrunesOldScriptSchemasAndSystems()
    {
        const auto fixture = std::filesystem::current_path() / "build" /
            ("runtime-restart-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(fixture);
        const auto source = fixture / "player.lua";
        struct Cleanup
        {
            std::filesystem::path file, directory;
            ~Cleanup() { std::error_code ignored; std::filesystem::remove(file, ignored);
                std::filesystem::remove(directory, ignored); }
        } cleanup{source, fixture};
        const auto writeScript = [&](std::string_view type, std::string_view value, std::string_view label)
        {
            std::ofstream output(source, std::ios::binary | std::ios::trunc);
            test::require(output.good(), "restart fixture should open its project-local script");
            output << "assert(engine.register_component('Shape', {value='" << type << "'}))\n"
                << "return {on_create=function() assert(self.set_component('Shape', {value=" << value << "})) end,\n"
                << "systems={{name='shape', all={'Shape'}, reads={'Shape'}, writes={},\n"
                << "update=function() engine.log('" << label << "') end},\n"
                << "{name='position', all={'Position3D'}, reads={'Position3D'}, writes={},\n"
                << "update=function(entity) if engine.get_position(entity) then engine.log('position-"
                << label << "') end end}}}\n";
            output.close();
            test::require(output.good(), "restart fixture should write its project-local script");
        };
        writeScript("number", "1", "old-system");
        auto authored = sample();
        authored.root = std::filesystem::canonical(fixture);
        authored.assets.at("asset:player-script").path = "player.lua";
        std::vector<std::string> logs;
        project::RuntimeOptions options;
        options.log = [&](std::string_view message) { logs.emplace_back(message); };
        project::Runtime runtime(std::move(authored), std::move(options));
        test::require(runtime.start().has_value() && runtime.tick().has_value(),
            "first schema version and system should run");
        test::require(std::count(logs.begin(), logs.end(), "old-system") == 1,
            "first system should execute exactly once");
        test::require(std::count(logs.begin(), logs.end(), "position-old-system") == 1,
            "a Position3D query should visit the authored native component");
        test::require(runtime.stop().has_value() && runtime.liveEntityCount() == 0 &&
            runtime.activeScriptCount() == 0, "stop should release scripts and component rows");
        writeScript("string", "'ready'", "new-system");
        const auto secondStart = runtime.start();
        test::require(secondStart.has_value(), secondStart ? "changed schema should load after restart" :
            "changed schema should load after restart: " + secondStart.error().front().message);
        test::require(runtime.tick().has_value(), "restarted system should tick");
        test::require(std::count(logs.begin(), logs.end(), "old-system") == 1 &&
            std::count(logs.begin(), logs.end(), "new-system") == 1 &&
            std::count(logs.begin(), logs.end(), "position-old-system") == 1 &&
            std::count(logs.begin(), logs.end(), "position-new-system") == 1,
            "restart must not retain the retired system or duplicate the new callback");
        test::require(runtime.stop().has_value(), "restarted runtime should stop cleanly");
    }

    void failedLuaSystemAbortsQueuedStructuralChanges()
    {
        auto project = sample();
        project.assets.at("asset:player-script").path = "scripts/system_abort_fixture.lua";
        std::vector<std::string> logs;
        project::RuntimeOptions options;
        options.log = [&](std::string_view message) { logs.emplace_back(message); };
        project::Runtime runtime(std::move(project), std::move(options));
        test::require(runtime.start().has_value(), "abort fixture should start");
        test::require(!runtime.tick().has_value(), "failing Lua system should fault the runtime");
        test::require(runtime.liveEntityCount() == 1,
            "failed system batch must discard queued entity creation instead of partially applying it");
        test::require(!runtime.running(), "system callback failure should fault the runtime");
        (void)runtime.stop();
        test::require(std::any_of(logs.begin(), logs.end(), [](const std::string& entry)
        {
            return entry.starts_with("abort-state:") &&
                std::stod(entry.substr(std::string("abort-state:").size())) == 0.0;
        }),
            "queued component value changes should also remain unapplied when a callback batch aborts");
    }

    void stagedReloadIsBoundaryAppliedAndKeepsLastGood()
    {
        project::Runtime runtime(sample());
        test::require(runtime.start().has_value(), "reload fixture should start");
        test::require(tooling::stageRuntimeScriptReload(runtime, "entity:player",
            "examples/first-project/scripts/player_reload.lua").has_value(),
            "compile-checked candidate should stage without running its lifecycle early");
        project::InputSnapshot input;
        input.pressed.insert("move_right");
        test::require(runtime.tick(input).has_value(), "staged replacement should apply at the tick boundary");
        test::require(playerX(runtime) == 10.0f, "replacement should own the next callback");

        test::require(!tooling::stageRuntimeScriptReload(runtime, "entity:player",
            "examples/first-project/scripts/invalid.lua").has_value(), "invalid source should be rejected before staging");
        test::require(runtime.tick(input).has_value(), "compile-rejected source must leave current script active");
        test::require(playerX(runtime) == 20.0f, "compile rejection must preserve last known good script");

        test::require(runtime.stageScriptReload("entity:player", "return { on_update = function( end }", "@broken.lua").has_value(),
            "runtime queue accepts externally staged candidate source");
        test::require(!runtime.tick(input).has_value(), "defensive runtime compile should report an invalid queued candidate");
        test::require(!runtime.tick(input).has_value(), "faulted runtime must reject further simulation ticks");
        test::require(runtime.stop().has_value(), "faulted runtime should still stop cleanly");
    }

    void teardownFailureKeepsOldScriptAndCleansCandidate()
    {
        auto project = sample();
        project.assets.emplace("asset:teardown-fails", project::Asset{
            "asset:teardown-fails", "scripts/teardown_fails.lua", "script"});
        project.scene.entities.front().script->asset = "asset:teardown-fails";
        std::vector<std::string> logs;
        project::RuntimeOptions options;
        options.log = [&](std::string_view message) { logs.emplace_back(message); };
        project::Runtime runtime(std::move(project), std::move(options));
        test::require(runtime.start().has_value(), "failing teardown fixture should start");
        test::require(std::find(logs.begin(), logs.end(), "script-started\t7") != logs.end(),
            "project Lua print output should route through the runtime logger");
        const std::string candidate = R"lua(return {
            on_destroy = function() engine.log("candidate-cleanup") end,
            on_update = function()
                if input.pressed("move_right") then
                    local p = self.get_position()
                    self.set_position(p.x + 10, p.y, p.z)
                end
            end
        })lua";
        test::require(runtime.stageScriptReload("entity:player", candidate, "@candidate.lua").has_value(),
            "replacement candidate should stage");
        project::InputSnapshot input;
        input.pressed.insert("move_right");
        test::require(!runtime.tick(input).has_value(), "old on_destroy failure should report reload failure");
        test::require(std::find(logs.begin(), logs.end(), "candidate-cleanup") != logs.end(),
            "candidate on_destroy should run when old script teardown rejects replacement");
        test::require(!runtime.tick(input).has_value(), "faulted runtime should reject more callbacks after lifecycle failure");
        const auto stopped = runtime.stop();
        test::require(!stopped.has_value(), "stop should report the retained script teardown failure");
        test::require(std::count(logs.begin(), logs.end(), "old-destroy") == 2,
            "previous script remains active after failed teardown and receives cleanup during stop");
        test::require(!runtime.position("entity:player"), "stop cleans the isolated scene even after lifecycle errors");
    }

    void invalidScriptFailsBeforeRuntimeBecomesActive()
    {
        auto project = sample();
        project.scene.entities.front().script->asset = "asset:missing-script";
        project::Runtime runtime(std::move(project));
        const auto started = runtime.start();
        test::require(!started.has_value(), "missing script should fail during setup before runtime is active");
        test::require(!runtime.running(), "failed setup must leave the runtime inactive");
        test::require(!runtime.position("entity:player"), "invalid asset IDs must fail before ECS instantiation");
    }

    void invalidLuaFailsDuringSetupAndCleansScene()
    {
        auto project = sample();
        project.assets.emplace("asset:valid-sentinel", project::Asset{
            "asset:valid-sentinel", "scripts/teardown_fails.lua", "script"});
        project.assets.emplace("asset:invalid-later", project::Asset{
            "asset:invalid-later", "scripts/invalid.lua", "script"});
        project.scene.entities.front().script->asset = "asset:valid-sentinel";
        auto laterEntity = project.scene.entities.front();
        laterEntity.id = "entity:later";
        laterEntity.name = "Later Script";
        laterEntity.script->asset = "asset:invalid-later";
        project.scene.entities.push_back(std::move(laterEntity));
        std::vector<std::string> logs;
        project::RuntimeOptions options;
        options.log = [&](std::string_view message) { logs.emplace_back(message); };
        project::Runtime runtime(std::move(project), std::move(options));
        const auto started = runtime.start();
        test::require(!started.has_value(), "malformed later Lua must fail runtime setup");
        test::require(logs.empty(), "all scripts must compile before the first script's on_create runs");
        test::require(!runtime.running() && runtime.runtimeEntities().empty() && !runtime.position("entity:player"),
            "syntax validation failure must happen before temporary ECS instantiation");
    }

    void invalidDeclarationsFailBeforeWorldCreation()
    {
        const auto fixture = std::filesystem::current_path() / "build" /
            ("runtime-declaration-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(fixture);
        const auto source = fixture / "player.lua";
        const auto firstSource = fixture / "first.lua";
        struct Cleanup
        {
            std::filesystem::path file, firstFile, directory;
            ~Cleanup() { std::error_code ignored; std::filesystem::remove(file, ignored);
                std::filesystem::remove(firstFile, ignored);
                std::filesystem::remove(directory, ignored); }
        } cleanup{source, firstSource, fixture};
        { std::ofstream output(firstSource, std::ios::binary);
            output << "return {on_create=function() engine.log('first-created') end}\n";
            output.close(); test::require(output.good(), "first declaration fixture should be writable"); }
        const auto write = [&](std::string_view reads)
        {
            std::ofstream output(source, std::ios::binary | std::ios::trunc);
            output << "assert(engine.register_component('Bad', {value='number'}))\n"
                << "return {on_create=function() engine.log('created') end,\n"
                << "systems={{name='bad', all={'Bad'}, reads=" << reads
                << ", writes={}, update=function() end}}}\n";
            output.close();
            test::require(output.good(), "declaration fixture should be writable");
        };
        write("{}");
        auto authored = sample();
        authored.root = std::filesystem::canonical(fixture);
        authored.assets.at("asset:player-script").path = "first.lua";
        authored.assets.emplace("asset:second-script", project::Asset{
            "asset:second-script", "player.lua", "script"});
        auto second = authored.scene.entities.front();
        second.id = "entity:second";
        second.name = "Second Script";
        second.script->asset = "asset:second-script";
        authored.scene.entities.push_back(std::move(second));
        std::vector<std::string> logs;
        project::RuntimeOptions options;
        options.log = [&](std::string_view message) { logs.emplace_back(message); };
        project::Runtime runtime(std::move(authored), std::move(options));
        const auto invalid = runtime.start();
        test::require(!invalid && invalid.error().front().code == "runtime.script.declaration" &&
            runtime.liveEntityCount() == 0 && logs.empty(),
            "malformed declarations must fail before entities or on_create callbacks run");
        write("{'Bad'}");
        test::require(runtime.start().has_value() && runtime.tick().has_value(),
            "a corrected declaration should start after preflight rejection");
        test::require(std::count(logs.begin(), logs.end(), "created") == 1,
            "successful start should invoke on_create once");
        test::require(std::count(logs.begin(), logs.end(), "first-created") == 1,
            "valid earlier script should first receive on_create after all declarations pass");
        test::require(runtime.stop().has_value(), "corrected declaration runtime should stop");
    }

    void onCreateFailureCleansScriptAndSpawnedEntities()
    {
        auto project = sample();
        project.assets.emplace("asset:create-then-fail", project::Asset{
            "asset:create-then-fail", "scripts/create_then_fail.lua", "script"});
        project.scene.entities.front().script->asset = "asset:create-then-fail";
        std::vector<std::string> logs;
        project::RuntimeOptions options;
        options.log = [&](std::string_view message) { logs.emplace_back(message); };
        project::Runtime runtime(std::move(project), std::move(options));

        const auto started = runtime.start();
        test::require(!started.has_value(), "on_create failure after native side effects must reject setup");
        test::require(!runtime.running(), "on_create failure must leave runtime inactive");
        test::require(runtime.runtimeEntities().empty() && runtime.liveEntityCount() == 0,
            "startup failure must destroy authored and script-spawned entities");
        test::require(runtime.activeScriptCount() == 0,
            "failed on_create must remove its Lua instance from the runtime script map");
        test::require(std::find(logs.begin(), logs.end(), "partial-on-create-effect") != logs.end(),
            "test must observe that on_create made a side effect before failing");
        test::require(std::find(logs.begin(), logs.end(), "spawned-entity-cleaned-by-script") != logs.end(),
            "LuaScriptSystem should invoke on_destroy to clean script-owned entities after failed on_create");
        test::require(!runtime.position("entity:player"), "failed setup must not expose an authored runtime entity");
    }

    void mutationIsOwnerThreadBound()
    {
        project::Runtime runtime(sample());
        project::Result<void> wrongThreadStart;
        std::thread worker([&] { wrongThreadStart = runtime.start(); });
        worker.join();
        test::require(!wrongThreadStart.has_value(), "runtime start from a different thread should fail");
        test::require(wrongThreadStart.error().front().code == "runtime.thread.owner",
            "wrong-thread start should return a stable owner-thread diagnostic");
        test::require(runtime.start().has_value(), "owner thread should still be able to start the runtime");

        project::Result<void> wrongThreadTick;
        project::Result<void> wrongThreadReload;
        project::Result<void> wrongThreadStop;
        std::thread secondWorker([&]
        {
            wrongThreadTick = runtime.tick();
            wrongThreadReload = runtime.stageScriptReload("entity:player", "return {}", "@cross-thread.lua");
            wrongThreadStop = runtime.stop();
        });
        secondWorker.join();
        test::require(!wrongThreadTick && !wrongThreadReload && !wrongThreadStop,
            "tick, reload staging, and stop must reject cross-thread mutation");
        test::require(runtime.stop().has_value(), "owner thread should stop its runtime");
    }
}

int main()
{
    try
    {
        authoredInputMovesOwner();
        authoredLuaSystemsUseDeferredSortedSnapshots();
        restartPrunesOldScriptSchemasAndSystems();
        failedLuaSystemAbortsQueuedStructuralChanges();
        stagedReloadIsBoundaryAppliedAndKeepsLastGood();
        teardownFailureKeepsOldScriptAndCleansCandidate();
        invalidScriptFailsBeforeRuntimeBecomesActive();
        invalidLuaFailsDuringSetupAndCleansScene();
        invalidDeclarationsFailBeforeWorldCreation();
        onCreateFailureCleansScriptAndSpawnedEntities();
        mutationIsOwnerThreadBound();
        std::cout << "[PASS] authored project runtime tests\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
