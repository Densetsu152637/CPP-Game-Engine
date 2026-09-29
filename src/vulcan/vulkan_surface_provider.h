#pragma once

#include <vector>

#ifdef CPP_GAME_ENGINE_USE_VULKAN
#include <vulkan/vulkan.h>
#else
using VkInstance = void*;
using VkSurfaceKHR = void*;
#endif

#include "../rendering/swapchain.h"

#ifndef CPP_GAME_ENGINE_MOBILE
struct GLFWwindow;
#endif

namespace vulkan
{
    // Implementations must remain alive until the renderer has shut down. The
    // rendering thread calls all three methods; providers must synchronize any
    // state collected by their platform event thread.
    class IVulkanSurfaceProvider
    {
    public:
        virtual ~IVulkanSurfaceProvider() = default;
        virtual std::vector<const char*> requiredInstanceExtensions() const = 0;
        virtual VkSurfaceKHR createSurface(VkInstance instance) const = 0;
        virtual rendering::ImageExtent drawableExtent() const = 0;
    };

#ifndef CPP_GAME_ENGINE_MOBILE
    class VulkanGlfwSurfaceProvider final : public IVulkanSurfaceProvider
    {
        ::GLFWwindow* m_window;
    public:
        explicit VulkanGlfwSurfaceProvider(::GLFWwindow* window);
        std::vector<const char*> requiredInstanceExtensions() const override;
        VkSurfaceKHR createSurface(VkInstance instance) const override;
        rendering::ImageExtent drawableExtent() const override;
    };
#endif
}
