#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace platform
{
    enum class DiagnosticKind { Requirement, Missing, Unsupported, Unverified };

    struct PlatformDiagnostic
    {
        DiagnosticKind kind;
        std::string code;
        std::string message;
    };

    struct PlatformCheckReport
    {
        std::string target;
        std::string rendererBackend;
        std::string minimumPlatform;
        std::string architecture;
        std::vector<std::string> instanceExtensions;
        std::vector<std::string> deviceExtensions;
        std::string assetStorage;
        std::string lifecycle;
        std::vector<PlatformDiagnostic> diagnostics;

        bool readyToBuild() const;
        std::string toJson() const;
    };

    // Probes the current host and project files; this is build readiness, not a
    // claim that a device or simulator has passed runtime validation.
    PlatformCheckReport checkPlatform(
        std::string_view target,
        const std::filesystem::path& repositoryRoot = "."
    );
}
