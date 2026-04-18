//
// Created by Nicholas on 18/04/26.
//

#ifndef CPP_GAME_ENGINE_PROMISE_H
#define CPP_GAME_ENGINE_PROMISE_H

#include <functional>
#include <memory>
#include <mutex>

#include "../functional/functions.h"
#include "../functional/result.h"
#include "../async/threadpool.h"
#include "../structs/arraylist.h"
#include "lock/waiter.h"

template <typename T>
class Promise
{
public:

    using Listener = std::function<void(const Result<T>&)>;

private:

    struct State
    {
        std::mutex mutex;
        std::condition_variable cv;

        bool done = false;
        Result<T> result;

        ArrayList<Listener> listeners;
        Threadpool* executor = nullptr;
    };

    std::shared_ptr<State> m_state = std::make_shared<State>();

public:

    explicit Promise(Threadpool* exec = nullptr)
    {
        m_state->executor = exec;
    }
    ~Promise() = default;

    void complete(Result<T> result);
    Result<T>& await();

    Promise<T> on_error(std::function<T(std::exception_ptr)> handler);

    template <typename U>
    Promise<U> then(std::function<U(T)> mapper)
    {
        Promise<U> next(m_state->executor);

        attach([next, mapper](const Result<T>& res) mutable {

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

};

template <typename T>
void Promise<T>::_attach(std::function<void(Result<T>&)> listener)
{
    bool execute_now = false;
    Result<T> snapshot;

    {
        std::lock_guard<std::mutex> lock(m_state->mutex);

        if (!m_state->done)
        {
            m_state->listeners.push_back(std::move(listener));
            return;
        }

        execute_now = true;
        snapshot = m_state->result;
    }

    if (execute_now)
    {
        if (m_state->executor)
        {
            m_state->executor->submit([listener, snapshot]() mutable {
                listener(snapshot);
            });
        }
        else
        {
            listener(snapshot);
        }
    }
}

template <typename T>
void Promise<T>::complete(Result<T> result)
{
    ArrayList<Listener> listeners;

    {
        std::lock_guard<std::mutex> lock(m_state->mutex);

        if (m_state->done)
            return;

        m_state->done = true;
        m_state->result = std::move(result);

        listeners = std::move(m_state->listeners);
        m_state->listeners.clear();
    }

    m_state->cv.notify_all();

    if (!listeners.empty() && m_state->executor)
    {
        ArrayList<Runnable> tasks;
        tasks.reserve(listeners.size());

        for (auto& l : listeners)
        {
            tasks.append([l, result]() mutable {
                l(result);
            });
        }

        m_state->executor->submitBatch(tasks);
    }
    else
    {
        for (auto& l : listeners)
            l(m_state->result);
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
Promise<T> Promise<T>::on_error(std::function<T(std::exception_ptr)> handler)
{
    Promise<T> next(m_state->executor);

    attach([next, handler](const Result<T>& res) mutable {

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
            next.complete(res);
        }
    });

    return next;
}



#endif //CPP_GAME_ENGINE_PROMISE_H
