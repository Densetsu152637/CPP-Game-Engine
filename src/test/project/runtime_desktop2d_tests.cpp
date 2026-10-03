#include "project/runtime.h"
#include "project/desktop_input_bridge.h"
#include "test/test_assertions.h"
#include <algorithm>
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
            on_update=function() assert(engine.is_alive(self.id())); assert(not os and not package and not io.open);print('safe module log')
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
        input.released.clear(); input.pressed = {"move"}; input.held = {"move"}; input.values = {{"move", 0.5f}};
        test::require(runtime.tick(input).has_value() && std::abs(runtime.position("traveller")->at(0) - 0.5f) < 0.0001f,
            "analog value must reach collision movement after a fresh press");
        test::require(runtime.camera()->center[0] == runtime.position("traveller")->at(0) && runtime.sprites().front().frame == 0,
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

    void desktopFrontendMergesSourcesAndMapsFractionalViewports()
    {
        std::set<std::string> previous;interaction::ActionFrame physical;physical.held={"confirm"};physical.values={{"confirm",0.75f}};
        physical.pointerX=3;physical.wheelY=2;
        project::InputSnapshot injected;injected.held={"confirm"};injected.pressed={"confirm"};
        auto frame=project::desktop::mergeInput(physical,injected,previous);
        test::require(frame.pressed.contains("confirm")&&frame.values.at("confirm")==1,"combined input should press once and injected held should override analog value");
        injected={};injected.released={"confirm"};frame=project::desktop::mergeInput(physical,injected,previous);
        test::require(frame.held.contains("confirm")&&frame.released.empty()&&frame.pressed.empty()&&frame.values.at("confirm")==0.75f&&frame.pointerX==3&&frame.wheelY==2,
            "injected release must not release a physically held action or discard analog/pointer/wheel input");
        physical.held.clear();physical.values.clear();frame=project::desktop::mergeInput(physical,{},previous);
        test::require(frame.released.contains("confirm")&&!frame.held.contains("confirm"),"combined release occurs when the last source releases");
        const auto bottom=project::desktop::logicalPointer(99.5,77.8,100,100,100,100,{0,22,100,56},{320,180});
        test::require(bottom[1]>179&&bottom[1]<180&&bottom[0]>318,"fractional viewport bottom row must map to its rendered logical row");
        const auto dpi=project::desktop::logicalPointer(49.75,38.9,50,50,100,100,{0,22,100,56},{320,180});
        test::require(dpi==bottom,"framebuffer DPI conversion must preserve logical pointer coordinates");
        test::require(project::desktop::logicalPointer(50,10,100,100,100,100,{0,22,100,56},{320,180})[1]<0,
            "letterbox pointer coordinates must remain outside logical UI");
    }

    void jsonBridgeBoundsNestedDataAndReservesLuaStack()
    {
        Fixture fixture;
        fixture.write("main.lua",R"lua(return {on_update=function()
            local value={};local node=value;for i=1,23 do node.child={};node=node.child end;node.value=true
            assert(save.write('near-limit',value));local read=assert(save.read('near-limit'))
            for i=1,23 do read=read.child end;assert(read.value==true)
            local cycle={};cycle.self=cycle;local ok,err=save.write('cycle',cycle);assert(ok==nil and err)
            node.child={};node.child.value=true;ok,err=save.write('too-deep',value);assert(ok==nil and err)
            ok,err=save.write('too-large',{text=string.rep('x',65537)});assert(ok==nil and err)
            local many={};for i=1,65536 do many[i]=true end;ok,err=save.write('too-many',{nodes=many});assert(ok==nil and err)
            local stored;stored,err=save.read('host-deep');assert(stored==nil and err)
            stored,err=save.read('host-null');assert(stored==nil and err)
            assert(state.write({stillUsable=true}));assert(state.read().stillUsable)
        end})lua");
        auto options=fixture.options();picojson::value nested(picojson::object{{"leaf",picojson::value(true)}});
        for(int i=0;i<27;++i) nested=picojson::value(picojson::object{{"child",nested}});
        test::require(options.persistence->save("host-deep",nested.get<picojson::object>()).has_value(),"host persistence can store data deeper than Lua bridge policy");
        const auto original=options.persistence->load("host-deep")->at("child").serialize();
        const picojson::object nullable{{"items",picojson::value(picojson::array{picojson::value(),picojson::value(true)})}};
        test::require(options.persistence->save("host-null",nullable).has_value(),"host null fixture should save");
        project::Runtime runtime(fixture.project,options);test::require(runtime.start().has_value(),"JSON bridge fixture should start");
        const auto tick=runtime.tick();if(!tick) throw std::runtime_error(tick.error().front().message);
        test::require(runtime.running()&&!options.persistence->load("cycle")&&!options.persistence->load("too-deep")&&
            !options.persistence->load("too-large")&&!options.persistence->load("too-many"),"invalid JSON writes must reject without effects or faulting");
        test::require(options.persistence->load("host-deep")->at("child").serialize()==original,"over-limit Lua reads must preserve stored bytes/data");
        test::require(options.persistence->load("host-null")->at("items").serialize()=="[null,true]","unsupported null reads must preserve stored array shape");
        test::require(runtime.stop().has_value(),"JSON bridge fixture should stop");
    }

    void staticAndPausedSpriteSnapshotsRemainDrawable()
    {
        Fixture fixture;
        auto fixed=fixture.project.scene.entities.front();fixed.id="static";fixed.script.reset();
        fixed.spriteRenderer->frames.clear();fixed.spriteRenderer->framesPerSecond=0;
        auto paused=fixed;paused.id="paused";paused.spriteRenderer->frames={{{0,0,1,1}},{{1,0,1,1}}};
        fixture.project.scene.entities.push_back(fixed);fixture.project.scene.entities.push_back(paused);
        project::Runtime runtime(fixture.project,fixture.options());
        test::require(runtime.start().has_value(), "static sprite fixture should start");
        for(int tick=0;tick<3;++tick)
        {
            const auto sprites=runtime.sprites();test::require(sprites.size()==3, "static, paused, and animated sprites should all publish snapshots");
            for(const auto& sprite:sprites) test::require(sprite.frame==(sprite.id=="traveller"?std::size_t(tick%2):0),
                "static and paused sprites must select frame zero while animation advances");
            test::require(runtime.tick().has_value(), "sprite snapshot fixture should advance");
        }
        test::require(runtime.stop().has_value(), "sprite snapshot fixture should stop");
    }

    void rejectedCandidateTeardownCannotMutateHostServices()
    {
        Fixture fixture;
        const std::vector<std::uint8_t> wav={'R','I','F','F',40,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,
            1,0,1,0,0x80,0xbb,0,0,0,0x77,1,0,2,0,16,0,'d','a','t','a',4,0,0,0,0,0x40,0,0xc0};
        fixture.write("cue.wav",std::string(wav.begin(),wav.end()));
        fixture.project.assets.emplace("cue",project::Asset{"cue","cue.wav","audio"});
        fixture.write("main.lua", "return {on_destroy=function() error('source retirement failed') end}");
        fixture.write("target.lua", R"lua(return {on_destroy=function()
            local voice,err=audio.play('cue',true);assert(voice==nil and err)
            local ok,err=save.write('candidate-sentinel',{rejected=true}); assert(ok==nil and err)
            ok,err=settings.write({audio={master=0.1}}); assert(ok==nil and err)
            ok,err=audio.volume('master',0.1); assert(ok==nil and err)
            engine.log('candidate-teardown-effects-denied')
        end})lua");
        auto options = fixture.options();std::vector<std::string> logs;
        options.log=[&](std::string_view text){logs.emplace_back(text);};
        picojson::object originalSettings{{"unchanged",picojson::value(true)}};
        test::require(options.persistence->saveSettings(originalSettings).has_value(), "baseline settings should be durable");
        options.audio->setBusGain(audio::Bus::Master,0.75f);
        project::Runtime runtime(fixture.project,options);
        test::require(runtime.start().has_value() && runtime.requestScene("room").has_value(), "teardown fixture should prepare target");
        const auto transitioned=runtime.tick();
        test::require(!transitioned && transitioned.error().front().code=="runtime.script.stop" && !runtime.running() && runtime.liveEntityCount()==0,
            "irreversible source teardown failure must report failure and clean the stopped source");
        test::require(!options.persistence->load("candidate-sentinel") && options.persistence->loadSettings()->at("unchanged").get<bool>(),
            "rejected candidate destruction must preserve durable saves and settings");
        test::require(options.audio->voiceCount()==0, "rejected candidate must not start voices");
        test::require(std::find(logs.begin(),logs.end(),"candidate-teardown-effects-denied")!=logs.end(),
            "candidate destruction must execute all guarded effects against a real loaded clip and receive failures");
        auto clip=std::make_shared<audio::Clip>();clip->sampleRate=48000;clip->channels=1;clip->samples={1,1};
        test::require(options.audio->play(clip).has_value(), "baseline mixer probe should play");
        std::array<float,2> mixed{};options.audio->mix(mixed);
        test::require(std::abs(mixed[0]-0.75f)<0.001f, "rejected candidate must preserve live mixer gains");
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
    desktopFrontendMergesSourcesAndMapsFractionalViewports();
    jsonBridgeBoundsNestedDataAndReservesLuaStack();
    staticAndPausedSpriteSnapshotsRemainDrawable();
    rejectedCandidateTeardownCannotMutateHostServices();
    settingsPersistAndApplyToInputAndMixer();
}
