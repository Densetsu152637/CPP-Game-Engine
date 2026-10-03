#include "test/test_assertions.h"
#include "tooling/iteration.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace
{
    struct TempDirectory
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("cpp-game-engine-tooling-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        TempDirectory() { std::filesystem::create_directories(path); }
        ~TempDirectory() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    };

    void write(const std::filesystem::path& file, const std::string& value)
    {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream output(file, std::ios::binary);
        output << value;
        test::require(static_cast<bool>(output), "fixture file could not be written");
    }

    std::filesystem::path externalLink(const std::filesystem::path& link, const std::filesystem::path& target)
    {
        std::filesystem::create_directories(link.parent_path());
#ifdef _WIN32
        const DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
        if (CreateSymbolicLinkW(link.c_str(), target.c_str(), flags)) return link;
        const DWORD linkError = GetLastError();
        if (linkError != ERROR_PRIVILEGE_NOT_HELD && linkError != ERROR_INVALID_PARAMETER)
            test::require(false, "security symlink fixture failed unexpectedly (error " + std::to_string(linkError) + ")");

        // Windows may deny file-symlink creation to unprivileged processes. A directory
        // junction exercises the same canonical escape checks without silently skipping coverage.
        const auto junction = link.parent_path() / "external-link";
        const std::wstring command = L"cmd.exe /d /s /c mklink /J \"" + junction.native() + L"\" \"" + target.parent_path().native() + L"\"";
        const int result = _wsystem(command.c_str());
        test::require(result == 0, "security junction fixture could not be created after file symlink permission denial");
        return junction / target.filename();
#else
        std::error_code ec;
        std::filesystem::create_symlink(target, link, ec);
        test::require(!ec, "security symlink fixture could not be created: " + ec.message());
        return link;
#endif
    }

    void externalDirectoryLink(const std::filesystem::path& link, const std::filesystem::path& target)
    {
        std::filesystem::create_directories(link.parent_path());
#ifdef _WIN32
        const DWORD flags = SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
        if (CreateSymbolicLinkW(link.c_str(), target.c_str(), flags)) return;
        const DWORD linkError = GetLastError();
        if (linkError != ERROR_PRIVILEGE_NOT_HELD && linkError != ERROR_INVALID_PARAMETER)
            test::require(false, "cache directory-link fixture failed unexpectedly (error " + std::to_string(linkError) + ")");
        const std::wstring command = L"cmd.exe /d /s /c mklink /J \"" + link.native() + L"\" \"" + target.native() + L"\"";
        test::require(_wsystem(command.c_str()) == 0, "cache directory junction fixture could not be created");
#else
        std::error_code ec;
        std::filesystem::create_directory_symlink(target, link, ec);
        test::require(!ec, "cache directory symlink fixture could not be created: " + ec.message());
#endif
    }
}

