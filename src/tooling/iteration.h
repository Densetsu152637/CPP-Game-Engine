#pragma once

#include "project/project.h"
#include "project/runtime.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace tooling
{
    project::Result<std::filesystem::path> initializeProject(const std::filesystem::path& templateRoot,
        const std::filesystem::path& destination);

    struct AssetRecord
    {
        std::string id;
        std::filesystem::path path;
        std::string kind;
        std::string contentHash;
        std::vector<std::string> dependencies;
        bool operator==(const AssetRecord&) const = default;
    };

    struct AssetIndex
    {
        unsigned schema = 1;
        std::vector<AssetRecord> assets;
    };

    struct StagedScript
    {
        std::filesystem::path source;
        std::string contents;
        std::string contentHash;
    };

    // Indexes manifest-declared assets. IDs are durable manifest identities; paths and
    // content hashes are changing metadata and can be updated on rename/reimport.
    project::Result<AssetIndex> buildAssetIndex(const project::Project& project);
    project::Result<std::string> writeAssetIndex(const project::Project& project, const AssetIndex& index);
    project::Result<AssetIndex> readAssetIndex(const std::filesystem::path& file);

    // Lua chunks are compiled without execution. This does not run lifecycle callbacks.
    project::Result<void> validateLua(const std::filesystem::path& file);
    project::Result<StagedScript> stageScriptReload(const std::filesystem::path& file);
    project::Result<void> stageRuntimeScriptReload(project::Runtime& runtime, std::string_view authoredEntityId,
        const std::filesystem::path& file);
    project::Result<void> stageRuntimeScriptReload(project::Runtime& runtime, const project::Project& project,
        std::string_view authoredEntityId, std::string_view assetId);

    // Compile each GLSL source using glslc into a temporary directory. The compiler
    // is supplied by the caller (usually the Vulkan SDK); no cached output is changed.
    project::Result<void> validateShaders(const std::filesystem::path& projectRoot,
        const std::filesystem::path& compiler);
    project::Result<std::filesystem::path> importShaders(const std::filesystem::path& projectRoot,
        const std::filesystem::path& compiler);

    // Export source assets and project files into a self-contained runtime folder.
    // The runtime is copied as supplied. An empty shaderDirectory discovers adjacent
    // build shaders; absence leaves a headless package. Explicit shader/license roots
    // are required when supplied. licenseRoot is the actual engine source root;
    // omission is recorded in PACKAGE.txt. This does not sandbox authored code.
    project::Result<std::filesystem::path> packageProject(const project::Project& project,
        const std::filesystem::path& destination,
        const std::filesystem::path& runtimeExecutable = {},
        const std::filesystem::path& shaderDirectory = {},
        const std::filesystem::path& licenseRoot = {});
    project::Result<std::filesystem::path> locateRuntimeAsset(const std::filesystem::path& packageRoot,
        const std::filesystem::path& projectRelativeAsset);
}
