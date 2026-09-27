#include "../../editor/editor.h"
#include "../test_assertions.h"

#include <chrono>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <typeinfo>

namespace
{
    class TemporaryProject
    {
    public:
        std::filesystem::path root;

        TemporaryProject()
        {
            const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
            root = std::filesystem::temp_directory_path() / ("cpp-engine-editor-" + std::to_string(nonce));
            std::filesystem::create_directories(root / "scenes");
            std::filesystem::create_directories(root / "scripts");
            write(root / "project.json", R"({"schema":1,"name":"Editor Test","assets":[{"id":"asset:mover","path":"scripts/mover.lua","kind":"script"}],"startup_scene":"scenes/main.json"})");
            writeScene();
            write(root / "scripts/mover.lua", R"lua(return {
                on_update = function(dt)
                    local p = self.get_position()
                    self.set_position(p.x + dt, p.y, p.z)
                end
            })lua");
        }

        ~TemporaryProject()
        {
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }

        static void write(const std::filesystem::path& path, const std::string& content)
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output << content;
        }

        void writeScene(const std::array<float, 3> position = {0, 0, 0})
        {
            write(root / "scenes/main.json",
                "{\"schema\":1,\"scene_id\":\"scene:test\",\"entities\":[{\"id\":\"object:hero\",\"name\":\"Hero\",\"components\":{\"Transform\":{\"position\":[" +
                std::to_string(position[0]) + "," + std::to_string(position[1]) + "," + std::to_string(position[2]) + "]}}}]}");
        }
    };
}

void test_editor_document_edit_history_save_reload_and_play_isolation()
{
    TemporaryProject fixture;
    editor::Document document;
    test::require(document.open(fixture.root / "project.json"), "editor document should open a valid project");
    test::require(document.project() && document.session(), "opened document should expose its project and authoring session");
    test::require(!document.isDirty(), "newly opened document should be clean");

    const project::SetPosition move {"object:hero", {1, 2, 3}};
    test::require(document.apply(move), "typed position operation should apply");
    test::require(document.isDirty(), "an applied authoring edit should mark the document dirty");
    test::require(document.undo(), "editor undo should revert the most recent operation");
    test::require(!document.isDirty(), "undoing back to the saved content should clear dirty state");
    test::require(document.redo(), "editor redo should reapply the operation");
    test::require(document.isDirty(), "redoing should restore dirty state");

    const project::SetScript attach {"object:hero", "asset:mover"};
    test::require(document.apply(attach), "asset picker selection should use the typed script operation");
    test::require(document.save(), "editor document should save through SceneSession");
    test::require(!document.isDirty(), "successful save should update the revision baseline");
    test::require(document.session()->snapshot().entities.front().script == project::Script{"asset:mover"},
        "script asset ID should be present in the authoring snapshot");
    test::require(document.reload(), "reloading a clean saved project should succeed");
    test::require(document.session()->snapshot().entities.front().script == project::Script{"asset:mover"},
        "saved scene should round-trip the stable script asset ID");

    const auto authoredBeforePlay = document.session()->snapshot().entities.front().transform->position;
    test::require(document.play(), "editor should start an isolated runtime");
    test::require(document.isPlaying(), "play state should be visible to editor controls");
    test::require(document.tick(), "isolated runtime should advance from the UI tick path");
    const auto runtimePosition = document.playPosition("object:hero");
    test::require(runtimePosition.has_value() && (*runtimePosition)[0] > authoredBeforePlay[0],
        "runtime script should move the authored entity in play state");
    test::require(document.session()->snapshot().entities.front().transform->position == authoredBeforePlay,
        "play simulation must not mutate the editor's authored scene");
    test::require(document.stop(), "editor should stop the isolated runtime");
    test::require(!document.isPlaying() && !document.playPosition("object:hero"), "stopping should discard the isolated runtime");

    test::require(document.apply(project::SetPosition{"object:hero", {8, 0, 0}}), "new unsaved edit should apply");
    const auto revisionBeforeFailedOpen = document.session()->revision();
    test::require(!document.open(fixture.root / "missing.json", true), "opening a missing project should fail");
    test::require(document.session()->revision() == revisionBeforeFailedOpen && document.isDirty(),
        "failed project open must retain current unsaved scene edits");

    TemporaryProject brokenReload;
    test::require(!document.open(brokenReload.root / "project.json"), "dirty edits should block project replacement unless explicitly discarded");
    test::require(document.session()->revision() == revisionBeforeFailedOpen,
        "blocked project replacement must leave the current session untouched");
    test::require(document.open(brokenReload.root / "project.json", true), "second valid project should open after explicit discard");
    test::require(document.apply(project::SetPosition{"object:hero", {9, 0, 0}}), "reload preservation fixture edit should apply");
    TemporaryProject::write(brokenReload.root / "scenes/main.json", "not json");
    const auto revisionBeforeFailedReload = document.session()->revision();
    test::require(!document.reload(true), "malformed on-disk scene should reject reload");
    test::require(document.session()->revision() == revisionBeforeFailedReload && document.isDirty(),
        "failed reload must retain current unsaved scene edits");
}

int main()
{
    try
    {
        test_editor_document_edit_history_save_reload_and_play_isolation();
        std::cout << "[PASS] editor document, revision, and play isolation tests\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] type=" << typeid(error).name() << " what=[" << error.what() << "]\n";
        return 1;
    }
}
