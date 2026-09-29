#include "platform_check.h"

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <cstdlib>
#include <sstream>

namespace platform
{
    namespace
    {
        std::string jsonEscape(std::string_view value)
        {
            std::string result;
            for (const char c : value)
            {
                switch (c)
                {
                    case '"': result += "\\\""; break;
                    case '\\': result += "\\\\"; break;
                    case '\n': result += "\\n"; break;
                    case '\r': result += "\\r"; break;
                    case '\t': result += "\\t"; break;
                    default: result += c;
                }
            }
            return result;
        }

        const char* kindName(DiagnosticKind kind)
        {
            switch (kind)
            {
                case DiagnosticKind::Requirement: return "requirement";
                case DiagnosticKind::Missing: return "missing";
                case DiagnosticKind::Unsupported: return "unsupported";
                case DiagnosticKind::Unverified: return "unverified";
            }
            return "unsupported";
        }

        bool hasEnvironmentPath(const char* name)
        {
            const char* value = std::getenv(name);
            return value && *value && std::filesystem::exists(value);
        }

        bool hasCommand(std::string_view name)
        {
            const char* path = std::getenv("PATH");
            if (!path) return false;
#ifdef _WIN32
            constexpr char separator = ';';
            const std::string executable = std::string(name) + ".exe";
#else
            constexpr char separator = ':';
            const std::string executable(name);
#endif
            std::stringstream paths(path);
            std::string part;
            while (std::getline(paths, part, separator))
                if (!part.empty() && std::filesystem::exists(std::filesystem::path(part) / executable))
                    return true;
            return false;
        }

        void requireFile(PlatformCheckReport& report, const std::filesystem::path& root,
                         const char* path)
        {
            if (!std::filesystem::is_regular_file(root / path))
                report.diagnostics.push_back({DiagnosticKind::Missing, "project_file", std::string("Missing project file: ") + path});
        }
    }

    bool PlatformCheckReport::readyToBuild() const
    {
        for (const auto& diagnostic : diagnostics)
            if (diagnostic.kind == DiagnosticKind::Missing || diagnostic.kind == DiagnosticKind::Unsupported)
                return false;
        return true;
    }

    std::string PlatformCheckReport::toJson() const
    {
        std::string result = "{\"target\":\"" + jsonEscape(target) + "\",\"readyToBuild\":" +
            (readyToBuild() ? "true" : "false") +
            ",\"rendererBackend\":\"" + jsonEscape(rendererBackend) +
            "\",\"minimumPlatform\":\"" + jsonEscape(minimumPlatform) +
            "\",\"architecture\":\"" + jsonEscape(architecture) +
            "\",\"assetStorage\":\"" + jsonEscape(assetStorage) +
            "\",\"lifecycle\":\"" + jsonEscape(lifecycle) +
            "\",\"instanceExtensions\":[";
        for (size_t i = 0; i < instanceExtensions.size(); ++i)
        {
            if (i) result += ',';
            result += "\"" + jsonEscape(instanceExtensions[i]) + "\"";
        }
        result += "],\"deviceExtensions\":[";
        for (size_t i = 0; i < deviceExtensions.size(); ++i)
        {
            if (i) result += ',';
            result += "\"" + jsonEscape(deviceExtensions[i]) + "\"";
        }
        result += "],\"diagnostics\":[";
        for (size_t i = 0; i < diagnostics.size(); ++i)
        {
            if (i) result += ',';
            const auto& diagnostic = diagnostics[i];
            result += "{\"kind\":\"" + std::string(kindName(diagnostic.kind)) +
                "\",\"code\":\"" + jsonEscape(diagnostic.code) +
                "\",\"message\":\"" + jsonEscape(diagnostic.message) + "\"}";
        }
        return result + "]}";
    }

    PlatformCheckReport checkPlatform(std::string_view target,
                                      const std::filesystem::path& repositoryRoot)
    {
        PlatformCheckReport report;
        report.target = std::string(target);
        const auto add = [&](DiagnosticKind kind, const char* code, const char* message)
        { report.diagnostics.push_back({kind, code, message}); };
        if (target != "android" && target != "ios")
        {
            add(DiagnosticKind::Unsupported, "target", "Target must be android or ios");
            return report;
        }

        requireFile(report, repositoryRoot, "mobile/CMakeLists.txt");
        requireFile(report, repositoryRoot, "mobile/platform-support.json");
        if (!hasCommand("cmake")) add(DiagnosticKind::Missing, "cmake", "CMake is not on PATH");

        if (target == "android")
        {
            report.rendererBackend = "Vulkan via SDL3 Android surface";
            report.minimumPlatform = "Android API 24";
            report.architecture = "arm64-v8a and x86_64 emulator";
            report.instanceExtensions = {"VK_KHR_surface", "VK_KHR_android_surface"};
            report.deviceExtensions = {"VK_KHR_swapchain"};
            report.assetStorage = "APK assets extracted to SDL_GetPrefPath private storage";
            report.lifecycle = "SDL background/foreground event watch pauses render and rebuilds surface";
            requireFile(report, repositoryRoot, "mobile/android/app/build.gradle");
            if (!hasEnvironmentPath("ANDROID_HOME") && !hasEnvironmentPath("ANDROID_SDK_ROOT"))
                add(DiagnosticKind::Missing, "android_sdk", "Set ANDROID_HOME to an installed Android SDK");
            if (!hasEnvironmentPath("ANDROID_NDK_HOME") && !hasEnvironmentPath("ANDROID_NDK_ROOT"))
                add(DiagnosticKind::Missing, "android_ndk", "Set ANDROID_NDK_HOME to an installed Android NDK");
            add(DiagnosticKind::Requirement, "platform", "Android API 24+, arm64-v8a or x86_64 emulator, Vulkan-capable GPU required");
        }
        else
        {
            report.rendererBackend = "Vulkan via SDL3 Metal surface and MoltenVK";
            report.minimumPlatform = "iOS 16.3";
            report.architecture = "arm64 device/simulator";
            report.instanceExtensions = {"VK_KHR_surface", "VK_EXT_metal_surface", "VK_KHR_portability_enumeration"};
            report.deviceExtensions = {"VK_KHR_swapchain", "VK_KHR_portability_subset when advertised"};
            report.assetStorage = "Bundle resources extracted to SDL_GetPrefPath private storage";
            report.lifecycle = "SDL background/foreground event watch pauses render and rebuilds surface";
            requireFile(report, repositoryRoot, "mobile/ios/build.sh");
#ifndef __APPLE__
            add(DiagnosticKind::Unsupported, "host", "iOS builds require a macOS host with Xcode");
#else
            if (!hasCommand("xcodebuild"))
                add(DiagnosticKind::Missing, "xcode", "Xcode command line tools are not on PATH");
#endif
            if (!hasEnvironmentPath("VULKAN_SDK"))
                add(DiagnosticKind::Missing, "vulkan_sdk", "Set VULKAN_SDK to a Vulkan SDK with iOS MoltenVK frameworks");
            add(DiagnosticKind::Requirement, "platform", "iOS 16.3+, arm64, Metal-capable device required");
        }

        add(DiagnosticKind::Unverified, "runtime", "Host readiness does not verify launch, surface presentation, or device GPU support");
        return report;
    }
}
