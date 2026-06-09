//
// Created by Nicholas on 18/04/26.
//

#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include "../functional/result.h"
#include "../structs/arraylist.h"
#include "../structs/LinkedQueue.h"

template <typename T>
class Promise;

size_t get_num_processors();

class ThreadTask
{
public:
    virtual ~ThreadTask() = default;
    virtual void invoke() = 0;
};

namespace threadpool_detail
{
    template <typename Callable>
    class TaskModel final : public ThreadTask
    {
        Callable callable;

    public:
        explicit TaskModel(Callable&& callable)
            : callable(std::move(callable))
        {}

        void invoke() override
        { callable(); }
    };

    template <typename Callable>
    std::unique_ptr<ThreadTask> make_task(Callable&& callable)
    {
        using Model = TaskModel<std::decay_t<Callable>>;
        return std::make_unique<Model>(std::forward<Callable>(callable));
    }
}

class Threadpool {

    static void thread_global_entrance_point(Threadpool* pool);

    ArrayList<std::thread> m_pool;
    std::string m_name;

    LinkedQueue<std::unique_ptr<ThreadTask>> m_queue;

    std::mutex m_mutex;
    std::condition_variable m_cv;

    std::atomic_bool m_stopping = false;

public:

    Threadpool() : Threadpool(get_num_processors(), "Anonymous Threadpool") {}
    Threadpool(size_t threads, std::string&& name);

    Threadpool(Threadpool const&) = delete;
    Threadpool& operator=(Threadpool const&) = delete;
    Threadpool(Threadpool&&) = delete;

    ~Threadpool()
    { shutdown(); }

    size_t size() const
    { return m_pool.length(); }

    void shutdown();

    // submit templated functions:

    template <typename F>
    auto submit(F&& fn) -> Promise<std::invoke_result_t<F>>
    {
        using T = std::invoke_result_t<F>;

        auto promise = Promise<T>(this);

        {
            auto task = threadpool_detail::make_task(
                [promise, fn = std::forward<F>(fn)]() mutable
                {
                    try
                    {
                        if constexpr (std::is_void_v<T>)
                        {
                            fn();
                            promise.complete(Result<void>::success());
                        }
                        else
                        {
                            T result = fn();
                            promise.complete(Result<T>::success(std::move(result)));
                        }
                    }
                    catch (...)
                    {
                        promise.complete(Result<T>::failure(std::current_exception()));
                    }
                }
            );

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_queue.append(std::move(task));
            }
        }

        m_cv.notify_one();
        return promise;
    }

    template <typename T, typename U>
    Promise<ArrayList<U>> map(
        std::function<U(const T&)> fn,
        const ArrayList<T>* data,
        const size_t min_batch_size = 0)
    {
        auto promise = Promise<ArrayList<U>>(this);
        const size_t n = data->length();

        if (n == 0)
        {
            promise.complete(Result<ArrayList<U>>::success(ArrayList<U>{}));
            return promise;
        }

        const size_t num_threads = std::max<size_t>(1, m_pool.length());
        const size_t default_batch_size = std::max<size_t>(1, n / num_threads);
        const size_t batch_size = std::max<size_t>(
            1,
            min_batch_size == 0 ? default_batch_size : min_batch_size
        );
        const size_t num_batches = (n + batch_size - 1) / batch_size;

        auto results = std::make_shared<ArrayList<U>>(n, true);
        const auto remaining_batches = std::make_shared<std::atomic<size_t>>(num_batches);
        const auto failed = std::make_shared<std::atomic<bool>>(false);

        for (size_t batch = 0; batch < num_batches; ++batch)
        {
            const size_t begin = batch * batch_size;
            const size_t end = std::min(n, begin + batch_size);

            auto task = threadpool_detail::make_task([promise, results, remaining_batches, failed, fn, data, begin, end]() mutable
            {
                if (failed->load(std::memory_order_acquire))
                    return;

                try
                {
                    for (size_t i = begin; i < end; ++i)
                    {
                        (*results)[i] = fn((*data)[i]);
                    }
                }
                catch (...)
                {
                    if (!failed->exchange(true))
                    {
                        promise.complete(
                            Result<ArrayList<U>>::failure(std::current_exception())
                        );
                    }
                    return;
                }

                if (remaining_batches->fetch_sub(1) == 1 && !failed->load(std::memory_order_acquire))
                {
                    promise.complete(
                        Result<ArrayList<U>>::success(std::move(*results))
                    );
                }
            });

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_queue.append(std::move(task));
            }
        }

        m_cv.notify_all();
        return promise;
    }

};

#include "promise.h"
