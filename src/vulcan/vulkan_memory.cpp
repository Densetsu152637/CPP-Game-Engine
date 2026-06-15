//
// Vulkan GPU buffer memory management.
//

#include "vulkan_memory.h"

#include <cstring>
#include <stdexcept>
#include <utility>

namespace vulkan
{
    namespace
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        VkBufferUsageFlags to_vk_buffer_usage(const rendering::GpuBufferUsage usage)
        {
            VkBufferUsageFlags flags = 0;

            if (rendering::has_usage(usage, rendering::GpuBufferUsage::TransferSource))
                flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

            if (rendering::has_usage(usage, rendering::GpuBufferUsage::TransferDestination))
                flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

            if (rendering::has_usage(usage, rendering::GpuBufferUsage::Vertex))
                flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

            if (rendering::has_usage(usage, rendering::GpuBufferUsage::Index))
                flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

            if (rendering::has_usage(usage, rendering::GpuBufferUsage::Uniform))
                flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

            if (rendering::has_usage(usage, rendering::GpuBufferUsage::Storage))
                flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

            return flags;
        }

        VkMemoryPropertyFlags to_vk_memory_properties(const rendering::GpuMemoryUsage usage)
        {
            switch (usage)
            {
                case rendering::GpuMemoryUsage::GpuOnly:
                    return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                case rendering::GpuMemoryUsage::CpuToGpu:
                    return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                case rendering::GpuMemoryUsage::GpuToCpu:
                    return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                        VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
            }

            return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        }
#endif
    }

    VulkanBuffer::VulkanBuffer(
        const VkDevice device,
        const VkBuffer buffer,
        const VkDeviceMemory memory,
        rendering::GpuBufferDescription description
    )
        : m_device(device),
          m_buffer(buffer),
          m_memory(memory),
          m_description(description)
    {}

    VulkanBuffer::~VulkanBuffer()
    {
        reset();
    }

    VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept
        : m_device(other.m_device),
          m_buffer(other.m_buffer),
          m_memory(other.m_memory),
          m_description(other.m_description)
    {
        other.m_device = {};
        other.m_buffer = {};
        other.m_memory = {};
        other.m_description = {};
    }

    VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept
    {
        if (this == &other)
            return *this;

        reset();
        m_device = other.m_device;
        m_buffer = other.m_buffer;
        m_memory = other.m_memory;
        m_description = other.m_description;

        other.m_device = {};
        other.m_buffer = {};
        other.m_memory = {};
        other.m_description = {};
        return *this;
    }

    bool VulkanBuffer::valid() const
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        return nullptr != m_device && nullptr != m_buffer && nullptr != m_memory;
#else
        return false;
#endif
    }

    void VulkanBuffer::reset()
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (nullptr != m_device)
        {
            if (nullptr != m_buffer)
                vkDestroyBuffer(m_device, m_buffer, nullptr);

            if (nullptr != m_memory)
                vkFreeMemory(m_device, m_memory, nullptr);
        }