void test_asset_index_is_deterministic_tracks_dependencies_and_roundtrips()
{
    TempDirectory temp;
    const auto root = temp.path / "project";
    write(root / "assets/scripts/main.lua", "require('helpers.move')\nreturn {}\n");
    write(root / "assets/scripts/helpers/move.lua", "return {}\n");
    write(root / "assets/shaders/main.vert", "#version 450\n#include \"common.glsl\"\n");
    write(root / "assets/shaders/common.glsl", "const float scale = 1.0;\n");
    const auto aliasedRoot = temp.path / "project-alias";
    externalDirectoryLink(aliasedRoot, root);
    project::Project project;
    // Temp paths on hosted Windows runners may traverse a junction. The public Project
    // struct also permits non-canonical roots, so tooling must normalize before resolving.
    project.root = aliasedRoot;
    project.assets.emplace("asset:main-script", project::Asset{"asset:main-script", "assets/scripts/main.lua", "script"});
    project.assets.emplace("asset:move-module", project::Asset{"asset:move-module", "assets/scripts/helpers/move.lua", "script"});
    project.assets.emplace("asset:main-shader", project::Asset{"asset:main-shader", "assets/shaders/main.vert", "shader"});
    project.assets.emplace("asset:common-shader", project::Asset{"asset:common-shader", "assets/shaders/common.glsl", "shader"});
    auto first = tooling::buildAssetIndex(project);
    auto second = tooling::buildAssetIndex(project);
    test::require(first && second, "asset index should build" + (first ? std::string{} : ": " + first.error().front().code + " " + first.error().front().message));
    test::require(first->assets == second->assets, "asset index must be deterministic");
    auto indexPath = tooling::writeAssetIndex(project, *first);
    test::require(indexPath.has_value(), "asset index should write");
    auto loaded = tooling::readAssetIndex(*indexPath);
    test::require(loaded && loaded->assets == first->assets, "asset index should round-trip" + (loaded ? std::string{} : ": " + loaded.error().front().code + " " + loaded.error().front().message));
    const auto script = std::find_if(first->assets.begin(), first->assets.end(), [](const auto& item) { return item.id == "asset:main-script"; });
    test::require(script != first->assets.end() && script->dependencies.size() == 1, "Lua module dependency should be indexed");
    const auto shader = std::find_if(first->assets.begin(), first->assets.end(), [](const auto& item) { return item.id == "asset:main-shader"; });
    test::require(shader != first->assets.end() && shader->dependencies.size() == 1, "GLSL include dependency should be indexed");

    project::Project renamed = project;
    std::filesystem::rename(root / "assets/scripts/main.lua", root / "assets/scripts/renamed.lua");
    renamed.assets.at("asset:main-script").path = "assets/scripts/renamed.lua";
    const auto afterRename = tooling::buildAssetIndex(renamed);
    test::require(afterRename && std::find_if(afterRename->assets.begin(), afterRename->assets.end(),
        [](const auto& item) { return item.id == "asset:main-script" && item.path.filename() == "renamed.lua"; }) != afterRename->assets.end(),
        "asset ID should remain stable when manifest path changes on rename");
}

void test_asset_index_rejects_shader_include_escape()
{
    TempDirectory temp;
    const auto root = temp.path / "project";
    write(root / "assets/shaders/main.vert", "#version 450\n#include \"../../../outside.glsl\"\n");
    project::Project project;
    project.root = root;
    project.assets.emplace("asset:shader", project::Asset{"asset:shader", "assets/shaders/main.vert", "shader"});
    const auto result = tooling::buildAssetIndex(project);
    test::require(!result && result.error().front().code == "asset.dependency.outside_root",
        "shader dependency outside project should fail with stable diagnostic");
    const auto missingCompiler = tooling::validateShaders(root, temp.path / "missing-glslc.exe");
    test::require(!missingCompiler && missingCompiler.error().front().code == "shader.compiler.missing",
        "missing shader compiler should report a stable prerequisite diagnostic");
}

void test_asset_index_rejects_oversized_lua_source()
{
    TempDirectory temp;
    const auto root = temp.path / "project";
    write(root / "scripts/large.lua", std::string(1024 * 1024 + 1, '-'));
    project::Project project;
    project.root = root;
    project.assets.emplace("asset:large-script", project::Asset{"asset:large-script", "scripts/large.lua", "script"});
    const auto result = tooling::buildAssetIndex(project);
    test::require(!result && result.error().front().code == "asset.script.too_large",
        "asset indexing should reject a Lua source over 1 MiB before reading it");
}

void test_lua_validation_compiles_without_executing_and_stages_only_valid_source()
{
    TempDirectory temp;
    const auto file = temp.path / "script.lua";
    write(file, "error('validation executed Lua')\nreturn {}\n");
    test::require(tooling::validateLua(file).has_value(), "valid Lua must compile without running its top-level code");
    auto candidate = tooling::stageScriptReload(file);
    test::require(candidate && candidate->contents.find("validation executed Lua") != std::string::npos,
        "valid replacement should be available as a staged candidate");
    write(file, "function (\n");
    auto invalid = tooling::stageScriptReload(file);
    test::require(!invalid && invalid.error().front().code == "script.syntax.invalid",
        "invalid replacement should leave the running script untouched and return a syntax diagnostic");
}

