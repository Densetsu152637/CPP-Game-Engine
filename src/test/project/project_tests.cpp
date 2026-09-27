#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "project/project.h"
#include "test/test_assertions.h"

namespace
{
    struct TemporaryProject
    {
        std::filesystem::path root;

        TemporaryProject()
        {
            const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
            root = std::filesystem::temp_directory_path() / ("cpp-game-engine-project-test-" + std::to_string(nonce));
            std::filesystem::create_directories(root / "scenes");
            std::filesystem::create_directories(root / "scripts");
            write("project.json", R"({"schema":1,"name":"Fixture","startup_scene":"scenes/main.json"})");
            write("scenes/main.json", R"({"schema":1,"scene_id":"scene:main","entities":[{"id":"object:player","name":"Player","components":{"Transform":{"position":[0,0,0]},"Script":{"asset":"scripts/player.lua"}}}]})");
            write("scripts/player.lua", "return {}\n");
        }

        ~TemporaryProject()
        {
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }

        void write(const std::filesystem::path& path, const std::string& content) const
        {
            std::filesystem::create_directories((root / path).parent_path());
            std::ofstream output(root / path, std::ios::binary | std::ios::trunc);
            output << content;
            if (!output) throw std::runtime_error("unable to create project fixture");
        }
    };

    void requireCode(const project::Diagnostics& diagnostics, const std::string& code)
    {
        test::require(std::any_of(diagnostics.begin(), diagnostics.end(), [&](const project::Diagnostic& diagnostic)
        {
            return diagnostic.code == code;
        }), "expected stable diagnostic code " + code);
    }

    void validProjectLoadsAndEditHistoryIsRevisioned()
    {
        TemporaryProject fixture;
        auto loaded = project::loadProject(fixture.root / "project.json");
        test::require(loaded.has_value(), "valid project should load");
        test::require(loaded->scene.id == "scene:main" && loaded->scene.entities.size() == 1,
            "project should load its startup scene and persistent entity");
        test::require(loaded->scene.entities[0].id == "object:player" && loaded->scene.entities[0].transform.has_value(),
            "scene identity and transform should be authored values");
        test::require(loaded->scene.entities[0].script == project::Script{"scripts/player.lua"},
            "script reference should remain project-relative");

        project::SceneSession session(loaded->scene, loaded->root);
        const auto originalRevision = session.revision();
        auto edited = session.apply(project::SetPosition{"object:player", {2.0f, 3.0f, 4.0f}}, originalRevision);
        test::require(edited.has_value() && *edited != originalRevision, "accepted edit should advance revision");
        auto stale = session.apply(project::SetPosition{"object:player", {8.0f, 9.0f, 10.0f}}, originalRevision);
        test::require(!stale, "stale revisions should be rejected");
        requireCode(stale.error(), "scene.revision.stale");
        test::require(session.undo(*edited).has_value(), "undo should restore previous authored state");
        test::require(session.snapshot().entities[0].transform->position == std::array<float, 3>{0, 0, 0},
            "undo should restore the original transform");
        const auto undoRevision = session.revision();
        test::require(session.redo(undoRevision).has_value(), "redo should reapply the transform edit");
        test::require(session.snapshot().entities[0].transform->position == std::array<float, 3>{2, 3, 4},
            "redo should restore the edited transform");
        const auto scriptRevision = session.revision();
        test::require(session.apply(project::SetScript{"object:player", std::nullopt}, scriptRevision).has_value(),
            "script component should be removable with a typed operation");
        test::require(!session.snapshot().entities[0].script, "script removal should affect the authored snapshot");
        test::require(session.undo(session.revision()).has_value(), "script component removal should be undoable");
        test::require(session.snapshot().entities[0].script.has_value(), "undo should restore the script reference");

        const auto authoredRevision = session.revision();
        test::require(session.save(authoredRevision).has_value(), "scene should save atomically from the current revision");
        auto roundTrip = project::loadProject(fixture.root / "project.json");
        test::require(roundTrip.has_value(), "saved project should parse again");
        test::require(roundTrip->scene.entities[0].transform->position == std::array<float, 3>{2, 3, 4},
            "save and reload should preserve authored meaning");

        test::require(session.beginPlay(session.revision()).has_value(), "play should snapshot authored scene");
        test::require(session.playSnapshot() != nullptr, "play should expose an isolated scene snapshot");
        test::require(session.applyPlay(project::SetPosition{"object:player", {9.0f, 8.0f, 7.0f}}).has_value(),
            "runtime play snapshot should accept operations independently");
        session.endPlay();
        test::require(session.playSnapshot() == nullptr, "stopping play should discard the runtime snapshot");
        test::require(session.snapshot().entities[0].transform->position == std::array<float, 3>{2, 3, 4},
            "runtime changes must not affect authored state");
    }

    void rejectsMalformedSchemaIdsAndPaths()
    {
        TemporaryProject fixture;
        fixture.write("scenes/main.json", R"({"schema":2,"scene_id":"scene:main","entities":[]})");
        auto unsupported = project::loadProject(fixture.root / "project.json");
        test::require(!unsupported, "newer schema versions should be rejected");
        requireCode(unsupported.error(), "project.schema.unsupported");

        fixture.write("scenes/main.json", R"({"schema":1,"scene_id":"scene:main","entities":[{"id":"object:same","name":"One","components":{}},{"id":"object:same","name":"Two","components":{}}]})");
        auto duplicate = project::loadProject(fixture.root / "project.json");
        test::require(!duplicate, "duplicate persistent ids should be rejected");
        requireCode(duplicate.error(), "scene.entity.id.duplicate");

        fixture.write("scenes/main.json", R"({"schema":1,"scene_id":"scene:main","entities":[{"id":"object:player","name":"Player","components":{"Transform":{"position":[0,1e40,0]}}}]})");
        auto nonFinite = project::loadProject(fixture.root / "project.json");
        test::require(!nonFinite, "out-of-range transform values should be rejected");
        requireCode(nonFinite.error(), "project.transform.position");

        fixture.write("scenes/main.json", R"({"schema":1,"scene_id":"scene:main","entities":[{"id":"object:player","name":"Player","components":{"Transform":{"position":[0,1e999,0]}}}]})");
        auto outOfRangeJson = project::loadProject(fixture.root / "project.json");
        test::require(!outOfRangeJson, "JSON numeric values outside the parser range should return diagnostics");
        requireCode(outOfRangeJson.error(), "project.document.malformed");

        fixture.write("project.json", R"({"schema":1,"name":"Fixture","startup_scene":"../escape.json"})");
        fixture.write("escape.json", R"({"schema":1,"scene_id":"scene:outside","entities":[]})");
        auto traversal = project::loadProject(fixture.root / "project.json");
        test::require(!traversal, "project root traversal should be rejected even if target exists");
        requireCode(traversal.error(), "project.path.outside_root");

        fixture.write("project.json", R"({"schema":1,"name":"Fixture","startup_scene":"scenes/main.json","unexpected":true})");
        fixture.write("scenes/main.json", R"({"schema":1,"scene_id":"scene:main","entities":[]})");
        auto unknown = project::loadProject(fixture.root / "project.json");
        test::require(!unknown, "unknown fields should not be silently dropped");
        requireCode(unknown.error(), "project.field.unknown");
    }
}

int main()
{
    try
    {
        validProjectLoadsAndEditHistoryIsRevisioned();
        rejectsMalformedSchemaIdsAndPaths();
        std::cout << "[PASS] project format and validation tests\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
