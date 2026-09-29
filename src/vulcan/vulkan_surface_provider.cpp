#include "vulkan_surface_provider.h"

#if defined(CPP_GAME_ENGINE_USE_VULKAN) && !defined(CPP_GAME_ENGINE_MOBILE)
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <algorithm>
#include <stdexcept>

namespace vulkan
{
    VulkanGlfwSurfaceProvider::VulkanGlfwSurfaceProvider(GLFWwindow* window)
        : m_window(window)
    {
        if (!window) throw std::invalid_argument("GLFW surface provider requires a window");
    }

    std::vector<const char*> VulkanGlfwSurfaceProvider::requiredInstanceExtensions() const
    {
        uint32_t count = 0;
        const char** extensions = glfwGetRequiredInstanceExtensions(&count);
        if (!extensions || count == 0)
            throw std::runtime_error("GLFW did not provide Vulkan surface extensions");
        return { extensions, extensions + count };
    }

    VkSurfaceKHR VulkanGlfwSurfaceProvider::createSurface(VkInstance instance) const
    {
        VkSurfaceKHR surface = {};
        if (glfwCreateWindowSurface(instance, m_window, nullptr, &surface) != VK_SUCCESS)
            throw std::runtime_error("GLFW could not create a Vulkan surface");
        return surface;
    }

    rendering::ImageExtent VulkanGlfwSurfaceProvider::drawableExtent() const
    {
        if (glfwGetWindowAttrib(m_window, GLFW_ICONIFIED)) return {};
        int width = 0, height = 0;
        glfwGetFramebufferSize(m_window, &width, &height);
        return { static_cast<uint32_t>(std::max(width, 0)),
            static_cast<uint32_t>(std::max(height, 0)) };
    }
}
#endif
