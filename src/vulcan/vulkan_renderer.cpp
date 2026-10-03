//
// Vulkan renderer, device, and swapchain orchestration.
//

#include "vulkan_renderer.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

#include "vulkan_render_resources.h"

namespace vulkan
{
    namespace
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
#ifndef CPP_GAME_ENGINE_MOBILE
        std::vector<const char*> glfw_required_instance_extensions()
        {
            uint32_t extensionCount = 0;
            const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
            if (nullptr == extensions || 0 == extensionCount)
                throw std::runtime_error("GLFW did not provide required Vulkan instance extensions");

            return std::vector<const char*>(extensions, extensions + extensionCount);
        }
#endif

        bool validation_layer_available(const char* layerName)
        {
            uint32_t layerCount = 0;
            vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

            std::vector<VkLayerProperties> layers(layerCount);
            vkEnumerateInstanceLayerProperties(&layerCount, layers.data());

            for (const VkLayerProperties& layer : layers)
            {
                if (0 == std::strcmp(layer.layerName, layerName))
                    return true;
            }

            return false;
        }

        bool extension_list_contains(
            const std::vector<const char*>& extensions,
            const char* extension
        ) {
            for (const char* existing : extensions)
            {
                if (0 == std::strcmp(existing, extension))
                    return true;
            }

            return false;
        }

        void append_unique_extension(std::vector<const char*>& extensions, const char* extension)
        {
            if (!extension_list_contains(extensions, extension))
                extensions.push_back(extension);
        }

        std::vector<const char*> required_device_extensions(const VulkanRendererConfig& config)
        {
            std::vector<const char*> extensions;
            extensions.reserve(1 + config.extraDeviceExtensions.size());
            extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

            for (const char* extension : config.extraDeviceExtensions)
                append_unique_extension(extensions, extension);

            return extensions;
        }

        bool instance_extension_available(const char* name)
        {
            uint32_t count = 0;
            if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS)
                return false;
            std::vector<VkExtensionProperties> properties(count);
            if (vkEnumerateInstanceExtensionProperties(nullptr, &count, properties.data()) != VK_SUCCESS)
                return false;
            for (const auto& property : properties)
                if (std::strcmp(property.extensionName, name) == 0) return true;
            return false;
        }

        bool device_extension_available(VkPhysicalDevice device, const char* name)
        {
            uint32_t count = 0;
            if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS)
                return false;
            std::vector<VkExtensionProperties> properties(count);
            if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, properties.data()) != VK_SUCCESS)
                return false;
            for (const auto& property : properties)
                if (std::strcmp(property.extensionName, name) == 0) return true;
            return false;
        }

        bool supports_sampled_rgba(VkPhysicalDevice device)
        {
            VkFormatProperties properties {};
            vkGetPhysicalDeviceFormatProperties(device, VK_FORMAT_R8G8B8A8_UNORM, &properties);
            return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0 &&
                (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) != 0;
        }

        bool supports_depth_attachment(VkPhysicalDevice device)
        {
            for (VkFormat format : { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT,
                VK_FORMAT_D32_SFLOAT_S8_UINT })
            {
                VkFormatProperties properties {};
                vkGetPhysicalDeviceFormatProperties(device, format, &properties);
                if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
                    return true;
            }
            return false;
        }

        bool device_supports_extensions(
            const VkPhysicalDevice device,
            const std::vector<const char*>& requiredExtensions
        ) {
            uint32_t extensionCount = 0;
            vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

            std::vector<VkExtensionProperties> availableExtensions(extensionCount);
            vkEnumerateDeviceExtensionProperties(
                device,
                nullptr,
                &extensionCount,
                availableExtensions.data()
            );

            for (const char* required : requiredExtensions)
            {
                bool found = false;
                for (const VkExtensionProperties& available : availableExtensions)
                {
                    if (0 == std::strcmp(available.extensionName, required))
                    {
                        found = true;
                        break;
                    }
                }

                if (!found)
                    return false;
            }

            return true;
        }
