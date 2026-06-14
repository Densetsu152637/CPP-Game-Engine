//
// Minimal Vulkan renderer bootstrap.
//

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#ifdef CPP_GAME_ENGINE_USE_VULKAN
#define GLFW_INCLUDE_VULKAN
#include <vulkan/vulkan.h>
#else
using VkInstance = void*;
using VkSurfaceKHR = void*;
using VkPhysicalDevice = void*;
#endif

#include <GLFW/glfw3.h>

#include "../structs/arraylist.h"
#include "../rendering/renderer.h"
#include "vulkan_shader.h"

namespace vulkan
{
    struct VulkanRendererConfig
    {
        std::string applicationName = "CPPGameEngine";
        uint32_t applicationVersion = 1;
        bool enableValidationLayers = false;
        std::vector<const char*> extraInstanceExtensions;
    };

    struct VulkanRendererInfo
    {
        uint32_t physicalDeviceCount = 0;
        bool hasSurface = false;
    };

    class VulkanRenderer : public IRenderer
    {
        VkInstance m_instance = {};
        VkSurfaceKHR m_surface = {};
        VkPhysicalDevice m_physicalDevice = {};
        VulkanRendererInfo m_info {};
        ArrayList<rendering::ShaderUniformWrite> m_pendingUniformWrites;
        size_t m_renderCallCount = 0;
        bool m_initialized = false;

        void createInstance(const VulkanRendererConfig& config);
        void createSurface(GLFWwindow* window);
        void pickPhysicalDevice();

    protected:
        bool uploadUniformImpl(
            rendering::IShader& shader,
            const rendering::ShaderUniformUpload& upload
        ) override;
        void renderImpl(rendering::IShader& shader) override;

    public:
        VulkanRenderer() = default;
        ~VulkanRenderer();

        VulkanRenderer(const VulkanRenderer&) = delete;
        VulkanRenderer& operator=(const VulkanRenderer&) = delete;
        VulkanRenderer(VulkanRenderer&& other) noexcept;
        VulkanRenderer& operator=(VulkanRenderer&& other) noexcept;

        void initialize(GLFWwindow* window, const VulkanRendererConfig& config = {});
        void shutdown();

        bool initialized() const
        { return m_initialized; }

        const VulkanRendererInfo& info() const
        { return m_info; }

        VkInstance instance() const
        { return m_instance; }

        VkSurfaceKHR surface() const
        { return m_surface; }

        VkPhysicalDevice physicalDevice() const
        { return m_physicalDevice; }

        const ArrayList<rendering::ShaderUniformWrite>& pendingUniformWrites() const
        { return m_pendingUniformWrites; }

        void clearPendingUniformWrites()
        { m_pendingUniformWrites.clear(); }

        size_t renderCallCount() const
        { return m_renderCallCount; }
    };
}
