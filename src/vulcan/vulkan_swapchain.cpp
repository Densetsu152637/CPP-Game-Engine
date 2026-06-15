//
// Vulkan swapchain ownership and selection helpers.
//

#include "vulkan_swapchain.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace vulkan
{
    namespace
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        rendering::PixelFormat to_rendering_format(const VkFormat format)
        {
            switch (format)
            {
                case VK_FORMAT_B8G8R8A8_SRGB:
                    return rendering::PixelFormat::Bgra8Srgb;
                case VK_FORMAT_R8G8B8A8_SRGB:
                    return rendering::PixelFormat::Rgba8Srgb;
                default:
                    return rendering::PixelFormat::Unknown;
            }
        }

        rendering::ColorSpace to_rendering_color_space(const VkColorSpaceKHR colorSpace)
        {
            switch (colorSpace)
            {
                case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:
                    return rendering::ColorSpace::SrgbNonlinear;
                default:
                    return rendering::ColorSpace::Unknown;
            }
        }

        rendering::PresentMode to_rendering_present_mode(const VkPresentModeKHR presentMode)
        {
            switch (presentMode)
            {
                case VK_PRESENT_MODE_IMMEDIATE_KHR:
                    return rendering::PresentMode::Immediate;
                case VK_PRESENT_MODE_MAILBOX_KHR:
                    return rendering::PresentMode::Mailbox;
                case VK_PRESENT_MODE_FIFO_KHR:
                    return rendering::PresentMode::Fifo;
                case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
                    return rendering::PresentMode::FifoRelaxed;
                default:
                    return rendering::PresentMode::Fifo;
            }
        }

        VkPresentModeKHR to_vk_present_mode(const rendering::PresentMode presentMode)
        {
            switch (presentMode)
            {
                case rendering::PresentMode::Immediate:
                    return VK_PRESENT_MODE_IMMEDIATE_KHR;
                case rendering::PresentMode::Mailbox:
                    return VK_PRESENT_MODE_MAILBOX_KHR;
                case rendering::PresentMode::Fifo:
                    return VK_PRESENT_MODE_FIFO_KHR;
                case rendering::PresentMode::FifoRelaxed:
                    return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
            }

            return VK_PRESENT_MODE_FIFO_KHR;
        }

        uint32_t clamp_image_count(
            const uint32_t desiredImageCount,
            const VkSurfaceCapabilitiesKHR& capabilities
        ) {
            uint32_t imageCount = std::max(capabilities.minImageCount, desiredImageCount);
            if (capabilities.maxImageCount > 0)
                imageCount = std::min(imageCount, capabilities.maxImageCount);

            return imageCount;
        }
#endif
    }

    VulkanQueueFamilyIndices find_queue_families(
        const VkPhysicalDevice device,
        const VkSurfaceKHR surface
    ) {
        VulkanQueueFamilyIndices indices;

#ifdef CPP_GAME_ENGINE_USE_VULKAN
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        for (uint32_t index = 0; index < queueFamilyCount; ++index)
        {
            if (!indices.hasGraphicsFamily && (queueFamilies[index].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                indices.graphicsFamily = index;
                indices.hasGraphicsFamily = true;
            }

            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &presentSupport);
            if (!indices.hasPresentFamily && presentSupport)
            {
                indices.presentFamily = index;
                indices.hasPresentFamily = true;
            }

            if (indices.complete())
                break;
        }
#else
        (void)device;
        (void)surface;
#endif

        return indices;
    }

    VulkanSwapchain::~VulkanSwapchain()
    {
        reset();
    }

    VulkanSwapchain::VulkanSwapchain(VulkanSwapchain&& other) noexcept
        : m_device(other.m_device),
          m_swapchain(other.m_swapchain),
          m_imageFormat(other.m_imageFormat),
          m_extent(other.m_extent),
          m_images(std::move(other.m_images)),
          m_imageViews(std::move(other.m_imageViews)),
          m_info(other.m_info)
    {
        other.m_device = {};
        other.m_swapchain = {};
        other.m_imageFormat = {};
        other.m_extent = {};
        other.m_info = {};
    }

    VulkanSwapchain& VulkanSwapchain::operator=(VulkanSwapchain&& other) noexcept
    {
        if (this == &other)
            return *this;

        reset();
        m_device = other.m_device;
        m_swapchain = other.m_swapchain;
        m_imageFormat = other.m_imageFormat;
        m_extent = other.m_extent;
        m_images = std::move(other.m_images);
        m_imageViews = std::move(other.m_imageViews);
        m_info = other.m_info;

        other.m_device = {};
        other.m_swapchain = {};
        other.m_imageFormat = {};
        other.m_extent = {};
        other.m_info = {};
        return *this;
    }

    void VulkanSwapchain::create(
        const VkPhysicalDevice physicalDevice,
        const VkDevice device,
        const VkSurfaceKHR surface,
        GLFWwindow* window,
        const VulkanQueueFamilyIndices& queueFamilies,
        const rendering::SwapchainConfig& config
    ) {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (nullptr == physicalDevice)
            throw std::invalid_argument("Cannot create a Vulkan swapchain with a null physical device");

        if (nullptr == device)
            throw std::invalid_argument("Cannot create a Vulkan swapchain with a null device");

        if (nullptr == surface)
            throw std::invalid_argument("Cannot create a Vulkan swapchain with a null surface");

        if (nullptr == window)
            throw std::invalid_argument("Cannot create a Vulkan swapchain without a GLFWwindow");

        if (!queueFamilies.complete())
            throw std::invalid_argument("Cannot create a Vulkan swapchain without graphics and present queues");

        reset();
        m_device = device;

        const VulkanSwapchainSupportDetails support = querySupport(physicalDevice, surface);
        if (!support.usable())
            throw std::runtime_error("Vulkan swapchain support is incomplete for the selected device");

        const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(support.formats);
        const VkPresentModeKHR presentMode = choosePresentMode(
            support.presentModes,
            config.preferredPresentMode
        );
        const VkExtent2D extent = chooseExtent(support.capabilities, window);
        if (0 == extent.width || 0 == extent.height)
        {
            m_device = {};
            throw std::runtime_error("Cannot create a Vulkan swapchain for a zero-sized framebuffer");
        }

        const uint32_t imageCount = clamp_image_count(
            config.desiredImageCount,
            support.capabilities
        );

        VkSwapchainCreateInfoKHR createInfo {};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        const uint32_t queueFamilyIndices[] = {
            queueFamilies.graphicsFamily,
            queueFamilies.presentFamily
        };

        if (queueFamilies.usesSingleQueueFamily())
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        else
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        }

        createInfo.preTransform = support.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        if (VK_SUCCESS != vkCreateSwapchainKHR(device, &createInfo, nullptr, &m_swapchain))
        {
            m_device = {};
            throw std::runtime_error("Failed to create Vulkan swapchain");
        }

        uint32_t actualImageCount = 0;
        vkGetSwapchainImagesKHR(device, m_swapchain, &actualImageCount, nullptr);
        m_images.resize(actualImageCount);
        vkGetSwapchainImagesKHR(device, m_swapchain, &actualImageCount, m_images.data());

        m_imageFormat = surfaceFormat.format;
        m_extent = extent;
        createImageViews();

        m_info.extent = { extent.width, extent.height };
        m_info.surfaceFormat = {
            to_rendering_format(surfaceFormat.format),
            to_rendering_color_space(surfaceFormat.colorSpace)
        };
        m_info.presentMode = to_rendering_present_mode(presentMode);
        m_info.imageCount = actualImageCount;
