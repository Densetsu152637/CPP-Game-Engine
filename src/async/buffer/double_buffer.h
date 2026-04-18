//
// Created by Nicholas on 18/04/26.
//

#ifndef CPP_GAME_ENGINE_DOUBLE_BUFFER_H
#define CPP_GAME_ENGINE_DOUBLE_BUFFER_H

#include <functional>
#include <mutex>
#include <memory>

template <typename T>
class DoubleBuffer
{
    T readBuffer;
    T writeBuffer;

    std::function<T(const T&, const T&)> copier; // optional

    mutable std::mutex m_mutex;

public:

    using Supplier = std::function<T()>;
    using Consumer  = std::function<void(T&)>;

    // =========================================
    // Constructor WITHOUT copy behavior
    // =========================================
    explicit DoubleBuffer(Supplier factory)
        : DoubleBuffer(factory, nullptr)
    {}

    // =========================================
    // Constructor WITH copy behavior
    // =========================================
    DoubleBuffer(Supplier factory,
                 std::function<T(const T&, const T&)> copierFn)
        : readBuffer(factory()),
          writeBuffer(factory()),
          copier(std::move(copierFn))
    {}

    // =========================================
    // Read (volatile equivalent)
    // =========================================
    T read() const
    {
        // mimic Java volatile read semantics
        std::lock_guard<std::mutex> lock(m_mutex);
        return readBuffer;
    }

    // =========================================
    // Write via consumer
    // =========================================
    void write(const Consumer& consumer)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        consumer(writeBuffer);
    }

    // =========================================
    // Direct write
    // =========================================
    void write(T newWrite)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        writeBuffer = std::move(newWrite);
    }

    // =========================================
    // Swap buffers
    // =========================================
    void swap()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        std::swap(readBuffer, writeBuffer);

        // optional copy propagation
        if (isCopying())
        {
            writeBuffer = copier(readBuffer, writeBuffer);
        }
    }

    // =========================================
    // Check if copier is active
    // =========================================
    bool isCopying() const
    {
        return static_cast<bool>(copier);
    }
};

#endif //CPP_GAME_ENGINE_DOUBLE_BUFFER_H