#endif
    }

    VulkanRenderer::VulkanRenderer() = default;

    VulkanRenderer::~VulkanRenderer()
    {
        shutdown();
    }

    VulkanRenderer::VulkanRenderer(VulkanRenderer&& other) noexcept
        : m_gpu(std::move(other.m_gpu)),
          m_window(other.m_window),
          m_surfaceProvider(other.m_surfaceProvider),
          m_instance(other.m_instance),
          m_validationCallback(std::exchange(other.m_validationCallback, {})),
          m_surface(other.m_surface),
          m_physicalDevice(other.m_physicalDevice),
          m_device(other.m_device),
          m_graphicsQueue(other.m_graphicsQueue),
          m_presentQueue(other.m_presentQueue),
          m_imageAvailableSemaphore(other.m_imageAvailableSemaphore),
          m_queueFamilies(other.m_queueFamilies),
          m_memoryManager(std::move(other.m_memoryManager)),
          m_swapchain(std::move(other.m_swapchain)),
          m_currentFrame(other.m_currentFrame),
          m_info(other.m_info),
          m_pendingUniformWrites(std::move(other.m_pendingUniformWrites)),
          m_swapchainConfig(other.m_swapchainConfig),
          m_nextFrameIndex(other.m_nextFrameIndex),
          m_renderCallCount(other.m_renderCallCount),
          m_frameActive(other.m_frameActive),
          m_initialized(other.m_initialized),
          m_surfaceLost(other.m_surfaceLost),
          m_portabilitySubset(other.m_portabilitySubset)
    {
        other.m_window = nullptr;
        other.m_surfaceProvider = nullptr;
        other.m_portabilitySubset = false;
        other.m_instance = {};
        other.m_surface = {};
        other.m_physicalDevice = {};
        other.m_device = {};
        other.m_graphicsQueue = {};
        other.m_presentQueue = {};
        other.m_imageAvailableSemaphore = {};
        other.m_queueFamilies = {};
        other.m_memoryManager.reset();
        other.m_currentFrame = {};
        other.m_info = {};
        other.m_nextFrameIndex = 0;
        other.m_renderCallCount = 0;
        other.m_frameActive = false;
        other.m_initialized = false;
        other.m_surfaceLost = false;
    }

    VulkanRenderer& VulkanRenderer::operator=(VulkanRenderer&& other) noexcept
    {
        if (this == &other)
            return *this;

        shutdown();
        m_gpu = std::move(other.m_gpu);
        m_window = other.m_window;
        m_surfaceProvider = other.m_surfaceProvider;
        m_instance = other.m_instance;
        m_validationCallback = std::exchange(other.m_validationCallback, {});
        m_surface = other.m_surface;
        m_physicalDevice = other.m_physicalDevice;
        m_device = other.m_device;
        m_graphicsQueue = other.m_graphicsQueue;
        m_presentQueue = other.m_presentQueue;
        m_imageAvailableSemaphore = other.m_imageAvailableSemaphore;
        m_queueFamilies = other.m_queueFamilies;
        m_memoryManager = std::move(other.m_memoryManager);
        m_swapchain = std::move(other.m_swapchain);
        m_currentFrame = other.m_currentFrame;
        m_info = other.m_info;
        m_pendingUniformWrites = std::move(other.m_pendingUniformWrites);
        m_swapchainConfig = other.m_swapchainConfig;
        m_nextFrameIndex = other.m_nextFrameIndex;
        m_renderCallCount = other.m_renderCallCount;
        m_frameActive = other.m_frameActive;
        m_initialized = other.m_initialized;
        m_surfaceLost = other.m_surfaceLost;
        m_portabilitySubset = other.m_portabilitySubset;

        other.m_window = nullptr;
        other.m_surfaceProvider = nullptr;
        other.m_portabilitySubset = false;
        other.m_instance = {};
        other.m_surface = {};
        other.m_physicalDevice = {};
        other.m_device = {};
        other.m_graphicsQueue = {};
        other.m_presentQueue = {};
        other.m_imageAvailableSemaphore = {};
        other.m_queueFamilies = {};
        other.m_memoryManager.reset();
        other.m_currentFrame = {};
        other.m_info = {};
        other.m_nextFrameIndex = 0;
        other.m_renderCallCount = 0;
        other.m_frameActive = false;
        other.m_initialized = false;
        other.m_surfaceLost = false;
        return *this;
    }

#ifndef CPP_GAME_ENGINE_MOBILE
    void VulkanRenderer::initialize(GLFWwindow* window, const VulkanRendererConfig& config)
    {
        if (m_initialized)
            throw std::logic_error("VulkanRenderer is already initialized");

        if (nullptr == window)
            throw std::invalid_argument("VulkanRenderer requires a GLFWwindow");

#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (!glfwVulkanSupported())
            throw std::runtime_error("GLFW reports that Vulkan is not supported on this platform");

        m_window = window;
        m_swapchainConfig = config.swapchain;
        try
        {
            createInstance(config);
            createSurface();
            pickPhysicalDevice(config);
            createLogicalDevice(config);
            createFrameSync();
            createSwapchain(config.swapchain);
            m_initialized = true;
        }
        catch (...)
        {
            shutdown();
            throw;
        }
#else
        (void)window;
        (void)config;
        throw std::runtime_error("VulkanRenderer requires CPP_GAME_ENGINE_USE_VULKAN");
#endif
    }