void test_project_template_package_and_runtime_lookup_are_root_scoped()
{
    TempDirectory temp;
    const auto templateRoot = temp.path / "template";
    write(templateRoot / "project.json", R"({"schema":1,"name":"Template","startup_scene":"scenes/main.json","assets":[{"id":"asset:main-script","path":"assets/scripts/main.lua","kind":"script"}]})");
    write(templateRoot / "scenes/main.json", R"({"schema":1,"scene_id":"scene:main","entities":[{"id":"entity:player","name":"Player","components":{"Script":{"asset":"asset:main-script"}}}]})");
    write(templateRoot / "assets/scripts/main.lua", "return {}\n");
    const auto initialized = tooling::initializeProject(templateRoot, temp.path / "new-project");
    test::require(initialized && std::filesystem::exists(*initialized / "project.json"), "template should initialize project");
    const auto invalidTemplate = temp.path / "invalid-template";
    write(invalidTemplate / "project.json", R"({"schema":1,"name":"Broken","startup_scene":"missing.json","assets":[]})");
    auto rejected = tooling::initializeProject(invalidTemplate, temp.path / "rejected-project");
    test::require(!rejected && !std::filesystem::exists(temp.path / "rejected-project"),
        "invalid templates must fail before publishing a project destination");
    project::Project project;
    project.root = templateRoot;
    project.manifest = "project.json";
    project.scene.source = "scenes/main.json";
    project.assets.emplace("asset:main-script", project::Asset{"asset:main-script", "assets/scripts/main.lua", "script"});
    write(templateRoot / "assets/scripts/main.lua", "require('helpers.move')\nrequire('scripts.progression')\nrequire('scripts.journal')\nreturn {}\n");
    write(templateRoot / "assets/scripts/helpers/move.lua", "return {}\n");
    write(templateRoot / "scripts/progression.lua", "return {complete = true}\n");
    write(templateRoot / "scripts/journal/init.lua", "require('scripts.progression')\nreturn {}\n");
    project.assets.emplace("asset:root-progression", project::Asset{"asset:root-progression", "scripts/progression.lua", "script"});
    project.assets.emplace("asset:root-journal", project::Asset{"asset:root-journal", "scripts/journal/init.lua", "script"});
    write(templateRoot / "assets/image.png", "PNG transport fixture");
    write(templateRoot / "assets/cue.wav", "WAV transport fixture");
    write(templateRoot / "assets/font.json", "font transport fixture");
    project.assets.emplace("asset:module", project::Asset{"asset:module", "assets/scripts/helpers/move.lua", "script"});
    project.assets.emplace("asset:image", project::Asset{"asset:image", "assets/image.png", "texture"});
    project.assets.emplace("asset:cue", project::Asset{"asset:cue", "assets/cue.wav", "audio"});
    project.assets.emplace("asset:font", project::Asset{"asset:font", "assets/font.json", "font"});
    const auto rootQualifiedIndex = tooling::buildAssetIndex(project);
    test::require(rootQualifiedIndex.has_value(), "root-qualified nested file and init.lua modules index with runtime resolution order");
    auto dependencies = [&](const char* id)
    {
        const auto found = std::find_if(rootQualifiedIndex->assets.begin(), rootQualifiedIndex->assets.end(), [id](const auto& asset) { return asset.id == id; });
        test::require(found != rootQualifiedIndex->assets.end(), "indexed script record must exist"); return found->dependencies;
    };
    test::require(dependencies("asset:main-script") == std::vector<std::string>{"asset:module", "asset:root-journal", "asset:root-progression"}, "root-qualified modules retain declared stable dependency IDs");
    test::require(dependencies("asset:root-journal") == std::vector<std::string>{"asset:root-progression"}, "nested init module can resolve another project-root-qualified module");
    write(templateRoot / "assets/scripts/main.lua", "require('scripts.absent')\nreturn {}\n");
    const auto absentModule = tooling::buildAssetIndex(project);
    test::require(!absentModule && absentModule.error().front().code == "asset.dependency.missing", "absent project-root modules remain explicit failures");
    write(templateRoot / "assets/scripts/main.lua", "require('../outside')\nreturn {}\n");
    const auto traversalModule = tooling::buildAssetIndex(project);
    test::require(!traversalModule && traversalModule.error().front().code == "asset.dependency.outside_root", "invalid traversal module names remain rejected");
    const auto outsideModule = temp.path / "external-module.lua"; write(outsideModule, "return {outside = true}\n");
    const auto moduleLink = externalLink(templateRoot / "assets/scripts/escape-module.lua", outsideModule);
    auto escapedName = moduleLink.lexically_relative(templateRoot / "assets/scripts").generic_string();
    escapedName.resize(escapedName.size() - 4); std::replace(escapedName.begin(), escapedName.end(), '/', '.');
    write(templateRoot / "assets/scripts/main.lua", "require('" + escapedName + "')\nreturn {}\n");
    const auto linkedModule = tooling::buildAssetIndex(project);
    test::require(!linkedModule && linkedModule.error().front().code == "asset.dependency.outside_root", "canonical module candidates reject real symlink/junction escape");
    test::require(!tooling::packageProject(project, temp.path / "escaped-module-package", temp.path / "absent-runtime.exe") && !std::filesystem::exists(temp.path / "escaped-module-package"), "escaping module cannot publish package content");
    if (moduleLink.filename() == "escape-module.lua") std::filesystem::remove(moduleLink);
    else std::filesystem::remove(moduleLink.parent_path());
    write(templateRoot / "assets/scripts/main.lua", "require('helpers.move')\nrequire('scripts.progression')\nrequire('scripts.journal')\nreturn {}\n");
    const auto runtime = temp.path / "engine.exe";
    write(runtime, "engine fixture");
    const auto package = tooling::packageProject(project, temp.path / "package", runtime);
    test::require(package && std::filesystem::exists(*package / "bin" / runtime.filename()), "package should include runnable engine binary");
    auto read = [](const std::filesystem::path& path) { std::ifstream stream(path, std::ios::binary); return std::string(std::istreambuf_iterator<char>(stream), {}); };
    test::require(read(*package / "PACKAGE.txt").find("engine-notices: omitted") != std::string::npos, "old API explicitly marks omitted engine notices");
    for (const auto* asset : {"assets/scripts/helpers/move.lua", "scripts/progression.lua", "scripts/journal/init.lua", "assets/image.png", "assets/cue.wav", "assets/font.json"})
        test::require(read(*package / asset) == read(templateRoot / asset), "declared Lua module and all new asset kinds are preserved byte-for-byte");
    const auto graphicsRuntime = temp.path / "desktop-build/bin/engine.exe";
    write(graphicsRuntime, "graphics engine fixture");
    const auto shaders = graphicsRuntime.parent_path().parent_path() / "shaders";
    const std::string spirv = std::string("\x03\x02\x23\x07", 4) + std::string(16, '\0');
    write(shaders / "mesh_textured.vert.spv", spirv); write(shaders / "mesh_textured.frag.spv", spirv);
    write(shaders / "triangle.vert.spv", spirv); write(shaders / "compiler-secret.txt", "do not export developer files");
    const auto notices = temp.path / "actual-engine";
    for (const auto* source : {"LICENSE", "third_party/entt/LICENSE", "third_party/glfw/LICENSE.md", "third_party/picojson/LICENSE", "third_party/stb/LICENSE"}) write(notices / source, std::string("pinned notice: ") + source);
    write(notices / "third_party/lua/lua.h", "header code\n* Copyright (C) Lua fixture\n* Permission is hereby granted\n* SOFTWARE IS PROVIDED AS IS\n******************************************************************************/\n#endif\n");
    const auto graphical = tooling::packageProject(project, temp.path / "graphical-package", graphicsRuntime, {}, notices);
    test::require(graphical.has_value(), "runtime shader autodiscovery and explicit pinned notices should package");
    test::require(read(*graphical / "shaders/mesh_textured.vert.spv") == spirv && read(*graphical / "shaders/triangle.vert.spv") == spirv, "known compiled shaders copied byte-for-byte");
    test::require(!std::filesystem::exists(*graphical / "shaders/compiler-secret.txt"), "shader directory export is an allowlist");
    for (const auto* destination : {"licenses/engine/LICENSE", "licenses/entt/LICENSE", "licenses/glfw/LICENSE.md", "licenses/picojson/LICENSE", "licenses/stb/LICENSE"}) test::require(read(*graphical / destination).starts_with("pinned notice:"), "package uses actual pinned notice contents");
    const auto luaNotice = read(*graphical / "licenses/lua/LICENSE.txt");
    test::require(luaNotice.find("Copyright (C) Lua fixture") != std::string::npos && luaNotice.find("Permission is hereby granted") != std::string::npos && luaNotice.find("header code") == std::string::npos, "Lua embedded copyright/permission notice extracted without source code");
#ifdef _WIN32
    const auto launcher = read(*graphical / "run.cmd");
    test::require(launcher.find("\"%~dp0bin/engine.exe\"") != std::string::npos && launcher.find("\"%~dp0project.json\"") != std::string::npos && launcher.find("--shaders \"%~dp0shaders\"") != std::string::npos, "launcher passes absolute package-relative binary, manifest and shader paths");
#else
    const auto launcher = read(*graphical / "run.sh");
    test::require(launcher.find("$package_dir/") != std::string::npos && launcher.find("--shaders \"$package_dir/shaders\"") != std::string::npos, "launcher passes absolute package-relative runtime paths");
#endif
    const auto explicitShaders = temp.path / "explicit-shaders";
    write(explicitShaders / "mesh_textured.vert.spv", spirv); write(explicitShaders / "mesh_textured.frag.spv", spirv);
    test::require(tooling::packageProject(project, temp.path / "explicit-package", runtime, explicitShaders, notices).has_value(), "explicit shader source packages outside runtime directory");
    auto missing = tooling::packageProject(project, temp.path / "missing-shaders-package", runtime, temp.path / "missing-shaders", notices);
    test::require(!missing && !std::filesystem::exists(temp.path / "missing-shaders-package"), "explicit missing shaders do not publish a partial package");
    std::filesystem::remove(explicitShaders / "mesh_textured.frag.spv");
    test::require(!tooling::packageProject(project, temp.path / "incomplete-package", runtime, explicitShaders, notices) && !std::filesystem::exists(temp.path / "incomplete-package"), "missing required fragment fails before publication");
    write(explicitShaders / "mesh_textured.frag.spv", "not compiled SPIR-V");
    test::require(!tooling::packageProject(project, temp.path / "invalid-shader-package", runtime, explicitShaders, notices), "invalid compiled shader rejected");
    write(explicitShaders / "mesh_textured.frag.spv", spirv);
    std::filesystem::remove(notices / "third_party/stb/LICENSE");
    test::require(!tooling::packageProject(project, temp.path / "missing-notice-package", runtime, explicitShaders, notices) && !std::filesystem::exists(temp.path / "missing-notice-package"), "missing pinned notice prevents publication");
    write(notices / "third_party/stb/LICENSE", "restored pinned notice");
    project::Project colliding = project;
    write(templateRoot / "licenses/lua/LICENSE.txt", "authored notice sentinel");
    colliding.assets.emplace("asset:notice-collision", project::Asset{"asset:notice-collision", "licenses/lua/LICENSE.txt", "data"});
    test::require(!tooling::packageProject(colliding, temp.path / "collision-package", runtime, explicitShaders, notices) && !std::filesystem::exists(temp.path / "collision-package"), "supplemental notices cannot overwrite declared authored assets");
    const auto externalNotices = temp.path / "external-notices"; write(externalNotices / "LICENSE", "external notice sentinel");
    std::filesystem::remove(notices / "third_party/stb/LICENSE"); std::filesystem::remove(notices / "third_party/stb");
    externalDirectoryLink(notices / "third_party/stb", externalNotices);
    test::require(!tooling::packageProject(project, temp.path / "linked-notice-package", runtime, explicitShaders, notices) && !std::filesystem::exists(temp.path / "linked-notice-package"), "actual regular license file reached through escaping source junction cannot publish");
    std::filesystem::remove(notices / "third_party/stb"); write(notices / "third_party/stb/LICENSE", "restored pinned notice");
    const auto external = temp.path / "outside.spv"; write(external, spirv);
    std::filesystem::remove(explicitShaders / "mesh_textured.frag.spv");
    const auto linkedShader = externalLink(explicitShaders / "mesh_textured.frag.spv", external);
    // If Windows uses a junction fallback, place it at the required filename via the caller's shader directory.
    if (linkedShader != explicitShaders / "mesh_textured.frag.spv")
    {
        std::filesystem::remove(explicitShaders / "external-link");
        externalDirectoryLink(explicitShaders / "mesh_textured.frag.spv", temp.path);
    }
    test::require(!tooling::packageProject(project, temp.path / "linked-shader-package", runtime, explicitShaders, notices) && !std::filesystem::exists(temp.path / "linked-shader-package"), "runtime shader source links cannot escape shader root");
    for (const auto& entry : std::filesystem::directory_iterator(temp.path)) test::require(entry.path().filename().string().find(".staging-") == std::string::npos, "failed supplemental packaging cleans task-owned staging");
    const auto script = tooling::locateRuntimeAsset(*package, "assets/scripts/main.lua");
    test::require(script && std::filesystem::exists(*script), "runtime lookup should resolve package assets");
    auto traversal = tooling::locateRuntimeAsset(*package, "../outside.lua");
    test::require(!traversal && traversal.error().front().code == "runtime.asset.path.outside",
        "runtime asset lookup must reject traversal");
}

