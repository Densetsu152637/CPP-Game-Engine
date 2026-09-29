#include "sdl_mobile_window.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>

#include <SDL3/SDL_vulkan.h>

namespace platform
{
    namespace
    {
        std::runtime_error sdlError(const char* operation)
        { return std::runtime_error(std::string(operation) + ": " + SDL_GetError()); }

        void copyPackagedFile(const std::filesystem::path& root, const char* relative)
        {
            size_t size = 0;
            std::string source = std::string(SDL_GetBasePath() ? SDL_GetBasePath() : "") + relative;
            void* data = SDL_LoadFile(source.c_str(), &size);
            if (!data) throw sdlError(relative);
            try
            {
                const auto destination = root / relative;
                std::filesystem::create_directories(destination.parent_path());
                std::ofstream output(destination, std::ios::binary | std::ios::trunc);
                output.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
                if (!output) throw std::runtime_error("Unable to write mobile asset: " + destination.string());
            }
            catch (...)
            {
                SDL_free(data);
                throw;
            }
            SDL_free(data);
        }
    }

    SdlMobileWindow::SdlMobileWindow()
    {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) throw sdlError("SDL_Init");
        m_window = SDL_CreateWindow("CPP Game Engine", 960, 600,
                                    SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!m_window)
        {
            SDL_Quit();
            throw sdlError("SDL_CreateWindow");
        }
        refreshExtent();
        if (!SDL_AddEventWatch(&SdlMobileWindow::eventWatch, this))
        {
            SDL_DestroyWindow(m_window);
            SDL_Quit();
            throw sdlError("SDL_AddEventWatch");
        }
    }

    SdlMobileWindow::~SdlMobileWindow()
    {
        SDL_RemoveEventWatch(&SdlMobileWindow::eventWatch, this);
        if (m_window) SDL_DestroyWindow(m_window);
        SDL_Quit();
    }

    bool SDLCALL SdlMobileWindow::eventWatch(void* userdata, SDL_Event* event)
    {
        auto& self = *static_cast<SdlMobileWindow*>(userdata);
        constexpr uint32_t background = 1u << 0;
        constexpr uint32_t unfocused = 1u << 1;
        constexpr uint32_t hidden = 1u << 2;
        switch (event->type)
        {
            case SDL_EVENT_WILL_ENTER_BACKGROUND:
            case SDL_EVENT_DID_ENTER_BACKGROUND:
                self.setInactive(background, true);
                break;
            case SDL_EVENT_DID_ENTER_FOREGROUND:
                self.setInactive(background, false);
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                self.setInactive(unfocused, true);
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                self.setInactive(unfocused, false);
                break;
            case SDL_EVENT_WINDOW_HIDDEN:
            case SDL_EVENT_WINDOW_MINIMIZED:
                self.setInactive(hidden, true);
                break;
            case SDL_EVENT_WINDOW_SHOWN:
            case SDL_EVENT_WINDOW_RESTORED:
                self.setInactive(hidden, false);
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                self.m_width.store(static_cast<uint32_t>(std::max(event->window.data1, 0)), std::memory_order_release);
                self.m_height.store(static_cast<uint32_t>(std::max(event->window.data2, 0)), std::memory_order_release);
                self.m_surfaceDirty.store(true, std::memory_order_release);
                break;
            default: break;
        }
        return true;
    }

    void SdlMobileWindow::setInactive(uint32_t reason, bool inactive)
    {
        const uint32_t previous = inactive
            ? m_inactiveReasons.fetch_or(reason, std::memory_order_acq_rel)
            : m_inactiveReasons.fetch_and(~reason, std::memory_order_acq_rel);
        const uint32_t current = inactive ? previous | reason : previous & ~reason;
        if (previous == 0 && current != 0) SDL_Log("MOBILE_STATE event=pause");
        if (previous != 0 && current == 0)
        {
            m_surfaceDirty.store(true, std::memory_order_release);
            SDL_Log("MOBILE_STATE event=resume");
        }
    }

    void SdlMobileWindow::refreshExtent()
    {
        int width = 0, height = 0;
        if (SDL_GetWindowSizeInPixels(m_window, &width, &height))
        {
            m_width.store(static_cast<uint32_t>(std::max(width, 0)), std::memory_order_release);
            m_height.store(static_cast<uint32_t>(std::max(height, 0)), std::memory_order_release);
        }
    }

    void SdlMobileWindow::pumpEvents()
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_TERMINATING:
                    m_quit = true;
                    break;
                case SDL_EVENT_FINGER_DOWN:
                case SDL_EVENT_FINGER_MOTION:
                {
                    const auto found = std::find_if(m_touches.begin(), m_touches.end(), [&](const TouchPoint& point)
                    { return point.finger == event.tfinger.fingerID; });
                    const TouchPoint point {event.tfinger.fingerID, event.tfinger.x, event.tfinger.y};
                    if (event.type == SDL_EVENT_FINGER_DOWN) m_newTouches.push_back(point);
                    if (found == m_touches.end()) m_touches.push_back(point);
                    else *found = point;
                    break;
                }
                case SDL_EVENT_FINGER_UP:
                    std::erase_if(m_touches, [&](const TouchPoint& point)
                    { return point.finger == event.tfinger.fingerID; });
                    break;
                default: break;
            }
        }
        if (paused())
        {
            m_touches.clear();
            m_newTouches.clear();
        }
        refreshExtent();
    }

    std::vector<const char*> SdlMobileWindow::requiredInstanceExtensions() const
    {
        Uint32 count = 0;
        const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
        if (!names || !count) throw sdlError("SDL_Vulkan_GetInstanceExtensions");
        return {names, names + count};
    }

    VkSurfaceKHR SdlMobileWindow::createSurface(VkInstance instance) const
    {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (!SDL_Vulkan_CreateSurface(m_window, instance, nullptr, &surface))
            throw sdlError("SDL_Vulkan_CreateSurface");
        return surface;
    }

    rendering::ImageExtent SdlMobileWindow::drawableExtent() const
    {
        if (paused()) return {0, 0};
        return {m_width.load(std::memory_order_acquire),
                m_height.load(std::memory_order_acquire)};
    }

    std::filesystem::path materializeMobileAssets()
    {
        const char* preferencePath = SDL_GetPrefPath("CPPGameEngine", "MobileSample");
        if (!preferencePath) throw sdlError("SDL_GetPrefPath");
        const std::filesystem::path root(preferencePath);
        // Keep this list in sync with the sample directory bundled by the
        // platform projects. SDL_LoadFile can read compressed Android APK
        // assets, while the engine's project loaders need ordinary files.
        for (const char* file : {
            "first-project/project.json",
            "first-project/README.md",
            "first-project/scenes/main.scene.json",
            "first-project/assets/player.mesh",
            "first-project/assets/player.ppm",
            "first-project/scripts/player.lua",
            "first-project/scripts/player_reload.lua",
            "first-project/scripts/system_fixture.lua",
            "first-project/scripts/system_abort_fixture.lua",
            "first-project/scripts/create_then_fail.lua",
            "first-project/scripts/invalid.lua",
            "first-project/scripts/teardown_fails.lua",
            "shaders/mesh_textured.vert.spv",
            "shaders/mesh_textured.frag.spv"})
            copyPackagedFile(root, file);
        return root;
    }
}
