#pragma once

#include "project.h"
#include "gameplay2d.h"
#include "gameplay_ui.h"
#include "persistence.h"
#include "../audio/audio.h"

#include <array>
#include <cstdint>
#include <cstddef>
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
        std::map<std::string, float> values;
        float pointerX = 0, pointerY = 0, wheelX = 0, wheelY = 0;
        bool focused = true;
    };

    struct RuntimeOptions
    {
        float fixedDeltaSeconds = 1.0f / 60.0f;
        std::set<std::string> allowedActions;
        std::function<void(std::string_view)> log;
        std::shared_ptr<audio::AudioSystem> audio;
        std::shared_ptr<PersistenceStore> persistence;
        std::shared_ptr<interaction::ActionMapper> inputMapper;
        ui::GlyphAdvance glyphAdvance;
        std::function<float(std::string_view)> fontLineHeight;
    };

    struct SpriteSnapshot
    {
        std::string id;
        std::array<float, 3> position{};
        SpriteRenderer sprite;
        std::size_t frame = 0;
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
        std::size_t liveEntityCount() const noexcept;
        std::size_t activeScriptCount() const noexcept;
        std::optional<std::array<float, 3>> position(std::string_view authoredEntityId) const;
        const std::map<std::string, std::int64_t>& runtimeEntities() const;
        const Scene& activeScene() const;
        std::uint64_t sceneRevision() const noexcept;
        std::optional<std::int64_t> entityHandle(std::string_view persistentId) const;
        std::vector<SpriteSnapshot> sprites() const;
        std::optional<Camera2D> camera() const;
        const std::vector<ui::PanelSnapshot>& panels() const;
        const std::vector<gameplay2d::TriggerEvent>& triggers() const;
        Result<void> requestScene(std::string sceneAssetId, std::string spawnEntityId = {}, std::string travellerEntityId = {});
    };
}
