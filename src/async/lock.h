//
// Created by Nicholas on 30/04/26.
//

#pragma once

#include <condition_variable>
#include <type_traits>
#include <mutex>
#include <functional>
#include <utility>

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

    template <typename Func>
    auto map(Func&& func) -> std::invoke_result_t<Func, T&>
    {
        std::unique_lock lock(m_mutex);
        return std::forward<Func>(func)(m_var);
    }

    template <typename Func>
    void use(Func&& func)
    {
        std::unique_lock lock(m_mutex);
        std::forward<Func>(func)(m_var);
    }

    T& ref() {
        return m_var; // references
    }

    std::mutex& lock() {
        return m_mutex; // references
    }
};
