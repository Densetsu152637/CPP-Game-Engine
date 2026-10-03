#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <map>
#include <optional>
#include <span>
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
        std::string asset;
        bool operator==(const Script&) const = default;
    };

    struct MeshRenderer
    {
        std::string mesh;
        std::optional<std::string> texture;
        bool operator==(const MeshRenderer&) const = default;
    };

    // Atlas rectangles use top-left texture pixel coordinates, not world units.
    using PixelRectangle = std::array<std::uint32_t, 4>;

    struct SpriteRenderer
    {
        std::string texture;
        std::array<float, 2> size{1.0f, 1.0f};
        std::array<float, 2> pivot{0.5f, 0.5f};
        std::array<float, 4> tint{1.0f, 1.0f, 1.0f, 1.0f};
        std::optional<PixelRectangle> source;
        std::vector<PixelRectangle> frames;
        float framesPerSecond = 0.0f;
        bool loop = true;
        int layer = 0;
        bool visible = true;
        bool operator==(const SpriteRenderer&) const = default;
    };

    struct Camera2D
    {
        std::array<std::uint32_t, 2> logicalSize{320, 180};
        float pixelsPerUnit = 16.0f;
        std::array<float, 2> center{};
        std::optional<std::string> follow;
        bool pixelSnap = true;
        bool operator==(const Camera2D&) const = default;
    };

    struct Collider2D
    {
        std::array<float, 2> size{1.0f, 1.0f};
        std::array<float, 2> offset{};
        bool trigger = false;
        std::uint32_t layer = 1;
        std::uint32_t mask = UINT32_MAX;
        bool operator==(const Collider2D&) const = default;
    };

    struct Asset
    {
        std::string id;
        std::filesystem::path path;
        std::string kind;
        bool operator==(const Asset&) const = default;
    };

    struct SceneEntity
    {
        std::string id;
        std::string name;
        std::optional<Transform> transform;
        std::optional<Script> script;
        std::optional<MeshRenderer> meshRenderer;
        std::optional<SpriteRenderer> spriteRenderer;
        std::optional<Camera2D> camera2D;
        std::optional<Collider2D> collider2D;
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
        std::map<std::string, Asset, std::less<>> assets;
        std::map<std::string, std::string, std::less<>> inputActions;
        std::map<std::string, std::vector<std::string>, std::less<>> inputBindings;
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
        std::optional<std::string> asset;
    };

    struct SetMeshRenderer
    {
        std::string entityId;
        std::optional<MeshRenderer> value;
    };

    using EditOperation = std::variant<SetPosition, SetScript, SetMeshRenderer>;

    enum class PropertyType
    {
        Float3,
        AssetId,
        OptionalAssetId,
        Float2,
        Float4,
        Float,
        Integer,
        Boolean,
        UInt2,
        Rectangle,
        RectangleArray,
        OptionalEntityId
    };

    struct PropertyDescriptor
    {
        std::string_view name;
        PropertyType type;
        std::string_view defaultValue;
        bool required;
    };

    struct ComponentDescriptor
    {
        std::string_view serializedName;
        unsigned schemaVersion;
        std::span<const PropertyDescriptor> properties;
    };

    std::span<const ComponentDescriptor> componentDescriptors() noexcept;

    enum class SaveFailureInjection
    {
        None,
        Serialization,
        Replace
    };

    class SceneSession
    {
        // A document and its edits belong to the thread that constructed it.
        struct Impl;
        std::unique_ptr<Impl> m_impl;
        std::optional<Scene> m_playSnapshot;

    public:
        explicit SceneSession(Scene scene, std::filesystem::path projectRoot);
        explicit SceneSession(const Project& project);
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
        Result<std::string> save(const std::string& expectedRevision, SaveFailureInjection injectFailure = SaveFailureInjection::None);
        Result<void> beginPlay(const std::string& expectedRevision);
        Result<void> applyPlay(const EditOperation& operation);
        const Scene* playSnapshot() const noexcept;
        Result<void> endPlay();
    };

    Result<Project> loadProject(const std::filesystem::path& manifest);
    Result<Scene> loadScene(const std::filesystem::path& sceneFile, const std::filesystem::path& projectRoot);
    Result<Scene> loadScene(const std::filesystem::path& sceneFile, const Project& project);
    Result<std::filesystem::path> resolveAsset(const Project& project, std::string_view assetId);
    Result<void> validateProject(const std::filesystem::path& manifest);
    Result<Scene> inspectScene(const std::filesystem::path& sceneFile, const std::filesystem::path& projectRoot);
    Result<Scene> inspectScene(const std::filesystem::path& sceneFile, const Project& project);

    std::string diagnosticsJson(const Diagnostics& diagnostics);
    std::string sceneJson(const Scene& scene, const std::string& revision = {});
    std::string resultJson(bool ok, std::string_view command, std::string_view payload = {});
    std::string revisionFor(const Scene& scene);
}
