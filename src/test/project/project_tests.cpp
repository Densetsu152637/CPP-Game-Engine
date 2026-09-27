#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include "project/project.h"
#include "test/test_assertions.h"

namespace
{
    struct DirectoryLink
    {
        std::filesystem::path link;
        DirectoryLink(std::filesystem::path linkPath, const std::filesystem::path& target) : link(std::move(linkPath))
        {
            std::filesystem::create_directories(link.parent_path());
#ifdef _WIN32
            if (CreateSymbolicLinkW(link.c_str(), target.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) return;
            const auto error = GetLastError();
            if (error != ERROR_PRIVILEGE_NOT_HELD && error != ERROR_INVALID_PARAMETER)
                test::require(false, "project root alias creation failed unexpectedly: " + std::to_string(error));
            const std::wstring command = L"cmd.exe /d /s /c mklink /J \"" + link.native() + L"\" \"" + target.native() + L"\"";
            test::require(_wsystem(command.c_str()) == 0, "project directory junction could not be created");
#else
            std::error_code ec;
            std::filesystem::create_directory_symlink(target, link, ec);
            test::require(!ec, "project directory symlink could not be created: " + ec.message());
#endif
        }
        ~DirectoryLink() { std::error_code ignored; std::filesystem::remove(link, ignored); }
    };

    struct TemporaryProject
    {
        std::filesystem::path root;

