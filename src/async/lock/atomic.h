//
// Created by Nicholas on 18/04/26.
//

#pragma once

#include <functional>
#include <utility>
#include <atomic>

template <typename T>
class Atomic {

    std::atomic<T> m_var {};

public:

    Atomic() {}
    ~Atomic() = default;

    void operator=(const T& t)
    { this->set(t); }
    void set(T&& t)
    { m_var = std::move(t); }

    void operator=(T&& t)
    { this->set(t); }
    void set(const T& t)
    { m_var = t; }

    T get()
    { return m_var; }

    template <typename U>
    U use(std::function<U (T&)> func)
    { return func(m_var); }

    void consume(std::function<void (T&)> func)
    { func(m_var); }

    void mutate(std::function<T (T&)> func)
    { m_var = func(m_var); }

};