void test_symlinked_assets_cannot_escape_project_or_package_roots()
{
    TempDirectory temp;
    const auto root = temp.path / "project";
    const auto sentinel = temp.path / "outside.lua";
    write(sentinel, "return {}\n");
    write(root / "project.json", "{}\n");
    write(root / "scenes/main.json", "{}\n");
    const auto externalAsset = externalLink(root / "assets/scripts/escape.lua", sentinel);
    test::require(std::filesystem::is_regular_file(externalAsset), "external asset link fixture should resolve to sentinel file");
    project::Project project;
    project.root = root;
    project.manifest = "project.json";
    project.scene.source = "scenes/main.json";
    project.assets.emplace("asset:escape", project::Asset{"asset:escape", externalAsset.lexically_relative(root), "script"});
    auto index = tooling::buildAssetIndex(project);
    test::require(!index && !index.error().empty(),
        "asset index must reject a symlink to content outside the project");
    const auto runtime = temp.path / "engine.exe";
    write(runtime, "engine fixture");
    const auto output = temp.path / "package";
    auto packaged = tooling::packageProject(project, output, runtime);
    test::require(!packaged && !std::filesystem::exists(output), "package must fail without publishing an external symlink target");

    const auto packageRoot = temp.path / "runtime-package";
    write(packageRoot / "assets/scripts/placeholder.lua", "return {}\n");
    const auto externalRuntimeAsset = externalLink(packageRoot / "assets/scripts/linked.lua", sentinel);
    auto resolved = tooling::locateRuntimeAsset(packageRoot, externalRuntimeAsset.lexically_relative(packageRoot));
    test::require(!resolved && resolved.error().front().code == "runtime.asset.path.outside",
        "runtime lookup must reject a symlink escaping its package root");
}