#endif

    void VulkanRenderer::initialize(IVulkanSurfaceProvider& surfaceProvider, const VulkanRendererConfig& config)
    {
        if (m_initialized)
            throw std::logic_error("VulkanRenderer is already initialized");
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        m_surfaceProvider = &surfaceProvider;
        m_swapchainConfig = config.swapchain;
        try
        {
            createInstance(config);
            createSurface();
            pickPhysicalDevice(config);
            createLogicalDevice(config);
            createFrameSync();
            createSwapchain(config.swapchain);
            m_initialized = true;
        }
        catch (...)
        {
            shutdown();
            throw;
        }
#else
        (void)surfaceProvider;
        (void)config;
        throw std::runtime_error("VulkanRenderer requires CPP_GAME_ENGINE_USE_VULKAN");
#endif
    }

    void VulkanRenderer::createInstance(const VulkanRendererConfig& config)
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        std::vector<const char*> extensions;
        if (m_surfaceProvider)
            extensions = m_surfaceProvider->requiredInstanceExtensions();
#ifndef CPP_GAME_ENGINE_MOBILE
        else
            extensions = glfw_required_instance_extensions();
#endif
        for (const char* extension : config.extraInstanceExtensions)
            append_unique_extension(extensions, extension);
        if (config.validationCallback)
            append_unique_extension(extensions, VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        const bool portabilityEnumeration = instance_extension_available(
            VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        if (portabilityEnumeration)
            append_unique_extension(extensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif

        std::vector<const char*> validationLayers;
        if (config.enableValidationLayers)
        {
            constexpr const char* layer = "VK_LAYER_KHRONOS_validation";
            if (!validation_layer_available(layer))
                throw std::runtime_error("Requested Vulkan validation layer is not available");

            validationLayers.push_back(layer);
        }

        VkApplicationInfo appInfo {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = config.applicationName.c_str();
        appInfo.applicationVersion = config.applicationVersion;
        appInfo.pEngineName = "CPPGameEngine";
        appInfo.engineVersion = 1;
        appInfo.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo createInfo {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        if (portabilityEnumeration)
            createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
        VkDebugReportCallbackCreateInfoEXT diagnostics { VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT };
        diagnostics.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
        diagnostics.pfnCallback = config.validationCallback;
        diagnostics.pUserData = config.validationUserData;
        if (config.validationCallback) createInfo.pNext = &diagnostics;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.empty() ? nullptr : validationLayers.data();

        if (VK_SUCCESS != vkCreateInstance(&createInfo, nullptr, &m_instance))
            throw std::runtime_error("Failed to create Vulkan instance");
        if (config.validationCallback)
        {
            const auto createCallback = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(
                vkGetInstanceProcAddr(m_instance, "vkCreateDebugReportCallbackEXT"));
            if (!createCallback)
                throw std::runtime_error("Vulkan debug report extension is unavailable");
            require_vk(createCallback(m_instance, &diagnostics, nullptr, &m_validationCallback),
                "Create validation callback");
        }
#else
        (void)config;
#endif
    }

    void VulkanRenderer::createSurface()
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (nullptr == m_instance)
            throw std::logic_error("Cannot create a Vulkan surface before the instance");

        if (m_surfaceProvider)
            m_surface = m_surfaceProvider->createSurface(m_instance);
#ifndef CPP_GAME_ENGINE_MOBILE
        else if (VK_SUCCESS != glfwCreateWindowSurface(m_instance, m_window, nullptr, &m_surface))
            throw std::runtime_error("Failed to create Vulkan window surface");
#endif
        if (m_surface == VK_NULL_HANDLE)
            throw std::runtime_error("Surface provider returned a null Vulkan surface");

        m_info.hasSurface = true;
#else
#endif
    }

    void VulkanRenderer::pickPhysicalDevice(const VulkanRendererConfig& config)
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (nullptr == m_instance)
            throw std::logic_error("Cannot pick a Vulkan physical device before the instance");

        if (nullptr == m_surface)
            throw std::logic_error("Cannot pick a Vulkan physical device before the surface");

        const std::vector<const char*> extensions = required_device_extensions(config);

        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
        m_info.physicalDeviceCount = deviceCount;
        if (0 == deviceCount)
            throw std::runtime_error("No Vulkan physical devices were found");

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());
        bool missingSampledTextures = false;
        bool missingDepthAttachment = false;

        for (const VkPhysicalDevice device : devices)
        {
            const VulkanQueueFamilyIndices queueFamilies = find_queue_families(device, m_surface);
            if (!queueFamilies.complete())
                continue;

            if (!device_supports_extensions(device, extensions))
                continue;

            if (!VulkanSwapchain::querySupport(device, m_surface).usable())
                continue;

            const bool sampledTextures = supports_sampled_rgba(device);
            const bool depthAttachment = supports_depth_attachment(device);
            missingSampledTextures |= !sampledTextures;
            missingDepthAttachment |= !depthAttachment;
            if (!sampledTextures || !depthAttachment)
                continue;

            m_physicalDevice = device;
            m_portabilitySubset = device_extension_available(device, "VK_KHR_portability_subset");
            m_info.portabilitySubset = m_portabilitySubset;
            m_info.sampledTextures = true;
            m_info.depthAttachment = true;
            m_queueFamilies = queueFamilies;
            m_info.graphicsQueueFamily = queueFamilies.graphicsFamily;
            m_info.presentQueueFamily = queueFamilies.presentFamily;
            return;
        }

        std::string reason = "No Vulkan physical device supports graphics, presentation, and swapchain creation";
        if (missingSampledTextures) reason += "; sampled textures unavailable";
        if (missingDepthAttachment) reason += "; depth attachments unavailable";
        throw std::runtime_error(reason);
#else
        (void)config;
#endif
    }

    void VulkanRenderer::createLogicalDevice(const VulkanRendererConfig& config)
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (nullptr == m_physicalDevice)
            throw std::logic_error("Cannot create a Vulkan logical device before selecting a physical device");

        if (!m_queueFamilies.complete())
            throw std::logic_error("Cannot create a Vulkan logical device without queue families");

        std::vector<uint32_t> uniqueQueueFamilies;
        uniqueQueueFamilies.push_back(m_queueFamilies.graphicsFamily);
        if (!m_queueFamilies.usesSingleQueueFamily())
            uniqueQueueFamilies.push_back(m_queueFamilies.presentFamily);

        const float queuePriority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        queueCreateInfos.reserve(uniqueQueueFamilies.size());
        for (const uint32_t queueFamily : uniqueQueueFamilies)
        {
            VkDeviceQueueCreateInfo queueCreateInfo {};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        VkPhysicalDeviceFeatures deviceFeatures {};
        std::vector<const char*> extensions = required_device_extensions(config);
        if (m_portabilitySubset)
            append_unique_extension(extensions, "VK_KHR_portability_subset");

        VkDeviceCreateInfo createInfo {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        if (VK_SUCCESS != vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device))
            throw std::runtime_error("Failed to create Vulkan logical device");

        vkGetDeviceQueue(m_device, m_queueFamilies.graphicsFamily, 0, &m_graphicsQueue);
        vkGetDeviceQueue(m_device, m_queueFamilies.presentFamily, 0, &m_presentQueue);
        m_memoryManager.initialize(m_physicalDevice, m_device);
        m_info.hasLogicalDevice = true;
#else
        (void)config;
#endif
    }

    void VulkanRenderer::createSwapchain(const rendering::SwapchainConfig& config)
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (!framebufferReady())
        {
            m_info.hasSwapchain = false;
            m_info.swapchain = {};
            return;
        }

        rendering::ImageExtent extent;
        if (m_surfaceProvider)
            extent = m_surfaceProvider->drawableExtent();
#ifndef CPP_GAME_ENGINE_MOBILE
        else
        {
            int width = 0, height = 0;
            glfwGetFramebufferSize(m_window, &width, &height);
            extent = { static_cast<uint32_t>(std::max(width, 0)),
                static_cast<uint32_t>(std::max(height, 0)) };
        }
#endif
        m_swapchain.create(
            m_physicalDevice,
            m_device,
            m_surface,
            extent,
            m_queueFamilies,
            config
        );
        m_info.hasSwapchain = true;
        m_info.swapchain = m_swapchain.info();
        m_gpu->createTargets(m_swapchain, m_physicalDevice);
#else
        (void)config;
#endif
    }

    void VulkanRenderer::recreateSwapchain()
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (nullptr == m_device || m_surfaceLost)
            return;

        try
        {
            require_vk(vkDeviceWaitIdle(m_device), "Wait before swapchain recreation");
            m_gpu->draws.clear();
            m_gpu->destroyTargets();
            m_swapchain.reset();
            m_info.hasSwapchain = false;
            m_info.swapchain = {};
            if (framebufferReady())
                createSwapchain(m_swapchainConfig);
        }
        catch (const VulkanSurfaceLost&)
        {
            abandonSurface();
        }
        catch (const VulkanSwapchainOutOfDate&)
        {
            // The native window may still be changing. Retry on a later frame.
        }
        catch (...)
        {
            shutdown();
            throw;
        }
