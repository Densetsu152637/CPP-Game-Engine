//
// Created by Nicholas on 26/04/26.
//

#pragma once


#include <functional>
#include <mutex>
#include <memory>

template <typename T>
class DoubleBuffer
{
    T m_readBuffer;
    T m_writeBuffer;

    std::function<void(const T&, T&)> copier; // optional

    mutable std::mutex m_mutex;

public:

    using Supplier = std::function<T()>;
    using Consumer = std::function<void(T&)>;

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
                 std::function<void(const T&, T&)> copierFn)
        : m_readBuffer(factory()),
          m_writeBuffer(factory()),
          copier(std::move(copierFn))
    {}

    // =========================================
    // Read (volatile equivalent)
    // =========================================
    T read() const
    {
        // mimic Java volatile read semantics
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_readBuffer;
    }

    // =========================================
    // Write via consumer
    // =========================================
    void write(const Consumer& consumer)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        consumer(m_writeBuffer);
    }

    // =========================================
    // Direct write
    // =========================================
    void write(T newWrite)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_writeBuffer = std::move(newWrite);
    }

    // =========================================
    // Swap buffers
    // =========================================
    void swap()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        std::swap(m_readBuffer, m_writeBuffer);

        // optional copy propagation
        if (isCopying())
        {
            copier(m_readBuffer, m_writeBuffer);
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

template <typename T>
class TripleBuffer
{
    T m_readCurrentBuffer;
    T m_readLastBuffer;
    T m_writeBuffer;

    std::function<void(const T&, T&)> copier;

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
                 std::function<T(const T&, T&)> copierFn)
        : m_readCurrentBuffer(factory()),
          m_readLastBuffer(factory()),
          m_writeBuffer(factory()),
          copier(std::move(copierFn))
    {}

    // =========================================
    // Read current frame
    // =========================================
    T read() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_readCurrentBuffer;
    }

    // =========================================
    // Read previous frame
    // =========================================
    T prev() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_readLastBuffer;
    }

    // =========================================
    // Write via consumer
    // =========================================
    void write(const Consumer& consumer)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        consumer(m_writeBuffer);
    }

    // =========================================
    // Direct write
    // =========================================
    void write(T newWrite)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_writeBuffer = std::move(newWrite);
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
        T oldLast = m_readLastBuffer;

        m_readLastBuffer    = m_readCurrentBuffer;
        m_readCurrentBuffer = m_writeBuffer;
        m_writeBuffer       = std::move(oldLast);

        // optional propagation
        if (isCopying())
        {
            copier(m_readCurrentBuffer, m_writeBuffer);
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