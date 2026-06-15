//
// Vulkan renderer, device, and swapchain orchestration.
//

#include "vulkan_renderer.h"

#include <cstring>
#include <stdexcept>
#include <utility>

namespace vulkan
{
    namespace
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        std::vector<const char*> glfw_required_instance_extensions()
        {
            uint32_t extensionCount = 0;
            const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
            if (nullptr == extensions || 0 == extensionCount)
                throw std::runtime_error("GLFW did not provide required Vulkan instance extensions");

            return std::vector<const char*>(extensions, extensions + extensionCount);
        }

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

    VulkanRenderer::~VulkanRenderer()
    {
        shutdown();
    }

    VulkanRenderer::VulkanRenderer(VulkanRenderer&& other) noexcept
        : m_window(other.m_window),
          m_instance(other.m_instance),
          m_surface(other.m_surface),
          m_physicalDevice(other.m_physicalDevice),
          m_device(other.m_device),
          m_graphicsQueue(other.m_graphicsQueue),
          m_presentQueue(other.m_presentQueue),
          m_imageAvailableSemaphore(other.m_imageAvailableSemaphore),
          m_queueFamilies(other.m_queueFamilies),
          m_swapchain(std::move(other.m_swapchain)),
          m_currentFrame(other.m_currentFrame),
          m_info(other.m_info),
          m_pendingUniformWrites(std::move(other.m_pendingUniformWrites)),
          m_swapchainConfig(other.m_swapchainConfig),
          m_nextFrameIndex(other.m_nextFrameIndex),
          m_renderCallCount(other.m_renderCallCount),
          m_frameActive(other.m_frameActive),
          m_initialized(other.m_initialized)
    {
        other.m_window = nullptr;
        other.m_instance = {};
        other.m_surface = {};
        other.m_physicalDevice = {};
        other.m_device = {};
        other.m_graphicsQueue = {};
        other.m_presentQueue = {};
        other.m_imageAvailableSemaphore = {};
        other.m_queueFamilies = {};
        other.m_currentFrame = {};
        other.m_info = {};
        other.m_nextFrameIndex = 0;
        other.m_renderCallCount = 0;
        other.m_frameActive = false;
        other.m_initialized = false;
    }

    VulkanRenderer& VulkanRenderer::operator=(VulkanRenderer&& other) noexcept
    {
        if (this == &other)
            return *this;

        shutdown();
        m_window = other.m_window;
        m_instance = other.m_instance;
        m_surface = other.m_surface;
        m_physicalDevice = other.m_physicalDevice;
        m_device = other.m_device;
        m_graphicsQueue = other.m_graphicsQueue;
        m_presentQueue = other.m_presentQueue;
        m_imageAvailableSemaphore = other.m_imageAvailableSemaphore;
        m_queueFamilies = other.m_queueFamilies;
        m_swapchain = std::move(other.m_swapchain);
        m_currentFrame = other.m_currentFrame;
        m_info = other.m_info;
        m_pendingUniformWrites = std::move(other.m_pendingUniformWrites);
        m_swapchainConfig = other.m_swapchainConfig;
        m_nextFrameIndex = other.m_nextFrameIndex;
        m_renderCallCount = other.m_renderCallCount;
        m_frameActive = other.m_frameActive;
        m_initialized = other.m_initialized;

        other.m_window = nullptr;
        other.m_instance = {};
        other.m_surface = {};
        other.m_physicalDevice = {};
        other.m_device = {};
        other.m_graphicsQueue = {};
        other.m_presentQueue = {};
        other.m_imageAvailableSemaphore = {};
        other.m_queueFamilies = {};
        other.m_currentFrame = {};
        other.m_info = {};
        other.m_nextFrameIndex = 0;
        other.m_renderCallCount = 0;
        other.m_frameActive = false;
        other.m_initialized = false;
        return *this;
    }

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
        createInstance(config);
        createSurface(window);
        pickPhysicalDevice(config);
        createLogicalDevice(config);
        createFrameSync();
        createSwapchain(config.swapchain);
        m_initialized = true;
#else
        (void)window;
        (void)config;
        throw std::runtime_error("VulkanRenderer requires CPP_GAME_ENGINE_USE_VULKAN");
#endif
    }

    void VulkanRenderer::createInstance(const VulkanRendererConfig& config)
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        std::vector<const char*> extensions = glfw_required_instance_extensions();
        for (const char* extension : config.extraInstanceExtensions)
            append_unique_extension(extensions, extension);

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
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.empty() ? nullptr : validationLayers.data();

        if (VK_SUCCESS != vkCreateInstance(&createInfo, nullptr, &m_instance))
            throw std::runtime_error("Failed to create Vulkan instance");
#else
        (void)config;
#endif
    }

    void VulkanRenderer::createSurface(GLFWwindow* window)
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (nullptr == m_instance)
            throw std::logic_error("Cannot create a Vulkan surface before the instance");

        if (VK_SUCCESS != glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface))
            throw std::runtime_error("Failed to create Vulkan window surface");

        m_info.hasSurface = true;