#endif
    }

    void VulkanRenderer::abandonSurface()
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (m_surfaceLost) return;
        if (m_frameActive)
            throw std::logic_error("Cannot invalidate a Vulkan surface during an active frame");
        // A failed present still enqueues its semaphore wait. Keep the device and
        // persistent resources, but retire every WSI object after the queue drains.
        if (m_device)
            require_vk(vkDeviceWaitIdle(m_device), "Wait after Vulkan surface loss");
        if (m_gpu)
        {
            m_gpu->draws.clear();
            m_gpu->destroyTargets();
        }
        m_swapchain.reset();
        if (m_instance && m_surface)
            vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
#endif
        m_surface = {};
        m_currentFrame.active = false;
        m_info.hasSurface = false;
        m_info.hasSwapchain = false;
        m_info.swapchain = {};
        m_surfaceLost = true;
    }

    void VulkanRenderer::invalidateSurface()
    { abandonSurface(); }

    void VulkanRenderer::createFrameSync()
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (nullptr == m_device)
            throw std::logic_error("Cannot create Vulkan frame sync before the logical device");

        VkSemaphoreCreateInfo createInfo {};
        createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        if (VK_SUCCESS != vkCreateSemaphore(m_device, &createInfo, nullptr, &m_imageAvailableSemaphore))
            throw std::runtime_error("Failed to create Vulkan frame semaphore");
        m_gpu = std::make_unique<GpuState>();
        m_gpu->initialize(m_device, m_physicalDevice, m_queueFamilies.graphicsFamily);
