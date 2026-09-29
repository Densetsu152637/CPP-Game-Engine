#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include "vulcan/vulkan_surface_provider.h"

namespace platform
{
    struct TouchPoint
    {
        SDL_FingerID finger = 0;
        float x = 0.0f; // normalized 0..1
        float y = 0.0f;
    };

    // The owning thread constructs, pumps, and destroys this window. SDL's
    // event watch only writes atomics so pause takes effect before event polling.
    class SdlMobileWindow final : public vulkan::IVulkanSurfaceProvider
    {
        SDL_Window* m_window = nullptr;
        // SDL's Android focus callback can precede the background callback and
        // the native surface destruction by several frames. Keep each inactive
        // reason until its matching foreground/window event arrives.
        std::atomic<uint32_t> m_inactiveReasons {0};
        std::atomic<bool> m_surfaceDirty {false};
        std::atomic<uint32_t> m_width {0};
        std::atomic<uint32_t> m_height {0};
        bool m_quit = false;
        std::vector<TouchPoint> m_touches;
        std::vector<TouchPoint> m_newTouches;

        static bool SDLCALL eventWatch(void* userdata, SDL_Event* event);
        void setInactive(uint32_t reason, bool inactive);
        void refreshExtent();

    public:
        SdlMobileWindow();
        ~SdlMobileWindow() override;
        SdlMobileWindow(const SdlMobileWindow&) = delete;
        SdlMobileWindow& operator=(const SdlMobileWindow&) = delete;

        void pumpEvents();
        bool quitRequested() const { return m_quit; }
        bool paused() const { return m_inactiveReasons.load(std::memory_order_acquire) != 0; }
        bool takeSurfaceDirty() { return m_surfaceDirty.exchange(false, std::memory_order_acq_rel); }
        const std::vector<TouchPoint>& touches() const { return m_touches; }
        const std::vector<TouchPoint>& newTouches() const { return m_newTouches; }
        void clearNewTouches() { m_newTouches.clear(); }

        std::vector<const char*> requiredInstanceExtensions() const override;
        VkSurfaceKHR createSurface(VkInstance instance) const override;
        rendering::ImageExtent drawableExtent() const override;
    };

    // Copies the authored sample and precompiled SPIR-V assets from the app
    // package into private writable storage for existing std::ifstream loaders.
    // Returns the private storage root.
    std::filesystem::path materializeMobileAssets();
}
