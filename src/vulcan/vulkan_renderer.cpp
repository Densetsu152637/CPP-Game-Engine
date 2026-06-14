//
// Minimal Vulkan renderer bootstrap.
//

#include "vulkan_renderer.h"

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
                if (std::string(layer.layerName) == layerName)
                    return true;
            }

            return false;
        }
#endif
    }

    VulkanRenderer::~VulkanRenderer()
    {
        shutdown();
    }

    VulkanRenderer::VulkanRenderer(VulkanRenderer&& other) noexcept
        : m_instance(other.m_instance),
          m_surface(other.m_surface),
          m_physicalDevice(other.m_physicalDevice),
          m_info(other.m_info),
          m_pendingUniformWrites(std::move(other.m_pendingUniformWrites)),
          m_renderCallCount(other.m_renderCallCount),
          m_initialized(other.m_initialized)
    {
        other.m_instance = {};
        other.m_surface = {};
        other.m_physicalDevice = {};
        other.m_info = {};
        other.m_renderCallCount = 0;
        other.m_initialized = false;
    }

    VulkanRenderer& VulkanRenderer::operator=(VulkanRenderer&& other) noexcept
    {
        if (this == &other)
            return *this;

        shutdown();
        m_instance = other.m_instance;
        m_surface = other.m_surface;
        m_physicalDevice = other.m_physicalDevice;
        m_info = other.m_info;
        m_pendingUniformWrites = std::move(other.m_pendingUniformWrites);
        m_renderCallCount = other.m_renderCallCount;
        m_initialized = other.m_initialized;

        other.m_instance = {};
        other.m_surface = {};
        other.m_physicalDevice = {};
        other.m_info = {};
        other.m_renderCallCount = 0;
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

        createInstance(config);
        createSurface(window);
        pickPhysicalDevice();
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
        extensions.insert(
            extensions.end(),
            config.extraInstanceExtensions.begin(),
            config.extraInstanceExtensions.end()
        );

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

    void VulkanRenderer::pickPhysicalDevice()
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (nullptr == m_instance)
            throw std::logic_error("Cannot pick a Vulkan physical device before the instance");

        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
        m_info.physicalDeviceCount = deviceCount;
        if (0 == deviceCount)
            throw std::runtime_error("No Vulkan physical devices were found");

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());
        m_physicalDevice = devices.front();
#endif
    }

    void VulkanRenderer::shutdown()
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (nullptr != m_instance && nullptr != m_surface)
            vkDestroySurfaceKHR(m_instance, m_surface, nullptr);

        if (nullptr != m_instance)
            vkDestroyInstance(m_instance, nullptr);
#endif

        m_instance = {};
        m_surface = {};
        m_physicalDevice = {};
        m_info = {};
        m_initialized = false;
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
