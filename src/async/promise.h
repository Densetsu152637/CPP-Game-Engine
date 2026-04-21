//
// Created by Nicholas on 18/04/26.
//

#pragma once

#include <functional>
#include <memory>
#include <mutex>

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

    std::mutex mutex;
    std::condition_variable cv;

    Result<T> result;
    ArrayList<Listener> listeners;
    Threadpool* executor = nullptr;
    bool done = false;

    void _attach(std::function<void(Result<T>&)> listener);

public:

    Promise() = default;
    Promise(Threadpool* exec)
    {
        executor = exec;
    }
    ~Promise() = default;

    void operator=(const Promise& p) = delete;
    void operator=(Promise&& p) = delete;

    void complete(Result<T> res);
    Result<T>& await();

    Promise<T> on_error(std::function<T(std::exception_ptr)> handler);

    template <typename U>
    std::shared_ptr<Promise<U>> then(Threadpool* exec, std::function<U(T)> mapper)
    {
        std::shared_ptr<Promise<U>> next = std::make_shared<Promise<U>>(exec);

        this->_attach([=](const Result<T>& res) mutable {

            if (res.is_success())
            {
                try
                {
                    U value = mapper(res.get());
                    next->complete(Result<U>::success(std::move(value)));
                }
                catch (...)
                {
                    next->complete(Result<U>::failure(std::current_exception()));
                }
            }
            else
            {
                next->complete(Result<U>::failure(res.exception()));
            }
        });

        return next;
    }

    template <typename U>
    std::shared_ptr<Promise<U>> then(std::function<U(T)> mapper)
    {
        return this->then(std::move(mapper), executor);
    }

};

template <typename T>
void Promise<T>::_attach(std::function<void(Result<T>&)> listener)
{
    bool execute_now = false;
    Result<T> snapshot;

    {
        std::lock_guard<std::mutex> lock(mutex);

        if (!done)
        {
            listeners.push_back(std::move(listener));
            return;
        }

        execute_now = true;
        snapshot = result;
    }

    if (execute_now)
    {
        if (executor)
        {
            executor->submit([listener, snapshot]() mutable {
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
void Promise<T>::complete(Result<T> res)
{
    ArrayList<Listener> listeners_at_completion;

    {
        std::lock_guard<std::mutex> lock(mutex);

        if (done)
            return;

        done = true;
        result = std::move(res);

        listeners_at_completion = std::move(listeners);
        listeners.clear();
    }

    cv.notify_all();

    if (!listeners_at_completion.empty() && executor)
    {
        ArrayList<std::function<void()>> tasks;
        tasks.reserve(listeners_at_completion.size());

        for (auto& l : listeners)
        {
            tasks.append([this, l]()
            {
                l(this->result);
            });
        }

        executor->submit(tasks);
    }
    else
    {
        for (auto& l : listeners)
            l(result);
    }
}

template <typename T>
Result<T>& Promise<T>::await()
{
    std::unique_lock<std::mutex> lock(mutex);

    cv.wait(lock, [&] {
        return done;
    });

    return result;
}

template <typename T>
Promise<T> Promise<T>::on_error(std::function<T(std::exception_ptr)> handler)
{
    Promise<T> next(executor);

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

