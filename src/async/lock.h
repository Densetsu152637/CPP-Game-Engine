//
// Created by Nicholas on 30/04/26.
//

#pragma once

#include <condition_variable>
#include <mutex>
#include <functional>

class Notifier
{
    std::mutex m_mutex;
    std::condition_variable m_cv;
    size_t m_count = 0;

public:

    template <typename Predicate>
    void await(Predicate pred)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, pred);
    }

    void await()
    {
        await([&]() {
            return m_count > 0;
        });

        --m_count; // consume one notification
    }

    void notify_one()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_count;
        }
        m_cv.notify_one();
    }

    void notify_many(const size_t num)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_count += num;
        }

        // wake up to num threads
        for (size_t i = 0; i < num; ++i)
        {
            m_cv.notify_one();
        }
    }
};

class Waiter
{
    bool signaled = false;
    std::mutex m_mutex;
    std::condition_variable m_cv;

public:
    void reset_signal()
    {
        std::scoped_lock lock(m_mutex);
        signaled = false;
    }

    void signal()
    {
        {
            std::scoped_lock lock(m_mutex);
            signaled = true;
        }
        m_cv.notify_all();
    }

    void await()
    {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [this] { return signaled; });
    }

};


template <typename T>
class Syncronized {

    T m_var {};
    std::mutex m_mutex;

public:

    Syncronized() {}
    ~Syncronized() = default;

    Syncronized(const T& other)
    { this->set(other); }

    Syncronized(T&& other)
    { this->set(std::move(other)); }

    Syncronized<T>& operator=(const T& other)
    {
        this->set(other);
        return *this;
    }

    Syncronized<T>& operator=(T&& other)
    {
        this->set(std::move(other));
        return *this;
    }

    void set(T&& t)
    {
        std::unique_lock lock(m_mutex);
        m_var = std::move(t);
    }

    void set(const T& t)
    {
        std::unique_lock lock(m_mutex);
        m_var = t;
    }

    T get() {
        std::unique_lock lock(m_mutex);
        return m_var; // copies
    }

    template <typename U>
    U map(std::function<U (T&)> func)
    {
        std::unique_lock lock(m_mutex);
        return func(m_var);
    }

    void use(std::function<void (T&)> func)
    {
        std::unique_lock lock(m_mutex);
        func(m_var);
    }

    T& ref() {
        return m_var; // references
    }

    std::mutex& lock() {
        return m_mutex; // references
    }
};