#else
        (void)physicalDevice;
        (void)device;
        (void)surface;
        (void)window;
        (void)queueFamilies;
        (void)config;
        throw std::runtime_error("Vulkan swapchains require CPP_GAME_ENGINE_USE_VULKAN");
#endif
    }

    void VulkanSwapchain::createImageViews()
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        m_imageViews.reserve(m_images.size());
        for (const VkImage image : m_images)
        {
            VkImageViewCreateInfo createInfo {};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = image;
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = m_imageFormat;
            createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            VkImageView imageView = VK_NULL_HANDLE;
            if (VK_SUCCESS != vkCreateImageView(m_device, &createInfo, nullptr, &imageView))
                throw std::runtime_error("Failed to create Vulkan swapchain image view");

            m_imageViews.push_back(imageView);
        }
#endif
    }

    void VulkanSwapchain::reset()
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (nullptr != m_device)
        {
            for (const VkImageView imageView : m_imageViews)
                vkDestroyImageView(m_device, imageView, nullptr);

            if (nullptr != m_swapchain)
                vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        }
#endif

        m_device = {};
        m_swapchain = {};
        m_imageFormat = {};
        m_extent = {};
        m_images.clear();
        m_imageViews.clear();
        m_info = {};
    }

    bool VulkanSwapchain::valid() const
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        return nullptr != m_swapchain && m_info.valid();
#else
        return false;
