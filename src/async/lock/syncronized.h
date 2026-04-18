//
// Created by Nicholas on 18/04/26.
//

#ifndef CPP_GAME_ENGINE_SYNCRONIZED_H
#define CPP_GAME_ENGINE_SYNCRONIZED_H
#include <mutex>
#include <functional>

template <typename T>
class Syncronized {

    T m_var{};
    std::mutex m_mutex;

public:

    Syncronized() {}
    ~Syncronized() = default;

    Syncronized<T>& operator=(const T&& other)
    {
        this->set(other);
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
        return m_var;
    }

    template <typename U>
    U use(std::function<U (T&)> func)
    {
        std::unique_lock lock(m_mutex);
        return func(m_var);
    }

    void consume(std::function<void (T&)> func)
    {
        std::unique_lock lock(m_mutex);
        func(m_var);
    }

    void mutate(std::function<T (T&)> func)
    {
        std::unique_lock lock(m_mutex);
        m_var = func(m_var);
    }

};



#endif //CPP_GAME_ENGINE_SYNCRONIZED_H