#endif
    }

    void VulkanRenderer::destroyFrameSync()
    {
        m_gpu.reset();
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (nullptr != m_device && nullptr != m_imageAvailableSemaphore)
            vkDestroySemaphore(m_device, m_imageAvailableSemaphore, nullptr);
#endif
        m_imageAvailableSemaphore = {};
    }

    bool VulkanRenderer::framebufferReady() const
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (m_surfaceProvider)
            return m_surfaceProvider->drawableExtent().valid();
#ifndef CPP_GAME_ENGINE_MOBILE
        if (nullptr == m_window || glfwGetWindowAttrib(m_window, GLFW_ICONIFIED))
            return false;

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(m_window, &width, &height);
        return width > 0 && height > 0;
#else
        return false;
#endif
#else
        return false;
#endif
    }

    void VulkanRenderer::shutdown()
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (m_device && m_frameActive && m_imageAvailableSemaphore)
        {
            // Acquisition can signal asynchronously. Consume it even when command
            // recording failed, before destroying its semaphore/swapchain.
            const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkSubmitInfo release { VK_STRUCTURE_TYPE_SUBMIT_INFO };
            release.waitSemaphoreCount = 1;
            release.pWaitSemaphores = &m_imageAvailableSemaphore;
            release.pWaitDstStageMask = &stage;
            vkQueueSubmit(m_graphicsQueue, 1, &release, VK_NULL_HANDLE);
        }
        if (nullptr != m_device)
            vkDeviceWaitIdle(m_device);

        destroyFrameSync();
        m_swapchain.reset();
        m_memoryManager.reset();

        if (nullptr != m_device)
            vkDestroyDevice(m_device, nullptr);

        if (nullptr != m_instance && nullptr != m_surface)
            vkDestroySurfaceKHR(m_instance, m_surface, nullptr);

        if (m_instance && m_validationCallback)
        {
            const auto destroyCallback = reinterpret_cast<PFN_vkDestroyDebugReportCallbackEXT>(
                vkGetInstanceProcAddr(m_instance, "vkDestroyDebugReportCallbackEXT"));
            if (destroyCallback) destroyCallback(m_instance, m_validationCallback, nullptr);
        }

        if (nullptr != m_instance)
            vkDestroyInstance(m_instance, nullptr);
