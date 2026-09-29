//
// Minimal Vulkan renderer bootstrap.
//

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#ifdef CPP_GAME_ENGINE_USE_VULKAN
#include <vulkan/vulkan.h>
#else
using VkInstance = void*;
using VkQueue = void*;
using VkSemaphore = void*;
using VkDebugReportCallbackEXT = void*;
#endif

#ifndef CPP_GAME_ENGINE_MOBILE
#include <GLFW/glfw3.h>
#else
struct GLFWwindow;
#endif

#include "../structs/arraylist.h"
#include "../rendering/renderer.h"
#include "../rendering/render_frame.h"
#include "../rendering/texture.h"
#include "vulkan_shader.h"
#include "vulkan_memory.h"
#include "vulkan_surface_provider.h"
#include "vulkan_swapchain.h"

namespace vulkan
{
    struct VulkanRendererConfig
    {
        std::string applicationName = "CPPGameEngine";
        uint32_t applicationVersion = 1;
        bool enableValidationLayers = false;
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        // Optional diagnostics callback, retained through initialization and shutdown.
        PFN_vkDebugReportCallbackEXT validationCallback = nullptr;
        void* validationUserData = nullptr;
#endif
        std::vector<const char*> extraInstanceExtensions;
        std::vector<const char*> extraDeviceExtensions;
        rendering::SwapchainConfig swapchain;
    };

    struct VulkanRendererInfo
    {
        uint32_t physicalDeviceCount = 0;
        bool hasSurface = false;
        bool hasLogicalDevice = false;
        bool hasSwapchain = false;
        bool portabilitySubset = false;
        bool sampledTextures = false;
        bool depthAttachment = false;
        uint32_t graphicsQueueFamily = 0;
        uint32_t presentQueueFamily = 0;
        rendering::SwapchainInfo swapchain;
    };

    class VulkanRenderer : public IRenderer, public rendering::IRenderFrameCoordinator
    {
        struct GpuState;
        std::unique_ptr<GpuState> m_gpu;
        GLFWwindow* m_window = nullptr;
        IVulkanSurfaceProvider* m_surfaceProvider = nullptr;
        VkInstance m_instance = {};
        VkDebugReportCallbackEXT m_validationCallback = {};
        VkSurfaceKHR m_surface = {};
        VkPhysicalDevice m_physicalDevice = {};
        VkDevice m_device = {};
        VkQueue m_graphicsQueue = {};
        VkQueue m_presentQueue = {};
        VkSemaphore m_imageAvailableSemaphore = {};
        VulkanQueueFamilyIndices m_queueFamilies;
        VulkanMemoryManager m_memoryManager;
        VulkanSwapchain m_swapchain;
        rendering::RenderFrame m_currentFrame;
        VulkanRendererInfo m_info {};
        ArrayList<rendering::ShaderUniformWrite> m_pendingUniformWrites;
        rendering::SwapchainConfig m_swapchainConfig;
        uint64_t m_nextFrameIndex = 0;
        size_t m_renderCallCount = 0;
        bool m_frameActive = false;
        bool m_initialized = false;
        bool m_portabilitySubset = false;

        void createInstance(const VulkanRendererConfig& config);
        void createSurface();
        void pickPhysicalDevice(const VulkanRendererConfig& config);
        void createLogicalDevice(const VulkanRendererConfig& config);
        void createSwapchain(const rendering::SwapchainConfig& config);
        void recreateSwapchain();
        void createFrameSync();
        void destroyFrameSync();
        bool framebufferReady() const;

    protected:
        bool uploadUniformImpl(
            rendering::IShader& shader,
            const rendering::ShaderUniformUpload& upload
        ) override;
        void renderImpl(rendering::IShader& shader) override;

    public:
        VulkanRenderer();
        ~VulkanRenderer();

        VulkanRenderer(const VulkanRenderer&) = delete;
        VulkanRenderer& operator=(const VulkanRenderer&) = delete;
        VulkanRenderer(VulkanRenderer&& other) noexcept;
        VulkanRenderer& operator=(VulkanRenderer&& other) noexcept;

#ifndef CPP_GAME_ENGINE_MOBILE
        void initialize(GLFWwindow* window, const VulkanRendererConfig& config = {});
#endif
        void initialize(IVulkanSurfaceProvider& surfaceProvider, const VulkanRendererConfig& config = {});
        void shutdown();

        bool beginRenderFrame() override;
        void endRenderFrame() override;
        void cancelRenderFrame() override;
        void waitIdle() override;
        const rendering::RenderFrame& currentFrame() const override
        { return m_currentFrame; }

        const rendering::SwapchainInfo& swapchainInfo() const override
        { return m_info.swapchain; }

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

        VkDevice device() const
        { return m_device; }

        VkQueue graphicsQueue() const
        { return m_graphicsQueue; }

        VkQueue presentQueue() const
        { return m_presentQueue; }

        const VulkanQueueFamilyIndices& queueFamilies() const
        { return m_queueFamilies; }

        const VulkanSwapchain& swapchain() const
        { return m_swapchain; }

        VulkanMemoryManager& memoryManager()
        { return m_memoryManager; }

        const VulkanMemoryManager& memoryManager() const
        { return m_memoryManager; }

        const ArrayList<rendering::ShaderUniformWrite>& pendingUniformWrites() const
        { return m_pendingUniformWrites; }

        void clearPendingUniformWrites()
        { m_pendingUniformWrites.clear(); }

        size_t renderCallCount() const
        { return m_renderCallCount; }

        // Record an asset-driven non-indexed mesh draw. Vertex bytes are copied
        // into frame-owned GPU resources before this call returns.
        void drawMesh(
            VulkanShaderProgram& shader,
            const rendering::SerializedBufferView& vertices,
            const rendering::VertexLayout& layout
        );
        void drawMesh(
            VulkanShaderProgram& shader,
            const rendering::SerializedBufferView& vertices,
            const rendering::VertexLayout& layout,
            const rendering::Texture2D& texture
        );

        // Render-thread-only persistent GPU resources used by RenderDevice.
        void cacheMesh(uint64_t id, const rendering::SerializedBufferView& vertices,
            const rendering::VertexLayout& layout);
        void cacheTexture(uint64_t id, const rendering::Texture2D& texture);
        void releaseMesh(uint64_t id);
        void releaseTexture(uint64_t id);
        void drawCached(VulkanShaderProgram& shader, uint64_t meshId, uint64_t textureId = 0);
    };
}
