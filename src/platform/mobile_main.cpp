#include <SDL3/SDL_main.h>
#include <SDL3/SDL_log.h>

#include <array>
#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

#include "platform/sdl_mobile_window.h"
#include "project/project.h"
#include "project/runtime.h"
#include "rendering/camera.h"
#include "rendering/content.h"
#include "rendering/material.h"
#include "rendering/render_device.h"
#include "tooling/iteration.h"
#include "vulcan/vulkan_frame_backend.h"
#include "vulcan/vulkan_shader.h"

namespace
{
    struct AuthoredDrawable
    {
        std::string entityId;
        rendering::MeshAsset mesh;
        rendering::Texture2D texture;
        rendering::RenderResourceHandle meshHandle;
        rendering::RenderResourceHandle textureHandle;
    };

    std::string diagnosticsText(const project::Diagnostics& diagnostics)
    {
        std::string message;
        for (const auto& diagnostic : diagnostics)
            message += diagnostic.code + ": " + diagnostic.message + "\n";
        return message;
    }

    struct AuthoredProject
    {
        project::Project project;
        std::vector<AuthoredDrawable> drawables;
    };

    // Validate every catalog entry, including content that is not visible in
    // the startup scene, before a window or GPU device is created.
    AuthoredProject preflight(const std::filesystem::path& manifest)
    {
        auto loaded = project::loadProject(manifest);
        if (!loaded) throw std::runtime_error(diagnosticsText(loaded.error()));
        for (const auto& [id, asset] : loaded->assets)
        {
            auto path = project::resolveAsset(*loaded, id);
            if (!path) throw std::runtime_error(diagnosticsText(path.error()));
            if (asset.kind == "script")
            {
                auto valid = tooling::validateLua(*path);
                if (!valid) throw std::runtime_error(diagnosticsText(valid.error()));
            }
            else if (asset.kind == "mesh") (void)rendering::loadMeshAsset(*path);
            else if (asset.kind == "texture") (void)rendering::loadTexturePpm(*path);
        }

        AuthoredProject result {std::move(*loaded), {}};
        for (const auto& entity : result.project.scene.entities)
        {
            if (!entity.meshRenderer) continue;
            const auto meshPath = project::resolveAsset(result.project, entity.meshRenderer->mesh);
            if (!meshPath) throw std::runtime_error(diagnosticsText(meshPath.error()));
            AuthoredDrawable drawable;
            drawable.entityId = entity.id;
            drawable.mesh = rendering::loadMeshAsset(*meshPath);
            drawable.texture = {1, 1, {255, 255, 255, 255}};
            if (entity.meshRenderer->texture)
            {
                const auto texturePath = project::resolveAsset(result.project, *entity.meshRenderer->texture);
                if (!texturePath) throw std::runtime_error(diagnosticsText(texturePath.error()));
                drawable.texture = rendering::loadTexturePpm(*texturePath);
            }
            result.drawables.push_back(std::move(drawable));
        }
        if (result.drawables.empty()) throw std::runtime_error("Startup scene has no MeshRenderer entities");
        return result;
    }

    project::InputSnapshot sampleTouchActions(const project::Project& authored,
        platform::SdlMobileWindow& window, std::map<std::string, bool>& prior)
    {
        std::set<std::string> keys;
        std::set<std::string> newlyPressedKeys;
        const auto mapFinger = [](std::set<std::string>& target, const platform::TouchPoint& finger)
        {
            target.insert(finger.x >= 0.5f ? "Right" : "Left");
            target.insert(finger.y < 0.5f ? "Up" : "Down");
            target.insert("Space");
        };
        for (const auto& finger : window.touches()) mapFinger(keys, finger);
        // A system tap can deliver down and up in one event pump. Preserve
        // its pressed edge for one simulation tick even if it is no longer held.
        for (const auto& finger : window.newTouches())
        {
            mapFinger(keys, finger);
            mapFinger(newlyPressedKeys, finger);
        }
        window.clearNewTouches();
        project::InputSnapshot input;
        for (const auto& [action, key] : authored.inputActions)
        {
            if (key != "Right" && key != "Left" && key != "Up" && key != "Down" && key != "Space")
                throw std::runtime_error("Unsupported mobile input binding: " + key);
            const bool held = keys.contains(key);
            if (held) input.held.insert(action);
            if (held && (!prior[action] || newlyPressedKeys.contains(key))) input.pressed.insert(action);
            if (!held && prior[action]) input.released.insert(action);
            prior[action] = held;
        }
        return input;
    }

    class MobilePreview
    {
        vulkan::VulkanShaderProgram m_shader {"authored mobile content"};
        std::unique_ptr<rendering::RenderDevice> m_device;
        rendering::RenderResourceHandle m_shaderHandle;
        std::vector<AuthoredDrawable>& m_drawables;