#endif

        m_window = nullptr;
        m_surfaceProvider = nullptr;
        m_portabilitySubset = false;
        m_instance = {};
        m_validationCallback = {};
        m_surface = {};
        m_physicalDevice = {};
        m_device = {};
        m_graphicsQueue = {};
        m_presentQueue = {};
        m_imageAvailableSemaphore = {};
        m_queueFamilies = {};
        m_memoryManager.reset();
        m_currentFrame = {};
        m_info = {};
        m_pendingUniformWrites.clear();
        m_nextFrameIndex = 0;
        m_renderCallCount = 0;
        m_frameActive = false;
        m_initialized = false;
        m_surfaceLost = false;
    }

    bool VulkanRenderer::beginRenderFrame()
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (!m_initialized)
            throw std::logic_error("Cannot begin a Vulkan render frame before initialization");

        if (m_frameActive)
            throw std::logic_error("Cannot begin a Vulkan render frame while another frame is active");

        if (m_surfaceLost)
            return false;

        if (!framebufferReady())
            return false;

        // A resize may still acquire the old swapchain successfully/suboptimally.
        // Rebuild before applying framebuffer-sized authored viewports.
        const auto drawable = m_surfaceProvider ? m_surfaceProvider->drawableExtent() : rendering::ImageExtent{};
        if (!m_swapchain.valid() || (m_surfaceProvider &&
            (drawable.width != m_swapchain.extent().width || drawable.height != m_swapchain.extent().height)))
            recreateSwapchain();

        if (!m_swapchain.valid())
            return false;

        require_vk(vkWaitForFences(m_device, 1, &m_gpu->completed, VK_TRUE, UINT64_MAX), "Wait for frame completion");

        uint32_t imageIndex = 0;
        const VkResult result = vkAcquireNextImageKHR(
            m_device,
            m_swapchain.handle(),
            50'000'000, // Bound shutdown latency while a mobile surface disappears.
            m_imageAvailableSemaphore,
            VK_NULL_HANDLE,
            &imageIndex
        );

        if (VK_ERROR_OUT_OF_DATE_KHR == result)
        {
            recreateSwapchain();
            return false;
        }
        if (VK_ERROR_SURFACE_LOST_KHR == result)
        {
            abandonSurface();
            return false;
        }
        if (VK_TIMEOUT == result)
            return false;

        if (VK_SUCCESS != result && VK_SUBOPTIMAL_KHR != result)
            throw std::runtime_error("Failed to acquire a Vulkan swapchain image");

        m_currentFrame = {
            m_nextFrameIndex++,
            imageIndex,
            true
        };
        m_frameActive = true;
        m_pendingUniformWrites.clear();
        m_gpu->suboptimal = result == VK_SUBOPTIMAL_KHR;
        try
        {
            m_gpu->begin(m_swapchain, imageIndex);
        }
        catch (...)
        {
            shutdown();
            throw;
        }
        return true;
#else
        throw std::runtime_error("Vulkan render frames require CPP_GAME_ENGINE_USE_VULKAN");
#endif
    }

    void VulkanRenderer::endRenderFrame()
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (!m_frameActive)
            throw std::logic_error("Cannot end a Vulkan render frame before one has begun");

        try
        {
            vkCmdEndRenderPass(m_gpu->command);
            require_vk(vkEndCommandBuffer(m_gpu->command), "End command buffer");
            const uint32_t imageIndex = m_currentFrame.imageIndex;
            const VkSemaphore rendered = m_gpu->rendered.at(imageIndex);
            const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo submit { VK_STRUCTURE_TYPE_SUBMIT_INFO };
            submit.waitSemaphoreCount = 1;
            submit.pWaitSemaphores = &m_imageAvailableSemaphore;
            submit.pWaitDstStageMask = &waitStage;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &m_gpu->command;
            submit.signalSemaphoreCount = 1;
            submit.pSignalSemaphores = &rendered;
            require_vk(vkResetFences(m_device, 1, &m_gpu->completed), "Reset frame fence");
            require_vk(vkQueueSubmit(m_graphicsQueue, 1, &submit, m_gpu->completed), "Submit render commands");
            m_frameActive = false; // The acquisition semaphore now belongs to this submission.

            const VkSwapchainKHR swapchain = m_swapchain.handle();
            VkPresentInfoKHR presentInfo { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
            presentInfo.waitSemaphoreCount = 1;
            presentInfo.pWaitSemaphores = &rendered;
            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains = &swapchain;
            presentInfo.pImageIndices = &imageIndex;
            const VkResult result = vkQueuePresentKHR(m_presentQueue, &presentInfo);
            m_currentFrame.active = false;
            m_frameActive = false;
            if (VK_ERROR_SURFACE_LOST_KHR == result)
                abandonSurface();
            else if (VK_ERROR_OUT_OF_DATE_KHR == result || VK_SUBOPTIMAL_KHR == result ||
                (VK_SUCCESS == result && m_gpu->suboptimal))
                recreateSwapchain();
            else
                require_vk(result, "Present swapchain image");
        }
        catch (...)
        {
            // A failed submit may leave an unsignaled fence: never wait on it next frame.
            shutdown();
            throw;
        }
#else
        throw std::runtime_error("Vulkan render frames require CPP_GAME_ENGINE_USE_VULKAN");
#endif
    }

    void VulkanRenderer::cancelRenderFrame()
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (!m_frameActive)
            return;

        try
        {
            // Discard the recorded draws, but consume the acquire semaphore and release
            // the acquired image through a valid clear/submit/present cycle.
            m_gpu->begin(m_swapchain, m_currentFrame.imageIndex);
            endRenderFrame();
        }
        catch (...)
        { shutdown(); }
