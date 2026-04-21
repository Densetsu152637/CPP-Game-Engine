//
// Created by Nicholas on 18/04/26.
//

#pragma once

#include <atomic>
#include <thread>

#include "promise.h"
#include "../structs/arraylist.h"
#include "../structs/smartstring.h"
#include "../structs/queue.h"
#include "../functional/functions.h"
#include "lock/notifier.h"

class Threadpool;

size_t get_num_processors();

class Threadpool {

    static void thread_global_entrance_point(Threadpool* thread);

        ArrayList<std::thread> m_pool;
        std::string m_name;

        Queue<std::function<void()>> m_queue;

        std::mutex m_mutex;
        std::condition_variable m_cv;

        bool m_stopping = false;

public:

    Threadpool() : Threadpool(get_num_processors()) {}
    Threadpool(const size_t threads, std::string&& name);

    Threadpool(Threadpool const&) = delete;
    Threadpool& operator=(Threadpool const&) = delete;
    Threadpool(Threadpool&&) = delete;

    ~Threadpool()
    { shutdown(); }

    void shutdown();

    // submit templated functions:

    template <typename F>
    auto submit(F&& fn) -> std::shared_ptr<Promise<std::invoke_result_t<F>>>
        {
            using T = std::invoke_result_t<F>;

            auto promise = std::make_shared<Promise<T>>(this);

            {
                std::function<void()> task(
                    [promise, fn = std::forward<F>(fn)]() mutable
                    {
                        try
                        {
                            T result = fn();
                            promise->complete(Result<T>::success(std::move(result)));
                        }
                        catch (...)
                        {
                            promise->complete(Result<T>::failure(std::current_exception()));
                        }
                    });

                std::lock_guard<std::mutex> lock(m_mutex);
                m_queue.append(std::move(task));
            }

            m_cv.notify_one();

            return promise;
        }

    template <typename T>
    std::shared_ptr<Promise<ArrayList<T>>> submit(const ArrayList<std::function<T()>>& fns)
    {
        auto promise = std::make_shared<Promise<ArrayList<T>>>(this);

        if (fns.length() == 0)
        {
            promise->complete(Result<ArrayList<T>>::success(ArrayList<T>()));
            return promise;
        }

        auto fns_copy = std::make_shared<ArrayList<std::function<T()>>>(fns);

        auto results = std::make_shared<ArrayList<T>>();
        results->resize(fns.length());

        auto remaining = std::make_shared<std::atomic<size_t>>(fns.length());
        auto failed = std::make_shared<std::atomic<bool>>(false);

        for (size_t i = 0; i < fns.length(); ++i)
        {
            {
                std::function<void()> task([=]()
                    {
                        if (failed->load(std::memory_order_acquire)) return;

                        try
                        {
                            (*results)[i] = (*fns_copy)[i]();
                        }
                        catch (...)
                        {
                            if (!failed->exchange(true))
                            {
                                promise->complete(
                                    Result<ArrayList<T>>::failure(std::current_exception())
                                );
                            }
                            return;
                        }

                        if (remaining->fetch_sub(1) == 1 && !failed->load())
                        {
                            promise->complete(
                                Result<ArrayList<T>>::success(std::move(*results))
                            );
                        }
                    });

                std::lock_guard<std::mutex> lock(m_mutex);
                m_queue.append(std::move(task));
            }
        }

        m_cv.notify_all();
        return promise;
    }

    template <typename T, typename U>
    std::shared_ptr<Promise<ArrayList<U>>> map(
        std::function<U(const T&)> fn,
        const ArrayList<T>* data)
    {
        auto promise = std::make_shared<Promise<ArrayList<U>>>(this);
        size_t n = data->length();

        if (n == 0)
        {
            promise->complete(Result<ArrayList<U>>::success({}));
            return promise;
        }

        const size_t num_threads = m_pool.length();
        const size_t num_tasks = std::min(num_threads, n);

        auto results = std::make_shared<ArrayList<U>>(n);

        auto remaining = std::make_shared<std::atomic<size_t>>(n);
        auto failed = std::make_shared<std::atomic<bool>>(false);

        auto remaining_queue = std::make_shared<Queue<int>>(n);
        auto queue_lock = std::make_shared<std::mutex>();

        for (int i = 0; i < static_cast<int>(n); ++i)
            remaining_queue->append(i);

        for (size_t t = 0; t < num_tasks; ++t)
        {
            {
                std::function<void()> task([=]()
                {
                    while (true)
                    {
                        if (failed->load(std::memory_order_acquire)) return;

                        int index;

                        {
                            std::lock_guard<std::mutex> lock(*queue_lock);
                            if (remaining_queue->length() == 0) return;
                            index = remaining_queue->pop();
                        }

                        try
                        {
                            (*results)[index] = fn((*data)[index]);
                        }
                        catch (...)
                        {
                            if (!failed->exchange(true))
                            {
                                promise->complete(
                                    Result<ArrayList<U>>::failure(std::current_exception())
                                );
                            }
                            return;
                        }

                        if (remaining->fetch_sub(1) == 1 && !failed->load())
                        {
                            promise->complete(
                                Result<ArrayList<U>>::success(std::move(*results))
                            );
                            return;
                        }
                    }
                });

                std::lock_guard<std::mutex> lock(m_mutex);
                m_queue.append(std::move(task));
            }
        }

        m_cv.notify_all();
        return promise;
    }

};
