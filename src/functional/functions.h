//
// Created by Nicholas on 18/04/26.
//

#ifndef CPP_GAME_ENGINE_FUNCTIONS_H
#define CPP_GAME_ENGINE_FUNCTIONS_H

#include <functional>
#include <memory>
#include <stdexcept>

class Runnable
{
    std::function<void()> m_fn;

public:
    Runnable() = default;
    explicit Runnable(std::function<void()> fn) : m_fn(std::move(fn)) {}

    void operator()()
    {
        if (!m_fn) throw std::bad_function_call();
        m_fn();
    }

};

template <typename T>
class Consumer
{
    std::function<void(T&)> m_fn;

public:
    Consumer() = default;
    explicit Consumer(std::function<void(T&)> fn) : m_fn(std::move(fn)) {}

    void operator()(T& t)
    {
        if (!m_fn) throw std::bad_function_call();
        m_fn(t);
    }
};

template <typename T>
class Supplier
{
    std::function<T()> m_fn;

public:
    Supplier() = default;
    explicit Supplier(std::function<T()> fn) : m_fn(std::move(fn)) {}

    T operator()()
    {
        if (!m_fn) throw std::bad_function_call();
        return m_fn();
    }


};

template <typename T, typename U>
class Function
{
    std::function<U(T&)> m_fn;

public:
    Function() = default;
    explicit Function(std::function<U(T&)> fn) : m_fn(std::move(fn)) {}

    U operator()(T& t)
    {
        if (!m_fn) throw std::bad_function_call();
        return m_fn(t);
    }
};

inline Supplier<void> to_supplier(Runnable fn)
{
    return Supplier<void>([fn = std::move(fn)]() mutable {
        fn();
        return;
    });
}



#endif //CPP_GAME_ENGINE_FUNCTIONS_H
