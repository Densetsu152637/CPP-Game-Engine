#pragma once

#include "../project/project.h"
#include "../project/runtime.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <array>

namespace editor
{
    // Owns the editor's authoring session. Runtime play state is kept by the
    // project service and never replaces this editable scene snapshot.
    class Document
    {
        std::filesystem::path m_manifest;
        std::optional<project::Project> m_project;
        std::unique_ptr<project::SceneSession> m_session;
        project::Diagnostics m_diagnostics;
        std::string m_status;
        std::string m_savedRevision;
        std::unique_ptr<project::Runtime> m_runtime;

        bool replaceFromManifest(const std::filesystem::path& manifest);

    public:
        ~Document();
        bool open(const std::filesystem::path& manifest, bool discardUnsaved = false);
        bool reload(bool discardUnsaved = false);
        bool save();
        bool apply(const project::EditOperation& operation);
        bool undo();
        bool redo();
        bool play();
        bool stop();
        bool tick();

        const project::Project* project() const;
        project::SceneSession* session() const;
        const project::Diagnostics& diagnostics() const;
        const std::string& status() const;
        bool isPlaying() const;
        bool isDirty() const;
        std::optional<std::array<float, 3>> playPosition(std::string_view authoredEntityId) const;
    };

    // Opens the native editor UI. On platforms without a native implementation,
    // prints an explicit unsupported-platform diagnostic and returns nonzero.
    int run(const std::filesystem::path& manifest);
}