    public:
        MobilePreview(platform::SdlMobileWindow& window,
            const std::filesystem::path& shaders, std::vector<AuthoredDrawable>& drawables)
            : m_drawables(drawables)
        {
            m_shader.addSpirv(rendering::ShaderStage::Vertex, shaders / "mesh_textured.vert.spv");
            m_shader.addSpirv(rendering::ShaderStage::Fragment, shaders / "mesh_textured.frag.spv");
            m_device = std::make_unique<rendering::RenderDevice>(
                std::make_unique<vulkan::VulkanFrameBackend>(window));
            m_shaderHandle = m_device->createShader(m_shader);
            for (auto& drawable : m_drawables)
            {
                drawable.meshHandle = m_device->createMesh(drawable.mesh.vertexData(), drawable.mesh.layout());
                drawable.textureHandle = m_device->createTexture(drawable.texture);
            }
        }

        ~MobilePreview()
        {
            if (m_device)
            {
                try { m_device->shutdown(); }
                catch (const std::exception& error) { SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s", error.what()); }
            }
        }

        void draw(const project::Runtime& runtime, bool observe)
        {
            auto frame = m_device->makeFrame();
            rendering::CameraUniform camera;
            camera.viewProjection = {0.5f,0,0,0, 0,-0.8f,0,0, 0,0,-0.1f,0, 0,0,0.5f,1};
            rendering::MaterialUniform material;
            for (const auto& drawable : m_drawables)
            {
                const auto position = runtime.position(drawable.entityId);
                if (!position) continue;
                const std::array<float, 16> model {1,0,0,0, 0,1,0,0, 0,0,1,0,
                    (*position)[0], (*position)[1], (*position)[2], 1};
                rendering::DrawCommand command {m_shaderHandle, drawable.meshHandle, drawable.textureHandle};
                rendering::appendUniform(command, "model", model, {0,0});
                rendering::appendUniform(command, "material", material, {0,1});
                rendering::appendUniform(command, "camera", camera, {0,2});
                frame->record(std::move(command));
            }
            const auto ticket = m_device->submit(frame);
            if (observe) m_device->wait(ticket);
        }
    };
}

int main(int, char**)
{
    try
    {
        platform::SdlMobileWindow window;
        const auto appStorage = platform::materializeMobileAssets();
        auto authored = preflight(appStorage / "first-project/project.json");
        project::Runtime runtime(authored.project);
        const auto started = runtime.start();
        if (!started) throw std::runtime_error(diagnosticsText(started.error()));

        std::unique_ptr<MobilePreview> preview;
        std::map<std::string, bool> priorActions;
        std::uint64_t ticks = 0;
        std::uint64_t generation = 0;
        SDL_Log("MOBILE_STATE event=start project=%s", authored.project.name.c_str());
        while (!window.quitRequested())
        {
            window.pumpEvents();
            if (window.quitRequested()) break;
            if (window.paused() || !window.drawableExtent().valid())
            {
                SDL_WaitEventTimeout(nullptr, 50);
                continue;
            }
            if (window.takeSurfaceDirty()) preview.reset();
            if (!preview)
            {
                preview = std::make_unique<MobilePreview>(window, appStorage / "shaders", authored.drawables);
                ++generation;
                SDL_Log("MOBILE_STATE event=surface-recreate generation=%llu",
                    static_cast<unsigned long long>(generation));
            }

            const auto next = std::chrono::steady_clock::now() + std::chrono::microseconds(16667);
            const auto input = sampleTouchActions(authored.project, window, priorActions);
            const auto advanced = runtime.tick(input);
            if (!advanced) throw std::runtime_error(diagnosticsText(advanced.error()));
            ++ticks;
            const bool observe = ticks == 1 || !input.pressed.empty() || ticks % 120 == 0 || generation > 1 && ticks % 30 == 0;
            preview->draw(runtime, observe);
            if (observe)
            {
                const auto position = runtime.position("entity:player");
                if (position)
                    SDL_Log("MOBILE_STATE event=frame tick=%llu x=%.3f generation=%llu rendered=1",
                        static_cast<unsigned long long>(ticks), (*position)[0],
                        static_cast<unsigned long long>(generation));
            }
            std::this_thread::sleep_until(next);
        }
        preview.reset();
        const auto stopped = runtime.stop();
        if (!stopped) throw std::runtime_error(diagnosticsText(stopped.error()));
        SDL_Log("MOBILE_STATE event=stop tick=%llu", static_cast<unsigned long long>(ticks));
        return 0;
    }
    catch (const std::exception& error)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Mobile engine: %s", error.what());
        return 1;
    }
}