#endif
    }

    void VulkanRenderer::waitIdle()
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (nullptr != m_device)
            vkDeviceWaitIdle(m_device);
#endif
    }

    bool VulkanRenderer::uploadUniformImpl(
        rendering::IShader& shader,
        const rendering::ShaderUniformUpload& upload
    )
    {
        rendering::ShaderUniformWrite write = rendering::capture_uniform_write(upload, shader.name());
        m_pendingUniformWrites.append(std::move(write));
        return true;
    }

    void VulkanRenderer::renderImpl(rendering::IShader& shader)
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (!m_initialized || !m_frameActive)
            throw std::logic_error("Vulkan draws require an active render frame");
        const auto* program = dynamic_cast<const VulkanShaderProgram*>(&shader);
        if (!program)
            throw std::invalid_argument("VulkanRenderer requires a VulkanShaderProgram");
        m_gpu->draw(*program, m_memoryManager);
        ++m_renderCallCount;
#else
        (void)shader;
        throw std::runtime_error("Vulkan draws require CPP_GAME_ENGINE_USE_VULKAN");
#endif
    }

    void VulkanRenderer::drawMesh(
        VulkanShaderProgram& shader,
        const rendering::SerializedBufferView& vertices,
        const rendering::VertexLayout& layout
    ) {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (!m_initialized || !m_frameActive)
            throw std::logic_error("Vulkan mesh draws require an active render frame");
        if (vertices.empty() || nullptr == vertices.data || !layout.valid() ||
            vertices.elementStride != layout.stride ||
            vertices.elementCount > std::numeric_limits<uint32_t>::max() ||
            vertices.elementCount > std::numeric_limits<size_t>::max() / layout.stride ||
            vertices.byteSize != vertices.elementCount * layout.stride)
            throw std::invalid_argument("Vulkan mesh data does not match its explicit vertex layout");
        m_gpu->draw(shader, m_memoryManager, &vertices, &layout);
        ++m_renderCallCount;
#else
        (void)shader;
        (void)vertices;
        (void)layout;
        throw std::runtime_error("Vulkan mesh draws require CPP_GAME_ENGINE_USE_VULKAN");
#endif
    }

    void VulkanRenderer::drawMesh(
        VulkanShaderProgram& shader,
        const rendering::SerializedBufferView& vertices,
        const rendering::VertexLayout& layout,
        const rendering::Texture2D& texture
    ) {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (!m_initialized || !m_frameActive)
            throw std::logic_error("Vulkan textured mesh draws require an active render frame");
        if (vertices.empty() || nullptr == vertices.data || !layout.valid() ||
            vertices.elementStride != layout.stride ||
            vertices.elementCount > std::numeric_limits<uint32_t>::max() ||
            vertices.elementCount > std::numeric_limits<size_t>::max() / layout.stride ||
            vertices.byteSize != vertices.elementCount * layout.stride)
            throw std::invalid_argument("Vulkan mesh data does not match its explicit vertex layout");
        if (!texture.valid())
            throw std::invalid_argument("Vulkan textures require complete row-major RGBA8 pixels");
        m_gpu->draw(shader, m_memoryManager, &vertices, &layout, &texture,
            m_graphicsQueue, m_queueFamilies.graphicsFamily);
        ++m_renderCallCount;
#else
        (void)shader;
        (void)vertices;
        (void)layout;
        (void)texture;
        throw std::runtime_error("Vulkan textured mesh draws require CPP_GAME_ENGINE_USE_VULKAN");
#endif
    }

    void VulkanRenderer::cacheMesh(uint64_t id, const rendering::SerializedBufferView& vertices,
        const rendering::VertexLayout& layout)
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (!m_initialized || m_frameActive || !id)
            throw std::logic_error("Mesh cache changes require an idle initialized renderer");
        if (vertices.empty() || !vertices.data || !layout.valid() ||
            vertices.elementStride != layout.stride ||
            vertices.elementCount > std::numeric_limits<uint32_t>::max() ||
            vertices.elementCount > std::numeric_limits<size_t>::max() / layout.stride ||
            vertices.byteSize != vertices.elementCount * layout.stride)
            throw std::invalid_argument("Cached mesh data does not match vertex layout");
        rendering::GpuBufferDescription description;
        description.byteSize = vertices.byteSize;
        description.usage = rendering::GpuBufferUsage::Vertex;
        description.memoryUsage = rendering::GpuMemoryUsage::CpuToGpu;
        auto buffer = m_memoryManager.createVulkanBuffer(description);
        m_memoryManager.writeBuffer(*buffer, vertices);
        m_gpu->cachedMeshes[id] = { std::move(buffer), layout,
            static_cast<uint32_t>(vertices.elementCount) };
