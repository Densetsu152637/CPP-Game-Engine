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
class Promise {

    struct State
    {
        const Threadpool* pool = nullptr;
        ArrayList<std::function<void(Result<T>&)>> listeners;
        Result<T> result;
        Waiter wait;
        std::mutex mutex;
        bool done = false;
    };

    std::shared_ptr<State> m_state = std::make_shared<State>();

    void _attach(std::function<void(Result<T>&)> listener);

public:

    Promise(const Threadpool* p)
    {
        m_state->pool = p;
    };
    ~Promise() = default;

    void complete(Result<T> result);
    Result<T>& await();

    template <typename U> Promise<U> then(Function<T, U>&& f);
    Promise<T> catch_error(Function<std::exception, T>&& f);

};

template <typename T>
void Promise<T>::_attach(std::function<void(Result<T>&)> listener)
{
    bool ready = false;
    {
        std::scoped_lock lock(m_state->mutex);
        if (m_state->done)
        {
            ready = true;
        }
        else
        {
            m_state->listeners.append(listener);
        }
    }

    if (ready)
    {
        listener(m_state->result);
    }
}

template <typename T>
void Promise<T>::complete(Result<T> result)
{
    ArrayList<std::function<void(Result<T>&)>> listeners;
    {
        std::scoped_lock lock(m_state->mutex);
        if (m_state->done)
        {
            return;
        }

        m_state->result = std::move(result);
        m_state->done = true;
        listeners = std::move(m_state->listeners);
        m_state->listeners = ArrayList<std::function<void(Result<T>&)>>();
    }

    for (auto& listener : listeners)
    {
        listener(m_state->result);
    }

    m_state->wait.signal();
}

template <typename T>
Result<T>& Promise<T>::await()
{
    m_state->wait.await();
    return m_state->result;
}

template <typename T>
template <typename U>
Promise<U> Promise<T>::then(Function<T, U>&& f)
{
    Promise<U> next;
    _attach([next, func = std::move(f)](Result<T>& result) mutable {
        if (result.is_failure())
        {
            next.complete(Result<U>::failure(result.exception()));
            return;
        }

        try
        {
            next.complete(Result<U>::success(func.apply(result.get())));
        }
        catch (...)
        {
            next.complete(Result<U>::failure(std::current_exception()));
        }
    });
    return next;
}

template <typename T>
Promise<T> Promise<T>::catch_error(Function<std::exception, T>&& f)
{
    Promise<T> next;
    _attach([next, func = std::move(f)](Result<T>& result) mutable {
        if (result.is_success())
        {
            next.complete(Result<T>::success(result.get()));
            return;
        }

        try
        {
            std::rethrow_exception(result.exception());
        }
        catch (std::exception& e)
        {
            try
            {
                next.complete(Result<T>::success(func.apply(e)));
            }
            catch (...)
            {
                next.complete(Result<T>::failure(std::current_exception()));
            }
        }
        catch (...)
        {
            next.complete(Result<T>::failure(std::current_exception()));
        }
    });
    return next;
}



#endif //CPP_GAME_ENGINE_PROMISE_H
