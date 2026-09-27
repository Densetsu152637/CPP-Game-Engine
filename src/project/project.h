#pragma once

#include <array>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace project
{
    inline constexpr unsigned ProjectSchemaVersion = 1;
    inline constexpr unsigned SceneSchemaVersion = 1;

    enum class Severity
    {
        Error,
        Warning
    };

    struct Diagnostic
    {
        std::string code;
        Severity severity = Severity::Error;
        std::filesystem::path file;
        std::string path;
        std::string message;
    };

    using Diagnostics = std::vector<Diagnostic>;

    struct Transform
    {
        std::array<float, 3> position{};
        bool operator==(const Transform&) const = default;
    };

    struct Script
    {
        std::filesystem::path asset;
        bool operator==(const Script&) const = default;
    };

    struct SceneEntity
    {
        std::string id;
        std::string name;
        std::optional<Transform> transform;
        std::optional<Script> script;
        bool operator==(const SceneEntity&) const = default;
    };

    struct Scene
    {
        unsigned schema = SceneSchemaVersion;
        std::string id;
        std::vector<SceneEntity> entities;
        std::filesystem::path source;
        bool operator==(const Scene&) const = default;
    };

    struct Project
    {
        unsigned schema = ProjectSchemaVersion;
        std::string name;
        std::filesystem::path root;
        std::filesystem::path manifest;
        std::filesystem::path startupScene;
        Scene scene;
    };

    template <typename T>
    using Result = std::expected<T, Diagnostics>;

    struct SetPosition
    {
        std::string entityId;
        std::array<float, 3> position{};
    };

    struct SetScript
    {
        std::string entityId;
        std::optional<std::filesystem::path> asset;
    };

    using EditOperation = std::variant<SetPosition, SetScript>;

    class SceneSession
    {
        struct Impl;
        std::unique_ptr<Impl> m_impl;
        std::optional<Scene> m_playSnapshot;

    public:
        explicit SceneSession(Scene scene, std::filesystem::path projectRoot);
        ~SceneSession();
        SceneSession(SceneSession&&) noexcept;
        SceneSession& operator=(SceneSession&&) noexcept;
        SceneSession(const SceneSession&) = delete;
        SceneSession& operator=(const SceneSession&) = delete;

        const Scene& snapshot() const;
        const std::string& revision() const;
        Result<std::string> apply(const EditOperation& operation, const std::string& expectedRevision);
        Result<std::string> undo(const std::string& expectedRevision);
        Result<std::string> redo(const std::string& expectedRevision);
        Result<std::string> save(const std::string& expectedRevision);
        Result<void> beginPlay(const std::string& expectedRevision);
        Result<void> applyPlay(const EditOperation& operation);
        const Scene* playSnapshot() const noexcept;
        void endPlay() noexcept;
    };

    Result<Project> loadProject(const std::filesystem::path& manifest);
    Result<Scene> loadScene(const std::filesystem::path& sceneFile, const std::filesystem::path& projectRoot);
    Result<void> validateProject(const std::filesystem::path& manifest);
    Result<Scene> inspectScene(const std::filesystem::path& sceneFile, const std::filesystem::path& projectRoot);

    std::string diagnosticsJson(const Diagnostics& diagnostics);
    std::string sceneJson(const Scene& scene, const std::string& revision = {});
    std::string resultJson(bool ok, std::string_view command, std::string_view payload = {});
    std::string revisionFor(const Scene& scene);
}
