#include "project/runtime.h"
#include "test/test_assertions.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>

namespace
{
    struct Fixture
    {
        std::filesystem::path root = std::filesystem::temp_directory_path() /
            ("cpp-desktop-runtime-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        project::Project project;
        Fixture()
        {
            std::filesystem::create_directories(root);
            project.root = root; project.manifest = root / "project.json"; project.name = "Desktop runtime test";
            project.scene.id = "scene:main"; project.scene.source = "main.scene.json";
            project.inputBindings = {{"move", {"Key:W"}}, {"ui_back", {"Key:Escape"}}};
            project.assets = {{"script", {"script", "main.lua", "script"}}, {"target-script", {"target-script", "target.lua", "script"}},
                {"texture", {"texture", "image.ppm", "texture"}}, {"font", {"font", "font.json", "font"}}, {"room", {"room", "room.scene.json", "scene"}}};
            project::SceneEntity player; player.id = "traveller"; player.name = "Traveller"; player.transform = project::Transform{};
            player.script = project::Script{"script"}; player.collider2D = project::Collider2D{};
            project::SpriteRenderer sprite; sprite.texture = "texture"; sprite.frames = {{{0,0,1,1}}, {{1,0,1,1}}}; sprite.framesPerSecond = 60;
            player.spriteRenderer = sprite;
            project::SceneEntity wall; wall.id = "wall"; wall.name = "Wall"; wall.transform = project::Transform{{2,0,0}}; wall.collider2D = project::Collider2D{};
            project::SceneEntity camera; camera.id = "camera"; camera.name = "Camera"; camera.camera2D = project::Camera2D{}; camera.camera2D->follow = "traveller";
            project.scene.entities = {player, wall, camera};
            write("image.ppm", "P3\n2 1\n255\n255 255 255 255 255 255\n"); write("font.json", "{}\n"); write("target.lua", "return {}\n");
            write("main.lua", "return {}\n");
            auto room = project.scene; room.id = "scene:room"; room.entities[0].script = project::Script{"target-script"};
            room.entities[0].transform->position = {4,0,0}; write("room.scene.json", project::sceneJson(room));
        }
        ~Fixture() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
        void write(const std::string& name, const std::string& text)
        { std::ofstream output(root / name, std::ios::binary | std::ios::trunc); output << text; test::require(output.good(), "runtime fixture write must succeed"); }
        project::RuntimeOptions options() const
        {
            project::RuntimeOptions result;
            result.glyphAdvance = [](std::string_view, char32_t) { return 8.0f; };
            result.fontLineHeight = [](std::string_view) { return 8.0f; };
            result.audio = std::make_shared<audio::AudioSystem>(); test::require(result.audio->initialize().has_value(), "offline audio must initialize");
            result.persistence = std::make_shared<project::PersistenceStore>(project::PersistenceOptions{root / "player-data", "fixture"});
            result.inputMapper = std::make_shared<interaction::ActionMapper>(interaction::BindingMap{{"move", {"Key:W"}}, {"ui_back", {"Key:Escape"}}});
            return result;
        }
    };

