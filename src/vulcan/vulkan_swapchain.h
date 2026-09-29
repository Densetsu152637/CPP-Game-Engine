//
// Vulkan swapchain ownership and selection helpers.
//

#pragma once

#include <cstdint>
#include <vector>

#ifdef CPP_GAME_ENGINE_USE_VULKAN
#include <vulkan/vulkan.h>
#else
using VkDevice = void*;
using VkPhysicalDevice = void*;
using VkSurfaceKHR = void*;
using VkSwapchainKHR = void*;
using VkImage = void*;
using VkImageView = void*;
using VkFormat = uint32_t;
using VkPresentModeKHR = uint32_t;
struct VkExtent2D
{
    uint32_t width = 0;
    uint32_t height = 0;
};
struct VkSurfaceCapabilitiesKHR {};
struct VkSurfaceFormatKHR
{
    VkFormat format = 0;
    uint32_t colorSpace = 0;
};
#endif

#include "../rendering/swapchain.h"

namespace vulkan
{
    struct VulkanQueueFamilyIndices
    {
        uint32_t graphicsFamily = 0;
        uint32_t presentFamily = 0;
        bool hasGraphicsFamily = false;
        bool hasPresentFamily = false;

        bool complete() const
        { return hasGraphicsFamily && hasPresentFamily; }

        bool usesSingleQueueFamily() const
        { return complete() && graphicsFamily == presentFamily; }
    };

    struct VulkanSwapchainSupportDetails
    {
        VkSurfaceCapabilitiesKHR capabilities {};
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;

        bool usable() const
        { return !formats.empty() && !presentModes.empty(); }
    };

    VulkanQueueFamilyIndices find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface);

    class VulkanSwapchain final : public rendering::ISwapchain
    {
        VkDevice m_device = {};
        VkSwapchainKHR m_swapchain = {};
        VkFormat m_imageFormat = {};
        VkExtent2D m_extent {};
        std::vector<VkImage> m_images;
        std::vector<VkImageView> m_imageViews;
        rendering::SwapchainInfo m_info;

        void createImageViews();

    public:
        VulkanSwapchain() = default;
        ~VulkanSwapchain() override;

        VulkanSwapchain(const VulkanSwapchain&) = delete;
        VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;
        VulkanSwapchain(VulkanSwapchain&& other) noexcept;
        VulkanSwapchain& operator=(VulkanSwapchain&& other) noexcept;

        void create(
            VkPhysicalDevice physicalDevice,
            VkDevice device,
            VkSurfaceKHR surface,
            rendering::ImageExtent drawableExtent,
            const VulkanQueueFamilyIndices& queueFamilies,
            const rendering::SwapchainConfig& config
        );
        void reset();

        bool valid() const override;
        const rendering::SwapchainInfo& info() const override
        { return m_info; }

        VkSwapchainKHR handle() const
        { return m_swapchain; }

        VkFormat imageFormat() const
        { return m_imageFormat; }

        VkExtent2D extent() const
        { return m_extent; }

        const std::vector<VkImage>& images() const
        { return m_images; }

        const std::vector<VkImageView>& imageViews() const
        { return m_imageViews; }

        static VulkanSwapchainSupportDetails querySupport(
            VkPhysicalDevice physicalDevice,
            VkSurfaceKHR surface
        );
        static VkSurfaceFormatKHR chooseSurfaceFormat(
            const std::vector<VkSurfaceFormatKHR>& formats
        );
        static VkPresentModeKHR choosePresentMode(
            const std::vector<VkPresentModeKHR>& presentModes,
            rendering::PresentMode preferredMode
        );
        static VkExtent2D chooseExtent(
            const VkSurfaceCapabilitiesKHR& capabilities,
            rendering::ImageExtent drawableExtent
        );
    };
}
