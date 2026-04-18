//
// Created by Nicholas on 18/04/26.
//

#ifndef CPP_GAME_ENGINE_FUNCTIONS_H
#define CPP_GAME_ENGINE_FUNCTIONS_H
#include <memory>
#include <functional>

template <typename T>
class Runnable
{

    std::shared_ptr<std::function<void()>> m_fn;

public:

    Runnable() {}
    Runnable(std::function<void(void)> fn) : Runnable(std::make_shared<std::function<void()>>(fn)) {}
    Runnable(std::shared_ptr<std::function<void()>> fn) : m_fn(fn) {}
    ~Runnable() = default;

    void run()
    {
        this->m_fn.get()->operator()();
    }

    void operator()()
    {
        this->run();
    }

};

template <typename T>
class Consumer
{

    std::shared_ptr<std::function<void(T&)>> m_fn;

public:

    Consumer() {}
    Consumer(std::function<void(T&)> fn) : Runnable(std::make_shared<std::function<void(T&)>>(fn)) {}
    Consumer(std::shared_ptr<std::function<void(T&)>> fn) : m_fn(fn) {}
    ~Consumer() = default;

    void consume(T& t)
    {
        return this->m_fn.get()->operator()(t);
    }

    void operator()(T& t)
    {
        return this->consume(t);
    }

};

template <typename T>
class Supplier
{

    std::shared_ptr<std::function<T()>> m_fn;

public:

    Supplier() {}
    Supplier(std::function<T()> fn) : Runnable(std::make_shared<std::function<T()>>(fn)) {}
    Supplier(std::shared_ptr<std::function<T()>> fn) : m_fn(fn) {}
    ~Supplier() = default;

    T get()
    {
        return m_fn.get()->operator()();
    }

    T operator()()
    {
        return this->get();
    }

};

template <typename T, typename U>
class Function
{

    std::shared_ptr<std::function<T(U&)>> m_fn;

public:

    Supplier() {}
    Supplier(std::function<T(U&)> fn) : Runnable(std::make_shared<std::function<T(U&)>>(fn)) {}
    Supplier(std::shared_ptr<std::function<T(U&)>> fn) : m_fn(fn) {}
    ~Supplier() = default;

    T apply(U& u)
    {
        return m_fn.get()->operator()(u);
    }

    T operator()(U& u)
    {
        return this->apply(u);
    }

};

#endif //CPP_GAME_ENGINE_FUNCTIONS_H
