//
// Created by Nicholas on 18/04/26.
//

#pragma once

#include <functional>
#include <mutex>

template <typename T>
class TripleBuffer
{
    T readCurrentBuffer;
    T readLastBuffer;
    T writeBuffer;

    std::function<T(const T&, const T&)> copier;

    mutable std::mutex m_mutex;

public:

    using Factory  = std::function<T()>;
    using Consumer = std::function<void(T&)>;

    // =========================================
    // Constructor WITHOUT copy behavior
    // =========================================
    explicit TripleBuffer(Factory factory)
        : TripleBuffer(factory, nullptr)
    {}

    // =========================================
    // Constructor WITH copy behavior
    // =========================================
    TripleBuffer(Factory factory,
                 std::function<T(const T&, const T&)> copierFn)
        : readCurrentBuffer(factory()),
          readLastBuffer(factory()),
          writeBuffer(factory()),
          copier(std::move(copierFn))
    {}

    // =========================================
    // Read current frame
    // =========================================
    T read() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return readCurrentBuffer;
    }

    // =========================================
    // Read previous frame
    // =========================================
    T prev() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return readLastBuffer;
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

        // rotate buffers:
        // last <- current
        // current <- write
        // write <- old last
        T oldLast = readLastBuffer;

        readLastBuffer    = readCurrentBuffer;
        readCurrentBuffer = writeBuffer;
        writeBuffer       = std::move(oldLast);

        // optional propagation
        if (isCopying())
        {
            writeBuffer = copier(readCurrentBuffer, writeBuffer);
        }
    }

    // =========================================
    // Copier check
    // =========================================
    bool isCopying() const
    {
        return static_cast<bool>(copier);
    }
};