#endif
    }

    VulkanSwapchainSupportDetails VulkanSwapchain::querySupport(
        const VkPhysicalDevice physicalDevice,
        const VkSurfaceKHR surface
    ) {
        VulkanSwapchainSupportDetails details;

#ifdef CPP_GAME_ENGINE_USE_VULKAN
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            physicalDevice,
            surface,
            &details.capabilities
        );

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
        if (formatCount > 0)
        {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(
                physicalDevice,
                surface,
                &formatCount,
                details.formats.data()
            );
        }

        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            physicalDevice,
            surface,
            &presentModeCount,
            nullptr
        );
        if (presentModeCount > 0)
        {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(
                physicalDevice,
                surface,
                &presentModeCount,
                details.presentModes.data()
            );
        }
#else
        (void)physicalDevice;
        (void)surface;
#endif

        return details;
    }

    VkSurfaceFormatKHR VulkanSwapchain::chooseSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& formats
    ) {
        if (formats.empty())
            throw std::invalid_argument("Cannot choose a Vulkan surface format from an empty list");

#ifdef CPP_GAME_ENGINE_USE_VULKAN
        for (const VkSurfaceFormatKHR& format : formats)
        {
            if (format.format == VK_FORMAT_B8G8R8A8_SRGB
                && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return format;
            }
        }
#endif

        return formats.front();
    }

    VkPresentModeKHR VulkanSwapchain::choosePresentMode(
        const std::vector<VkPresentModeKHR>& presentModes,
        const rendering::PresentMode preferredMode
    ) {
        if (presentModes.empty())
            throw std::invalid_argument("Cannot choose a Vulkan present mode from an empty list");

#ifdef CPP_GAME_ENGINE_USE_VULKAN
        const VkPresentModeKHR preferred = to_vk_present_mode(preferredMode);
        for (const VkPresentModeKHR presentMode : presentModes)
        {
            if (presentMode == preferred)
                return presentMode;
        }

        for (const VkPresentModeKHR presentMode : presentModes)
        {
            if (presentMode == VK_PRESENT_MODE_FIFO_KHR)
                return presentMode;
        }
#else
        (void)preferredMode;
#endif

        return presentModes.front();
    }

    VkExtent2D VulkanSwapchain::chooseExtent(
        const VkSurfaceCapabilitiesKHR& capabilities,
        GLFWwindow* window
    ) {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
            return capabilities.currentExtent;

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);

        VkExtent2D extent {
            static_cast<uint32_t>(std::max(width, 0)),
            static_cast<uint32_t>(std::max(height, 0))
        };
        extent.width = std::clamp(
            extent.width,
            capabilities.minImageExtent.width,
            capabilities.maxImageExtent.width
        );
        extent.height = std::clamp(
            extent.height,
            capabilities.minImageExtent.height,
            capabilities.maxImageExtent.height
        );
        return extent;
#else
        (void)capabilities;
        (void)window;
        return {};
#endif
    }
}
