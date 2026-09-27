#pragma once

#include "project.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace project
{
    struct InputSnapshot
    {
        std::set<std::string> pressed;
        std::set<std::string> held;
        std::set<std::string> released;
    };

    struct RuntimeOptions
    {
        float fixedDeltaSeconds = 1.0f / 60.0f;
        std::set<std::string> allowedActions;
        std::function<void(std::string_view)> log;
    };

    class Runtime
    {
        struct Impl;
        std::unique_ptr<Impl> m_impl;

    public:
        explicit Runtime(Project project, RuntimeOptions options = {});
        ~Runtime();
        Runtime(Runtime&&) noexcept;
        Runtime& operator=(Runtime&&) noexcept;
        Runtime(const Runtime&) = delete;
        Runtime& operator=(const Runtime&) = delete;

        Result<void> start();
        Result<void> tick(const InputSnapshot& input = {});
        Result<void> stageScriptReload(std::string authoredEntityId, std::string source,
            std::string chunkName = "staged script");
        Result<void> runHeadless(std::uint32_t ticks);
        Result<void> stop();
        bool running() const noexcept;
        std::uint64_t tickCount() const noexcept;
        std::optional<std::array<float, 3>> position(std::string_view authoredEntityId) const;
        const std::map<std::string, std::int64_t>& runtimeEntities() const;
    };
}