#endif

        m_device = {};
        m_buffer = {};
        m_memory = {};
        m_description = {};
    }

    VulkanMemoryManager::VulkanMemoryManager(
        const VkPhysicalDevice physicalDevice,
        const VkDevice device
    ) {
        initialize(physicalDevice, device);
    }

    void VulkanMemoryManager::initialize(
        const VkPhysicalDevice physicalDevice,
        const VkDevice device
    ) {
        if (nullptr == physicalDevice)
            throw std::invalid_argument("VulkanMemoryManager requires a physical device");

        if (nullptr == device)
            throw std::invalid_argument("VulkanMemoryManager requires a logical device");

        m_physicalDevice = physicalDevice;
        m_device = device;
    }

    void VulkanMemoryManager::reset()
    {
        m_physicalDevice = {};
        m_device = {};
    }

    std::unique_ptr<rendering::IGpuBuffer> VulkanMemoryManager::createBuffer(
        const rendering::GpuBufferDescription& description
    ) {
        return createVulkanBuffer(description);
    }

    std::unique_ptr<VulkanBuffer> VulkanMemoryManager::createVulkanBuffer(
        const rendering::GpuBufferDescription& description
    ) {
        if (!description.valid())
            throw std::invalid_argument("Vulkan buffer descriptions require size and usage");

        if (!initialized())
            throw std::logic_error("VulkanMemoryManager is not initialized");

#ifdef CPP_GAME_ENGINE_USE_VULKAN
        VkBufferCreateInfo createInfo {};
        createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        createInfo.size = static_cast<VkDeviceSize>(description.byteSize);
        createInfo.usage = to_vk_buffer_usage(description.usage);
        createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer buffer = VK_NULL_HANDLE;
        if (VK_SUCCESS != vkCreateBuffer(m_device, &createInfo, nullptr, &buffer))
            throw std::runtime_error("Failed to create Vulkan buffer");

        VkMemoryRequirements requirements {};
        vkGetBufferMemoryRequirements(m_device, buffer, &requirements);

        VkMemoryAllocateInfo allocateInfo {};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = requirements.size;
        allocateInfo.memoryTypeIndex = findMemoryType(
            requirements.memoryTypeBits,
            to_vk_memory_properties(description.memoryUsage)
        );

        VkDeviceMemory memory = VK_NULL_HANDLE;
        if (VK_SUCCESS != vkAllocateMemory(m_device, &allocateInfo, nullptr, &memory))
        {
            vkDestroyBuffer(m_device, buffer, nullptr);
            throw std::runtime_error("Failed to allocate Vulkan buffer memory");
        }

        if (VK_SUCCESS != vkBindBufferMemory(m_device, buffer, memory, 0))
        {
            vkFreeMemory(m_device, memory, nullptr);
            vkDestroyBuffer(m_device, buffer, nullptr);
            throw std::runtime_error("Failed to bind Vulkan buffer memory");
        }

        return std::make_unique<VulkanBuffer>(m_device, buffer, memory, description);
#else
        throw std::runtime_error("Vulkan buffers require CPP_GAME_ENGINE_USE_VULKAN");
#endif
    }

    void VulkanMemoryManager::writeBuffer(
        rendering::IGpuBuffer& buffer,
        const rendering::SerializedBufferView& data,
        const size_t byteOffset
    ) {
        if (data.empty())
            return;

        auto* vulkanBuffer = dynamic_cast<VulkanBuffer*>(&buffer);
        if (nullptr == vulkanBuffer)
            throw std::invalid_argument("VulkanMemoryManager can only write VulkanBuffer instances");

        if (byteOffset + data.byteSize > vulkanBuffer->description().byteSize)
            throw std::out_of_range("Vulkan buffer write exceeds buffer size");

        if (vulkanBuffer->description().memoryUsage == rendering::GpuMemoryUsage::GpuOnly)
            throw std::logic_error("Cannot directly write to GPU-only Vulkan memory without a staging copy");

#ifdef CPP_GAME_ENGINE_USE_VULKAN
        void* mapped = nullptr;
        const VkDeviceSize offset = static_cast<VkDeviceSize>(byteOffset);
        const VkDeviceSize size = static_cast<VkDeviceSize>(data.byteSize);

        if (VK_SUCCESS != vkMapMemory(m_device, vulkanBuffer->memory(), offset, size, 0, &mapped))
            throw std::runtime_error("Failed to map Vulkan buffer memory");

        std::memcpy(mapped, data.data, data.byteSize);
        vkUnmapMemory(m_device, vulkanBuffer->memory());
#else
        (void)byteOffset;
        throw std::runtime_error("Vulkan buffer writes require CPP_GAME_ENGINE_USE_VULKAN");
#endif
    }

#ifdef CPP_GAME_ENGINE_USE_VULKAN
    uint32_t VulkanMemoryManager::findMemoryType(
        const uint32_t typeFilter,
        const VkMemoryPropertyFlags properties
    ) const {
        VkPhysicalDeviceMemoryProperties memoryProperties {};
        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memoryProperties);

        for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index)
        {
            const bool typeMatches = 0 != (typeFilter & (1u << index));
            const bool propertiesMatch =
                (memoryProperties.memoryTypes[index].propertyFlags & properties) == properties;

            if (typeMatches && propertiesMatch)
                return index;
        }

        throw std::runtime_error("No compatible Vulkan memory type was found");
    }
#endif
}