#else
        (void)window;
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

        for (const VkPhysicalDevice device : devices)
        {
            const VulkanQueueFamilyIndices queueFamilies = find_queue_families(device, m_surface);
            if (!queueFamilies.complete())
                continue;

            if (!device_supports_extensions(device, extensions))
                continue;

            if (!VulkanSwapchain::querySupport(device, m_surface).usable())
                continue;

            m_physicalDevice = device;
            m_queueFamilies = queueFamilies;
            m_info.graphicsQueueFamily = queueFamilies.graphicsFamily;
            m_info.presentQueueFamily = queueFamilies.presentFamily;
            return;
        }

        throw std::runtime_error("No Vulkan physical device supports graphics, presentation, and swapchain creation");
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
        const std::vector<const char*> extensions = required_device_extensions(config);

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

        m_swapchain.create(
            m_physicalDevice,
            m_device,
            m_surface,
            m_window,
            m_queueFamilies,
            config
        );
        m_info.hasSwapchain = true;
        m_info.swapchain = m_swapchain.info();
#else
        (void)config;
#endif
    }

    void VulkanRenderer::recreateSwapchain()
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (nullptr == m_device)
            return;

        vkDeviceWaitIdle(m_device);
        if (!framebufferReady())
        {
            m_swapchain.reset();
            m_info.hasSwapchain = false;
            m_info.swapchain = {};
            return;
        }

        createSwapchain(m_swapchainConfig);
#endif
    }

    void VulkanRenderer::createFrameSync()
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (nullptr == m_device)
            throw std::logic_error("Cannot create Vulkan frame sync before the logical device");

        VkSemaphoreCreateInfo createInfo {};
        createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        if (VK_SUCCESS != vkCreateSemaphore(m_device, &createInfo, nullptr, &m_imageAvailableSemaphore))
            throw std::runtime_error("Failed to create Vulkan frame semaphore");
#endif
    }

    void VulkanRenderer::destroyFrameSync()
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (nullptr != m_device && nullptr != m_imageAvailableSemaphore)
            vkDestroySemaphore(m_device, m_imageAvailableSemaphore, nullptr);
#endif
        m_imageAvailableSemaphore = {};
    }

    bool VulkanRenderer::framebufferReady() const
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (nullptr == m_window)
            return false;

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(m_window, &width, &height);
        return width > 0 && height > 0;
#else
        return false;
#endif
    }

    void VulkanRenderer::shutdown()
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (nullptr != m_device)
            vkDeviceWaitIdle(m_device);

        destroyFrameSync();
        m_swapchain.reset();

        if (nullptr != m_device)
            vkDestroyDevice(m_device, nullptr);

        if (nullptr != m_instance && nullptr != m_surface)
            vkDestroySurfaceKHR(m_instance, m_surface, nullptr);

        if (nullptr != m_instance)
            vkDestroyInstance(m_instance, nullptr);
#endif

        m_window = nullptr;
        m_instance = {};
        m_surface = {};
        m_physicalDevice = {};
        m_device = {};
        m_graphicsQueue = {};
        m_presentQueue = {};
        m_imageAvailableSemaphore = {};
        m_queueFamilies = {};
        m_currentFrame = {};
        m_info = {};
        m_pendingUniformWrites.clear();
        m_nextFrameIndex = 0;
        m_renderCallCount = 0;
        m_frameActive = false;
        m_initialized = false;
    }

    bool VulkanRenderer::beginRenderFrame()
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (!m_initialized)
            throw std::logic_error("Cannot begin a Vulkan render frame before initialization");

        if (m_frameActive)
            throw std::logic_error("Cannot begin a Vulkan render frame while another frame is active");

        if (!framebufferReady())
            return false;

        if (!m_swapchain.valid())
            recreateSwapchain();

        if (!m_swapchain.valid())
            return false;

        uint32_t imageIndex = 0;
        const VkResult result = vkAcquireNextImageKHR(
            m_device,
            m_swapchain.handle(),
            UINT64_MAX,
            m_imageAvailableSemaphore,
            VK_NULL_HANDLE,
            &imageIndex
        );

        if (VK_ERROR_OUT_OF_DATE_KHR == result)
        {
            recreateSwapchain();
            return false;
        }

        if (VK_SUCCESS != result && VK_SUBOPTIMAL_KHR != result)
            throw std::runtime_error("Failed to acquire a Vulkan swapchain image");

        m_currentFrame = {
            m_nextFrameIndex++,
            imageIndex,
            true
        };
        m_frameActive = true;
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

        VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphore };
        VkSwapchainKHR swapchains[] = { m_swapchain.handle() };
        const uint32_t imageIndex = m_currentFrame.imageIndex;

        VkPresentInfoKHR presentInfo {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = waitSemaphores;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchains;
        presentInfo.pImageIndices = &imageIndex;

        const VkResult result = vkQueuePresentKHR(m_presentQueue, &presentInfo);
        vkQueueWaitIdle(m_presentQueue);

        m_currentFrame.active = false;
        m_frameActive = false;

        if (VK_ERROR_OUT_OF_DATE_KHR == result || VK_SUBOPTIMAL_KHR == result)
        {
            destroyFrameSync();
            createFrameSync();
            recreateSwapchain();
            return;
        }

        if (VK_SUCCESS != result)
            throw std::runtime_error("Failed to present a Vulkan swapchain image");
#else
        throw std::runtime_error("Vulkan render frames require CPP_GAME_ENGINE_USE_VULKAN");
#endif
    }

    void VulkanRenderer::cancelRenderFrame()
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (!m_frameActive)
            return;

        m_currentFrame.active = false;
        m_frameActive = false;

        try
        {
            if (nullptr != m_device)
            {
                vkDeviceWaitIdle(m_device);
                destroyFrameSync();
                createFrameSync();
                recreateSwapchain();
            }
        }
        catch (...)
        {}
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
        (void)shader;
        ++m_renderCallCount;
    }
}
