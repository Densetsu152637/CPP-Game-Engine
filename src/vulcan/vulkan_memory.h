//
// Vulkan GPU buffer memory management.
//

#pragma once

#include <cstddef>
#include <memory>
#include <stdexcept>

#ifdef CPP_GAME_ENGINE_USE_VULKAN
#include <vulkan/vulkan.h>
#else
using VkPhysicalDevice = void*;
using VkDevice = void*;
using VkBuffer = void*;
using VkDeviceMemory = void*;
#endif

#include "../rendering/gpu_buffer.h"

namespace vulkan
{
    class VulkanBuffer final : public rendering::IGpuBuffer
    {
        VkDevice m_device = {};
        VkBuffer m_buffer = {};
        VkDeviceMemory m_memory = {};
        rendering::GpuBufferDescription m_description;

    public:
        VulkanBuffer() = default;
        VulkanBuffer(
            VkDevice device,
            VkBuffer buffer,
            VkDeviceMemory memory,
            rendering::GpuBufferDescription description
        );
        ~VulkanBuffer() override;

        VulkanBuffer(const VulkanBuffer&) = delete;
        VulkanBuffer& operator=(const VulkanBuffer&) = delete;
        VulkanBuffer(VulkanBuffer&& other) noexcept;
        VulkanBuffer& operator=(VulkanBuffer&& other) noexcept;

        const rendering::GpuBufferDescription& description() const override
        { return m_description; }

        VkBuffer handle() const
        { return m_buffer; }

        VkDeviceMemory memory() const
        { return m_memory; }

        bool valid() const;
        void reset();
    };

    class VulkanMemoryManager final : public rendering::IGpuMemoryManager
    {
        VkPhysicalDevice m_physicalDevice = {};
        VkDevice m_device = {};

#ifdef CPP_GAME_ENGINE_USE_VULKAN
        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
#endif

    public:
        VulkanMemoryManager() = default;
        VulkanMemoryManager(VkPhysicalDevice physicalDevice, VkDevice device);

        void initialize(VkPhysicalDevice physicalDevice, VkDevice device);
        void reset();

        bool initialized() const
        { return nullptr != m_physicalDevice && nullptr != m_device; }

        std::unique_ptr<rendering::IGpuBuffer> createBuffer(
            const rendering::GpuBufferDescription& description
        ) override;
        void writeBuffer(
            rendering::IGpuBuffer& buffer,
            const rendering::SerializedBufferView& data,
            size_t byteOffset = 0
        ) override;

        std::unique_ptr<VulkanBuffer> createVulkanBuffer(
            const rendering::GpuBufferDescription& description
        );

        template <typename Container>
        std::unique_ptr<VulkanBuffer> createVulkanBufferFrom(
            const Container& data,
            const rendering::GpuBufferUsage usage,
            const rendering::GpuMemoryUsage memoryUsage = rendering::GpuMemoryUsage::CpuToGpu
        ) {
            const rendering::SerializedBufferView view = rendering::serialized_buffer_view(data);
            if (view.empty())
                throw std::invalid_argument("Cannot create a Vulkan buffer from empty serialized data");

            rendering::GpuBufferDescription description;
            description.byteSize = view.byteSize;
            description.usage = usage;
            description.memoryUsage = memoryUsage;

            std::unique_ptr<VulkanBuffer> buffer = createVulkanBuffer(description);
            writeBuffer(*buffer, view);
            return buffer;
        }
    };
}
