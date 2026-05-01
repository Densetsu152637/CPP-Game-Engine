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
    size_t m_readBuffer = 0;
    size_t m_writeBuffer = 1;
    mutable std::mutex m_mutex;
    std::function<void(const T&, T&)> copier; // optional

    volatile T buff[2];

public:

    using Supplier = std::function<T()>;
    using Consumer = std::function<void(T&)>;

    // =========================================
    // Constructor WITHOUT copy behavior
    // =========================================
    explicit DoubleBuffer(Supplier factory)
    : DoubleBuffer(factory, [](const T& src, T& dst)
        {
            dst = src;
        })
    {}

    // =========================================
    // Constructor WITH copy behavior
    // =========================================
    DoubleBuffer(Supplier factory,
                 std::function<void(const T&, T&)> copierFn)
        : copier(std::move(copierFn))
    {
        buff = {
            factory(),
            factory()
        };
    }

    // =========================================
    // Read (volatile equivalent)
    // =========================================
    const T& read() const
    {
        return buff[m_readBuffer];
    }

    // =========================================
    // Write via consumer
    // =========================================
    void write(const Consumer& consumer)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        consumer(buff[m_writeBuffer]);
    }

    // =========================================
    // Direct write
    // =========================================
    void write(T&& newWrite)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        buff[m_writeBuffer] = newWrite;
    }

    // =========================================
    // Swap buffers
    // =========================================
    void swap()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        std::swap(m_readBuffer, m_writeBuffer);

        // optional copy propagation
        copier(m_readBuffer, m_writeBuffer);
    }
};



template <typename T>
class TripleBuffer
{
    size_t m_readCurrentBuffer = 0;
    size_t m_readLastBuffer = 1;
    size_t m_writeBuffer = 2;
    mutable std::mutex m_mutex;
    std::function<void(const T&, T&)> copier;

    volatile T buff[3];

public:

    using Factory  = std::function<T()>;
    using Consumer = std::function<void(T&)>;

    // =========================================
    // Constructor WITHOUT copy behavior
    // =========================================
    explicit TripleBuffer(Factory factory)
        : TripleBuffer(factory,
        [](const T& src, T& dst)
        {
            dst = src;
        })
    {}

    // =========================================
    // Constructor WITH copy behavior
    // =========================================
    TripleBuffer(Factory factory,
                 std::function<void(const T&, T&)> copierFn)
        : copier(std::move(copierFn))
    {
        buff = {
            factory(),
            factory(),
            factory()
        };
    }

    // =========================================
    // Read current frame
    // =========================================
    const T& read() const
    {
        return buff[m_readCurrentBuffer];
    }

    // =========================================
    // Read previous frame
    // =========================================
    const T& prev() const
    {
        return buff[m_readLastBuffer];
    }

    std::tuple<T, T> readLast() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return std::make_tuple(m_readLastBuffer, m_readCurrentBuffer);
    }

    // =========================================
    // Write via consumer
    // =========================================
    void write(const Consumer& consumer)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        consumer(buff[m_writeBuffer]);
    }

    // =========================================
    // Direct write
    // =========================================
    void write(T&& newWrite)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        buff[m_writeBuffer] = newWrite;
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
        const size_t oldLast = m_readLastBuffer;

        m_readLastBuffer    = m_readCurrentBuffer;
        m_readCurrentBuffer = m_writeBuffer;
        m_writeBuffer       = oldLast;

        // optional propagation
        copier(buff[m_readCurrentBuffer], buff[m_writeBuffer]);
    }

};