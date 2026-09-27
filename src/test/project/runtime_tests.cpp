#include "project/runtime.h"
#include "test/test_assertions.h"
#include "tooling/iteration.h"

#include <algorithm>
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
        stagedReloadIsBoundaryAppliedAndKeepsLastGood();
        teardownFailureKeepsOldScriptAndCleansCandidate();
        invalidScriptFailsBeforeRuntimeBecomesActive();
        invalidLuaFailsDuringSetupAndCleansScene();
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
