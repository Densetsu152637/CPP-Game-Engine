//
// Backend-neutral GPU buffer descriptions and serialization helpers.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <typeindex>
#include <vector>

#include "../structs/arraylist.h"

namespace rendering
{
    enum class GpuBufferUsage : uint32_t
    {
        None = 0,
        TransferSource = 1u << 0,
        TransferDestination = 1u << 1,
        Vertex = 1u << 2,
        Index = 1u << 3,
        Uniform = 1u << 4,
        Storage = 1u << 5
    };

    constexpr GpuBufferUsage operator|(const GpuBufferUsage lhs, const GpuBufferUsage rhs)
    {
        return static_cast<GpuBufferUsage>(
            static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs)
        );
    }

    constexpr GpuBufferUsage operator&(const GpuBufferUsage lhs, const GpuBufferUsage rhs)
    {
        return static_cast<GpuBufferUsage>(
            static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs)
        );
    }

    inline GpuBufferUsage& operator|=(GpuBufferUsage& lhs, const GpuBufferUsage rhs)
    {
        lhs = lhs | rhs;
        return lhs;
    }

    constexpr bool has_usage(const GpuBufferUsage usage, const GpuBufferUsage target)
    {
        return static_cast<uint32_t>(usage & target) != 0;
    }

    enum class GpuMemoryUsage
    {
        GpuOnly,
        CpuToGpu,
        GpuToCpu
    };

    struct GpuBufferDescription
    {
        size_t byteSize = 0;
        GpuBufferUsage usage = GpuBufferUsage::None;
        GpuMemoryUsage memoryUsage = GpuMemoryUsage::GpuOnly;

        bool valid() const
        { return byteSize > 0 && usage != GpuBufferUsage::None; }
    };

    struct SerializedBufferView
    {
        const std::byte* data = nullptr;
        size_t byteSize = 0;
        size_t elementCount = 0;
        size_t elementStride = 0;
        std::type_index elementType { typeid(void) };

        bool empty() const
        { return byteSize == 0 || elementCount == 0; }
    };

    template <typename T>
    constexpr bool gpu_serializable_v = std::is_trivially_copyable_v<T>;

    template <typename T>
    SerializedBufferView serialized_buffer_view(const T* data, const size_t count)
    {
        using Value = std::remove_cv_t<T>;
        static_assert(gpu_serializable_v<Value>, "GPU buffer data must be trivially copyable");

        if (0 == count)
            return { nullptr, 0, 0, sizeof(Value), std::type_index(typeid(Value)) };

        if (nullptr == data)
            throw std::invalid_argument("Cannot serialize GPU buffer data from a null pointer");

        return {
            reinterpret_cast<const std::byte*>(data),
            sizeof(Value) * count,
            count,
            sizeof(Value),
            std::type_index(typeid(Value))
        };
    }

    template <typename T>
    SerializedBufferView serialized_buffer_view(const std::vector<T>& values)
    {
        return serialized_buffer_view(values.data(), values.size());
    }

    template <typename T>
    SerializedBufferView serialized_buffer_view(const ArrayList<T>& values)
    {
        return serialized_buffer_view(values.ptr().ptr, values.length());
    }

    class IGpuBuffer
    {
    public:
        virtual ~IGpuBuffer() = default;

        virtual const GpuBufferDescription& description() const = 0;

        size_t size() const
        { return description().byteSize; }
    };

    class IGpuMemoryManager
    {
    public:
        virtual ~IGpuMemoryManager() = default;

        virtual std::unique_ptr<IGpuBuffer> createBuffer(const GpuBufferDescription& description) = 0;
        virtual void writeBuffer(
            IGpuBuffer& buffer,
            const SerializedBufferView& data,
            size_t byteOffset = 0
        ) = 0;

        std::unique_ptr<IGpuBuffer> createBufferFrom(
            const SerializedBufferView& data,
            GpuBufferUsage usage,
            GpuMemoryUsage memoryUsage = GpuMemoryUsage::CpuToGpu
        ) {
            if (data.empty())
                throw std::invalid_argument("Cannot create a GPU buffer from empty serialized data");

            GpuBufferDescription description;
            description.byteSize = data.byteSize;
            description.usage = usage;
            description.memoryUsage = memoryUsage;

            std::unique_ptr<IGpuBuffer> buffer = createBuffer(description);
            writeBuffer(*buffer, data);
            return buffer;
        }

        template <typename Container>
        std::unique_ptr<IGpuBuffer> createBufferFrom(
            const Container& data,
            const GpuBufferUsage usage,
            const GpuMemoryUsage memoryUsage = GpuMemoryUsage::CpuToGpu
        ) {
            return createBufferFrom(serialized_buffer_view(data), usage, memoryUsage);
        }
    };
}
