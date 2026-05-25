//
// Created by Nicholas on 18/04/26.
//

#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

#include "../functional/result.h"
#include "../structs/arraylist.h"

class Threadpool;

template <typename T>
struct PromiseSharedState
{
    using Listener = std::function<void(const Result<T>&)>;

    std::mutex mutex;
    std::condition_variable cv;

    Result<T> result;
    ArrayList<Listener> listeners;
    Threadpool* executor = nullptr;
    bool done = false;
};

template <typename T>
class Promise
{
    using Listener = std::function<void(const Result<T>&)>;

    std::shared_ptr<PromiseSharedState<T>> m_state = std::make_shared<PromiseSharedState<T>>();

    void _attach(Listener listener);

public:

    Promise() = default;

    explicit Promise(Threadpool* exec)
    { m_state->executor = exec; }
    ~Promise() = default;

    void complete(Result<T>&& res);
    Result<T>& await();
    void assert();

    Promise<T> on_error(std::function<T(std::exception_ptr)> handler);

    template <typename U>
    Promise<U> then(Threadpool* exec, std::function<U(T)> mapper)
    {
        Promise<U> next = Promise<U>(exec);

        this->_attach([next, mapper](const Result<T>& res) mutable
        {

            if (res.is_success())
            {
                try
                {
                    U value = mapper(res.get());
                    next.complete(Result<U>::success(std::move(value)));
                }
                catch (...)
                {
                    next.complete(Result<U>::failure(std::current_exception()));
                }
            }
            else
            {
                next.complete(Result<U>::failure(res.exception()));
            }
        });

        return next;
    }

    template <typename U>
    Promise<U> then(std::function<U(T)> mapper)
    {
        return this->then(m_state->executor, std::move(mapper));
    }

};

template <typename T>
void Promise<T>::_attach(Listener listener)
{
    bool executeNow = false;

    {
        std::lock_guard<std::mutex> lock(m_state->mutex);

        if (!m_state->done)
        {
            m_state->listeners.append(std::move(listener));
            return;
        }

        executeNow = true;
    }

    if (executeNow)
        listener(m_state->result);
}

template <typename T>
void Promise<T>::complete(Result<T>&& res)
{
    ArrayList<Listener> listeners_at_completion;

    {
        std::lock_guard<std::mutex> lock(m_state->mutex);

        if (m_state->done)
            return;

        m_state->done = true;
        m_state->result = std::move(res);

        listeners_at_completion = std::move(m_state->listeners);
    }

    m_state->cv.notify_all();

    for (auto& listener : listeners_at_completion)
    {
        listener(m_state->result);
    }
}

template <typename T>
Result<T>& Promise<T>::await()
{
    std::unique_lock<std::mutex> lock(m_state->mutex);

    m_state->cv.wait(lock, [&] {
        return m_state->done;
    });

    return m_state->result;
}

template <typename T>
void Promise<T>::assert()
{ await().get(); } // throws error if failed


template <typename T>
Promise<T> Promise<T>::on_error(std::function<T(std::exception_ptr)> handler)
{
    auto next = Promise<T>(m_state->executor);

    this->_attach([next, handler](const Result<T>& res) mutable
    {

        if (res.is_failure())
        {
            try
            {
                T value = handler(res.exception());
                next.complete(Result<T>::success(std::move(value)));
            }
            catch (...)
            {
                next.complete(Result<T>::failure(std::current_exception()));
            }
        }
        else
        {
            next.complete(Result<T>::success(res.get()));
        }
    });

    return next;
}
