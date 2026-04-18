//
// Created by Nicholas on 18/04/26.
//

#ifndef CPP_GAME_ENGINE_THREAD_TIMER_H
#define CPP_GAME_ENGINE_THREAD_TIMER_H

// TODO
// add global pool of threads to ensure threads are safely executed and managed

#include "../functional/result.h"
#include "../functional/functions.h"

#include <thread>
#include <future>
#include <exception>

template <typename T>
class ThreadTimer
{
public:

    template <typename Duration>
    static Result<T> run(Supplier<T> fn, Duration timeout);
};

template <typename T>
template <typename Duration>
Result<T> ThreadTimer<T>::run(Supplier<T> fn, Duration timeout)
{
    std::promise<T> promise;
    std::future<T> future = promise.get_future();

    std::thread worker([promise = std::move(promise), fn = std::move(fn)]() mutable {

        try
        {
            T result = fn();
            promise.set_value(std::move(result));
        }
        catch (...)
        {
            promise.set_exception(std::current_exception());
        }

    });

    if (future.wait_for(timeout) == std::future_status::ready)
    {
        try
        {
            T value = future.get();
            worker.join();
            return Result<T>::success(std::move(value));
        }
        catch (...)
        {
            worker.join();
            return Result<T>::failure(std::current_exception());
        }
    }
    else
    {
        // timeout occurred
        // IMPORTANT: thread is NOT killed
        worker.detach();

        return Result<T>::failure(
            std::make_exception_ptr(std::runtime_error("ThreadTimer timeout"))
        );
    }
}

#endif //CPP_GAME_ENGINE_THREAD_TIMER_H