void test_derived_cache_rejects_external_directory_links()
{
    TempDirectory temp;
    const auto root = temp.path / "project";
    const auto outside = temp.path / "outside-cache";
    write(root / "scripts/player.lua", "return {}\n");
    std::filesystem::create_directories(outside);
    project::Project project;
    project.root = root;
    project.assets.emplace("asset:player", project::Asset{"asset:player", "scripts/player.lua", "script"});
    const auto index = tooling::buildAssetIndex(project);
    test::require(index.has_value(), "fixture asset should index before cache redirection");
    externalDirectoryLink(root / ".derived", outside);
    const auto written = tooling::writeAssetIndex(project, *index);
    test::require(!written && written.error().front().code == "derived.root.outside_root",
        "asset index must reject an external derived-directory link");
    test::require(!std::filesystem::exists(outside / "asset-index.json"), "asset index must not write through a cache link");

    write(root / "assets/shaders/test.vert", "#version 450\nvoid main() {}\n");
    const auto compiler = temp.path / "glslc.exe";
    write(compiler, "unused compiler fixture");
    const auto imported = tooling::importShaders(root, compiler);
    test::require(!imported && imported.error().front().code == "derived.root.outside_root",
        "shader import must reject an external derived-directory link");
    test::require(!std::filesystem::exists(outside / "shaders"), "shader importer must not create cache data outside project");
}