    void cachedModulesResolveTheActiveEntityContext()
    {
        Fixture fixture;
        fixture.write("controller.lua", R"lua(return {new=function() return {
            on_update=function() assert(engine.is_alive(self.id())); assert(not os and not package and (not io or not io.open))
                if input.held('move') then self.set_position(self.get_position().x+input.value('move'),0,0) end
            end} end})lua");
        fixture.write("main.lua", "return require('controller').new()");
        auto second = fixture.project.scene.entities.front(); second.id = "second";
        second.transform->position = {10,0,0}; fixture.project.scene.entities.push_back(second);
        project::Runtime runtime(fixture.project, fixture.options());
        test::require(runtime.start().has_value(), "shared controller module should pass declarations and create both owners");
        project::InputSnapshot input; input.held = {"move"}; input.values = {{"move",0.5f}};
        const auto firstTick = runtime.tick(input);
        if (!firstTick) throw std::runtime_error(firstTick.error().front().message);
        const auto secondTick = runtime.tick(input);
        if (!secondTick) throw std::runtime_error(secondTick.error().front().message);
        test::require(runtime.position("traveller")->at(0) == 1 && runtime.position("second")->at(0) == 11,
            "module self and analog input must resolve the current owner on every callback");
        test::require(runtime.stop().has_value(), "shared module runtime should stop");
        fixture.write("controller.lua", "save.write('forbidden',{}); return {}");
        project::Runtime rejected(fixture.project, fixture.options());
        test::require(!rejected.start() && !fixture.options().persistence->load("forbidden"),
            "module declarations must reject effects and leave durable data untouched");
    }

    void modalInputAndCollisionUseLiveSnapshots()
    {
        Fixture fixture;
        fixture.write("main.lua", R"lua(return {
            on_create=function() assert(ui.open({id='dialog',font='font',text='UTF-8: café Ω',width=100,height=20})) end,
            on_update=function() if input.held('move') then assert(self.move(input.value('move'),0)) end end
        })lua");
        project::Runtime runtime(fixture.project, fixture.options()); test::require(runtime.start().has_value(), "modal fixture should start");
        project::InputSnapshot input; input.pressed = {"move", "ui_back"}; input.held = {"move"}; input.values = {{"move", 0.5f}};
        test::require(runtime.tick(input).has_value() && runtime.panels().empty() && runtime.position("traveller")->at(0) == 0,
            "modal dismissal must consume gameplay input in the closing tick");
        input.pressed.clear(); test::require(runtime.tick(input).has_value() && runtime.position("traveller")->at(0) == 0,
            "held control must stay suppressed after modal dismissal");
        input.held.clear(); input.values.clear(); input.released = {"move"}; test::require(runtime.tick(input).has_value(), "release should clear held controls");
        test::require(runtime.tick().has_value(), "neutral tick should clear suppression");
        input.released.clear(); input.pressed = {"move"}; input.held = {"move"}; input.values = {{"move", 0.5f}};
        test::require(runtime.tick(input).has_value() && std::abs(runtime.position("traveller")->at(0) - 0.5f) < 0.0001f,
            "analog value must reach collision movement after a fresh press");
        test::require(runtime.camera()->center[0] == runtime.position("traveller")->at(0) && runtime.sprites().front().frame == 1,
            "camera and animated sprite snapshots must reflect the successful simulation tick");
        input.pressed.clear(); input.values["move"] = 1;
        test::require(runtime.tick(input).has_value() && runtime.position("traveller")->at(0) <= 1.0001f,
            "swept movement must stop at the wall");
        input.values["move"] = std::numeric_limits<float>::infinity(); const auto ticks = runtime.tickCount();
        test::require(!runtime.tick(input) && runtime.tickCount() == ticks && runtime.running(), "invalid analog values must reject without faulting or advancing");
        test::require(runtime.stop().has_value(), "modal fixture should cleanly stop");
    }

    void sceneCandidatesCopyStateAndPreserveOpaqueHandles()
    {
        Fixture fixture; std::vector<std::string> logs;
        fixture.write("main.lua", R"lua(return {
            on_create=function() assert(state.write({n=0})) end,
            on_update=function() local s=state.read(); s.n=s.n+1; assert(state.write(s)); engine.log(string.format('%d:%s',s.n,tostring(s.candidate))) end
        })lua");
        fixture.write("target.lua", R"lua(return {on_create=function() local s=state.read();s.candidate=true;assert(state.write(s));error('reject prepared room') end})lua");
        auto options = fixture.options(); options.log = [&](std::string_view text) { logs.emplace_back(text); };
        project::Runtime runtime(fixture.project, options); test::require(runtime.start().has_value(), "source room should start");
        const auto oldHandle = *runtime.entityHandle("traveller");
        test::require(runtime.requestScene("room").has_value() && !runtime.tick() && runtime.running() && runtime.activeScene().id == "scene:main",
            "failed target initialization must retain the playable source room");
        test::require(runtime.tick().has_value() && logs.back() == "2:nil" && runtime.entityHandle("traveller") == oldHandle,
            "rejected target state writes must not modify source session state or handles");
        fixture.write("target.lua", "return {on_create=function() local s=state.read(); s.candidate=true; assert(state.write(s)); "
            "local ok,err=save.write('prepared',{});assert(ok==nil and err); "
            "local voice,aerr=audio.play('missing',true);assert(voice==nil and aerr) end, on_update=function() "
            "assert(not engine.is_alive(" + std::to_string(oldHandle) + "));local s=state.read();assert(s.n==3 and s.candidate);engine.log('committed') end}");
        test::require(runtime.requestScene("room").has_value() && runtime.tick().has_value() && runtime.activeScene().id == "scene:room" && runtime.sceneRevision() == 2,
            "validated target must commit at the successful tick boundary");
        test::require(runtime.entityHandle("traveller") != oldHandle && runtime.position("traveller")->at(0) == 4,
            "committed scene must publish new handles and live authored positions");
        test::require(runtime.tick().has_value() && logs.back() == "committed", "retired handles must remain dead and session state must survive commit");
        test::require(!options.persistence->load("prepared"), "scene preparation must not write durable saves");
        test::require(runtime.stop().has_value(), "scene transaction fixture should cleanly stop");
    }

    void settingsPersistAndApplyToInputAndMixer()
    {
        Fixture fixture;
        fixture.write("main.lua", R"lua(return {on_update=function()
            assert(settings.write({bindings={move={'Key:Z'},ui_back={'Key:Escape'}},audio={master=0.5}}))
            assert(save.write('checkpoint',{scene='scene:main',entity='traveller',n=7}))
            assert(save.read('checkpoint').n==7)
        end})lua");
        auto options = fixture.options(); project::Runtime runtime(fixture.project, options);
        test::require(runtime.start().has_value() && runtime.tick().has_value(), "settings and save bridges should succeed");
        test::require(options.inputMapper->bindings().at("move") == std::vector<std::string>{"Key:Z"}, "saved remapping must affect the live mapper");
        test::require(runtime.stop().has_value(), "settings session should stop");
        options.inputMapper = std::make_shared<interaction::ActionMapper>(interaction::BindingMap{{"move", {"Key:W"}}});
        project::Runtime restart(fixture.project, options); test::require(restart.start().has_value(), "runtime restart should load settings separately from progression");
        interaction::RawInput raw; raw.down = {"Key:Z"}; test::require(options.inputMapper->sample(raw).held.contains("move"), "restarted physical mapping should use saved binding");
        auto clip = std::make_shared<audio::Clip>(); clip->sampleRate = 48000; clip->channels = 1; clip->samples = {1,1};
        const auto voice = options.audio->play(clip); test::require(voice.has_value(), "test tone should enter live mixer");
        std::array<float,2> mixed{}; options.audio->mix(mixed);
        test::require(std::abs(mixed[0] - 0.5f) < 0.001f, "saved master volume must affect actual mixed samples");
        test::require(restart.stop().has_value(), "restarted session should cleanly stop");
    }
}

void runDesktopRuntimeTests()
{
    cachedModulesResolveTheActiveEntityContext();
    modalInputAndCollisionUseLiveSnapshots();
    sceneCandidatesCopyStateAndPreserveOpaqueHandles();
    settingsPersistAndApplyToInputAndMixer();
}