#else
        (void)id; (void)vertices; (void)layout;
        throw std::runtime_error("Vulkan mesh cache requires CPP_GAME_ENGINE_USE_VULKAN");
#endif
    }

    void VulkanRenderer::cacheTexture(uint64_t id, const rendering::Texture2D& texture)
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (!m_initialized || m_frameActive || !id)
            throw std::logic_error("Texture cache changes require an idle initialized renderer");
        if (!texture.valid()) throw std::invalid_argument("Cached texture requires RGBA8 pixels");
        m_gpu->cachedTextures[id] = m_gpu->createSampledTexture(texture, m_memoryManager,
            m_graphicsQueue, m_queueFamilies.graphicsFamily);
#else
        (void)id; (void)texture;
        throw std::runtime_error("Vulkan texture cache requires CPP_GAME_ENGINE_USE_VULKAN");
#endif
    }

    void VulkanRenderer::releaseMesh(uint64_t id)
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        m_gpu->cachedMeshes.erase(id);
#else
        (void)id;
#endif
    }

    void VulkanRenderer::releaseTexture(uint64_t id)
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        m_gpu->cachedTextures.erase(id);
#else
        (void)id;
#endif
    }

    void VulkanRenderer::drawProcedural(VulkanShaderProgram& shader, const rendering::DrawState& state)
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (!m_initialized || !m_frameActive)
            throw std::logic_error("Vulkan draws require an active render frame");
        m_gpu->draw(shader, m_memoryManager, nullptr, nullptr, nullptr,
            VK_NULL_HANDLE, 0, nullptr, nullptr, state);
        ++m_renderCallCount;
#else
        (void)shader; (void)state;
        throw std::runtime_error("Vulkan draws require CPP_GAME_ENGINE_USE_VULKAN");
#endif
    }
    void VulkanRenderer::drawCached(VulkanShaderProgram& shader, uint64_t meshId, uint64_t textureId,
        const rendering::DrawState& state)
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (!m_initialized || !m_frameActive)
            throw std::logic_error("Cached draws require an active render frame");
        const auto mesh = m_gpu->cachedMeshes.find(meshId);
        if (mesh == m_gpu->cachedMeshes.end()) throw std::out_of_range("Unknown cached mesh");
        const GpuState::SampledTexture* texture = nullptr;
        if (textureId)
        {
            const auto found = m_gpu->cachedTextures.find(textureId);
            if (found == m_gpu->cachedTextures.end()) throw std::out_of_range("Unknown cached texture");
            texture = found->second.get();
        }
        m_gpu->draw(shader, m_memoryManager, nullptr, &mesh->second.layout, nullptr,
            m_graphicsQueue, m_queueFamilies.graphicsFamily, &mesh->second, texture, state);
        ++m_renderCallCount;
#else
        (void)shader; (void)meshId; (void)textureId; (void)state;
        throw std::runtime_error("Vulkan cached draws require CPP_GAME_ENGINE_USE_VULKAN");
#endif
    }
}