        TemporaryProject()
        {
            const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
            root = std::filesystem::temp_directory_path() / ("cpp-game-engine-project-test-" + std::to_string(nonce));
            std::filesystem::create_directories(root / "scenes");
            std::filesystem::create_directories(root / "scripts");
            std::filesystem::create_directories(root / "assets");
            write("project.json", R"({"schema":1,"name":"Fixture","startup_scene":"scenes/main.json","assets":[{"id":"asset:player-script","path":"scripts/player.lua","kind":"script"},{"id":"asset:player-mesh","path":"assets/player.mesh","kind":"mesh"},{"id":"asset:player-texture","path":"assets/player.ppm","kind":"texture"}]})");
            write("scenes/main.json", R"({"schema":1,"scene_id":"scene:main","entities":[{"id":"object:player","name":"Player","components":{"Transform":{"position":[0,0,0]},"Script":{"asset":"asset:player-script"},"MeshRenderer":{"mesh":"asset:player-mesh","texture":"asset:player-texture"}}}]})");
            write("scripts/player.lua", "return {}\n");
            write("assets/player.mesh", "mesh test\n");
            write("assets/player.ppm", "P3\n1 1\n255\n0 0 0\n");
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
        test::require(loaded->scene.entities[0].script == project::Script{"asset:player-script"},
            "script reference should store stable catalog ID");
        test::require(loaded->scene.entities[0].meshRenderer == project::MeshRenderer{"asset:player-mesh", "asset:player-texture"},
            "mesh renderer should preserve stable mesh and texture IDs");
        test::require(project::resolveAsset(*loaded, "asset:player-script").has_value(), "declared asset ID should resolve");

        project::SceneSession session(*loaded);
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
        const auto meshRevision = session.revision();
        test::require(session.apply(project::SetMeshRenderer{"object:player", project::MeshRenderer{"asset:player-mesh", std::nullopt}}, meshRevision).has_value(),
            "typed mesh renderer edit should apply catalog IDs");
        test::require(!session.snapshot().entities[0].meshRenderer->texture, "mesh renderer edit should update the optional texture");
        test::require(session.undo(session.revision()).has_value(), "mesh renderer edit should be undoable");
        test::require(session.snapshot().entities[0].meshRenderer->texture == "asset:player-texture", "undo should restore previous renderer values");

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
        test::require(session.endPlay().has_value(), "stopping play should discard the runtime snapshot");
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
        test::require(std::any_of(nonFinite.error().begin(), nonFinite.error().end(), [](const project::Diagnostic& diagnostic)
        {
            return diagnostic.code == "project.transform.position" &&
                diagnostic.file == std::filesystem::path("scenes/main.json") &&
                diagnostic.path == "entities[0].components.Transform.position[1]";
        }), "transform diagnostic should identify its source file and exact field path");

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

    void stableCatalogAndSessionConflictsAreEnforced()
    {
        TemporaryProject fixture;
        auto loaded = project::loadProject(fixture.root / "project.json");
        test::require(loaded.has_value(), "project with a declared asset should load");
        fixture.write("scripts/renamed.lua", "return {}\n");
        fixture.write("project.json", R"({"schema":1,"name":"Fixture","startup_scene":"scenes/main.json","assets":[{"id":"asset:player-script","path":"scripts/renamed.lua","kind":"script"},{"id":"asset:player-mesh","path":"assets/player.mesh","kind":"mesh"},{"id":"asset:player-texture","path":"assets/player.ppm","kind":"texture"}]})");
        auto moved = project::loadProject(fixture.root / "project.json");
        test::require(moved && moved->scene.entities[0].script == project::Script{"asset:player-script"}, "asset move should preserve scene ID");
        test::require(project::resolveAsset(*moved, "asset:player-script").has_value(), "stable ID should resolve updated path");

        fixture.write("project.json", R"({"schema":1,"name":"Fixture","startup_scene":"scenes/main.json","assets":[{"id":"asset:player-script","path":"scripts/renamed.lua","kind":"script"},{"id":"ASSET:PLAYER-SCRIPT","path":"scripts/player.lua","kind":"script"}]})");
        auto duplicate = project::loadProject(fixture.root / "project.json");
        test::require(!duplicate, "case-insensitive duplicate asset IDs should fail");
        requireCode(duplicate.error(), "project.asset.id.duplicate");

        fixture.write("project.json", R"({"schema":1,"name":"Fixture","startup_scene":"scenes/main.json","assets":[]})");
        auto missing = project::loadProject(fixture.root / "project.json");
        test::require(!missing, "scene reference without catalog entry should fail");
        requireCode(missing.error(), "project.asset.unknown");

        fixture.write("project.json", R"({"schema":1,"name":"Fixture","startup_scene":"scenes/main.json","assets":[{"id":"asset:player-script","path":"scripts/player.lua","kind":"script"},{"id":"asset:player-mesh","path":"assets/player.mesh","kind":"script"}]})");
        auto wrongKind = project::loadProject(fixture.root / "project.json");
        test::require(!wrongKind, "MeshRenderer may not reference a script asset as a mesh");
        requireCode(wrongKind.error(), "project.asset.kind.mismatch");

        fixture.write("project.json", R"({"schema":1,"name":"Fixture","startup_scene":"scenes/main.json","assets":[{"id":"asset:player-script","path":"scripts/player.lua","kind":"script"},{"id":"asset:player-mesh","path":"assets/player.mesh","kind":"mesh"},{"id":"asset:player-texture","path":"assets/player.ppm","kind":"texture"}]})");
        loaded = project::loadProject(fixture.root / "project.json");
        test::require(loaded.has_value(), "fixture should reload");
        project::SceneSession session(*loaded);
        const auto revision = session.revision();
        bool rejected = false;
        std::thread worker([&]
        {
            auto result = session.apply(project::SetPosition{"object:player", {1, 2, 3}}, revision);
            rejected = !result && std::any_of(result.error().begin(), result.error().end(), [](const auto& d) { return d.code == "project.thread.owner"; });
        });
        worker.join();
        test::require(rejected, "cross-thread mutation should have owner-thread diagnostic");
        auto edited = session.apply(project::SetPosition{"object:player", {1, 2, 3}}, session.revision());
        test::require(edited.has_value(), "owner-thread edit should work");
        fixture.write("scenes/main.json", R"({"schema":1,"scene_id":"scene:main","entities":[{"id":"object:player","name":"Player","components":{"Transform":{"position":[9,0,0]},"Script":{"asset":"asset:player-script"}}}]})");
        auto save = session.save(*edited);
        test::require(!save, "external disk edit should prevent save");
        requireCode(save.error(), "scene.revision.stale");
    }

    void sharedResolversCanonicalizeAliasedRootsAndRejectEscapingLinks()
    {
        TemporaryProject fixture;
        auto loaded = project::loadProject(fixture.root / "project.json");
        test::require(loaded.has_value(), "base fixture should load before alias checks");

        const auto aliasPath = fixture.root.parent_path() / (fixture.root.filename().string() + "-alias");
        DirectoryLink rootAlias(aliasPath, fixture.root);
        auto aliasedProject = *loaded;
        aliasedProject.root = aliasPath;
        auto script = project::resolveAsset(aliasedProject, "asset:player-script");
        test::require(script && *script == std::filesystem::canonical(fixture.root / "scripts/player.lua"),
            "asset resolver should canonicalize a junction-backed project root");
        auto scene = project::loadScene(aliasPath / "scenes/main.json", aliasedProject);
        test::require(scene && scene->entities.size() == 1,
            "project scene loader should validate assets from a junction-backed root");
        auto missingCatalog = aliasedProject;
        missingCatalog.assets.erase("asset:player-script");
        auto missing = project::loadScene(aliasPath / "scenes/main.json", missingCatalog);
        test::require(!missing, "project scene loader should reject missing references through an aliased root");
        requireCode(missing.error(), "project.asset.unknown");

        TemporaryProject outside;
        const auto linkPath = fixture.root / "external-assets";
        DirectoryLink externalLink(linkPath, outside.root);
        outside.write("secret.lua", "return {}\n");
        aliasedProject.assets.emplace("asset:escape", project::Asset{"asset:escape", "external-assets/secret.lua", "script"});
        auto escaped = project::resolveAsset(aliasedProject, "asset:escape");
        test::require(!escaped, "canonical root normalization must not allow an asset symlink outside the project");
        requireCode(escaped.error(), "project.path.outside_root");
        fixture.write("scenes/main.json", R"({"schema":1,"scene_id":"scene:main","entities":[{"id":"object:player","name":"Player","components":{"Script":{"asset":"asset:escape"}}}]})");
        auto escapedScene = project::loadScene(aliasPath / "scenes/main.json", aliasedProject);
        test::require(!escapedScene, "project scene loader must reject an escaping asset link through an aliased root");
        requireCode(escapedScene.error(), "project.path.outside_root");
    }

    void componentRegistryDrivesStableEditorMetadataAndSaveFailuresPreserveBytes()
    {
        const auto descriptors = project::componentDescriptors();
        test::require(descriptors.size() == 3, "registry should expose the three authored component types");
        test::require(descriptors[0].serializedName == "Transform" && descriptors[0].schemaVersion == 1 &&
            descriptors[0].properties.size() == 1 && descriptors[0].properties[0].name == "position" &&
            descriptors[0].properties[0].defaultValue == "[0,0,0]", "Transform descriptor should expose stable serialized metadata");
        test::require(descriptors[1].serializedName == "Script" && descriptors[1].schemaVersion == 1 &&
            descriptors[1].properties[0].type == project::PropertyType::AssetId, "Script descriptor should expose asset ID metadata");
        test::require(descriptors[2].serializedName == "MeshRenderer" && descriptors[2].schemaVersion == 1 &&
            descriptors[2].properties.size() == 2 && descriptors[2].properties[1].type == project::PropertyType::OptionalAssetId &&
            !descriptors[2].properties[1].required, "MeshRenderer descriptor should expose optional texture metadata");

        TemporaryProject fixture;
        auto loaded = project::loadProject(fixture.root / "project.json");
        test::require(loaded.has_value(), "fixture should load for save fault tests");
        project::SceneSession session(*loaded);
        auto changed = session.apply(project::SetPosition{"object:player", {2, 3, 4}}, session.revision());
        test::require(changed.has_value(), "fixture edit should succeed");
        auto readScene = [&]
        {
            std::ifstream input(fixture.root / "scenes/main.json", std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        };
        const auto original = readScene();
        auto serializeFailure = session.save(*changed, project::SaveFailureInjection::Serialization);
        test::require(!serializeFailure, "injected serializer failure should be reported");
        requireCode(serializeFailure.error(), "scene.save.serialize");
        test::require(readScene() == original, "serializer failure must preserve original scene bytes");
        auto replaceFailure = session.save(*changed, project::SaveFailureInjection::Replace);
        test::require(!replaceFailure, "injected replacement failure should be reported");
        requireCode(replaceFailure.error(), "scene.save.replace");
        test::require(readScene() == original, "replace failure must preserve original scene bytes");
        test::require(!std::filesystem::exists(fixture.root / "scenes/main.json.tmp"), "failed replacement should clean its temporary file");
        test::require(session.save(*changed).has_value(), "save should succeed after injected failures are removed");
        test::require(readScene() != original, "successful retry should commit the edited scene");
    }
}

int main()
{
    try
    {
        validProjectLoadsAndEditHistoryIsRevisioned();
        rejectsMalformedSchemaIdsAndPaths();
        stableCatalogAndSessionConflictsAreEnforced();
        sharedResolversCanonicalizeAliasedRootsAndRejectEscapingLinks();
        componentRegistryDrivesStableEditorMetadataAndSaveFailuresPreserveBytes();
        std::cout << "[PASS] project format and validation tests\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
