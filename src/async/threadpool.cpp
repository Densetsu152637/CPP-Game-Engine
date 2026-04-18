//
// Created by Nicholas on 18/04/26.
//

#include "threadpool.h"

#include <thread>

size_t get_num_processors()
{
    return std::thread::hardware_concurrency();
}

void Threadpool::thread_global_entrance_point(const Thread* thread)
{
    Threadpool* pool = thread->pool;

    while (true)
    {
        Runnable task;
        Runnable* r_ptr = nullptr;

        if (pool->m_stopping)
        {
            break;
        }

        int q_length = pool->m_queue.use([](Queue<Runnable>& q) { return q.length(); });

        if (0 == q_length)
        {
            pool->m_notifier.await();
        }

        // pop task off the queue
        pool->m_queue.consume([&](Queue<Runnable>& q) {
            if (0 == q.length()) return;
            task = std::move(q.pop());
            r_ptr = &task;
        });

        if (nullptr != r_ptr)
        {
            task();
        }
    }
}

Threadpool::Threadpool(const size_t threads, std::string&& name)
{
    m_name = name;
    m_pool.reserve(threads);
    for (int i = 0; i < threads; i++)
    {
        m_pool[i] = Thread {
            std::thread( thread_global_entrance_point, &m_pool[i] ),
            this
        };
        m_pool.ptr().size++;
    }
}

void Threadpool::shutdown()
{
    m_stopping = true;
    m_notifier.notify_many(m_pool.length()); // wake all waiting threads

    for (Thread& worker : m_pool)
    {
        if (std::thread& thread = worker.thread; thread.joinable())
        {
            thread.join();
        }
    }
}

// single

template <typename T>
Promise<T> Threadpool::submit(const Supplier<T>& fn)
{
    Promise<T> promise;
    promise.m_state->pool = this;

    Runnable task([promise, fn]() mutable {
        try
        {
            T result = fn();
            promise.complete(Result<T>::success(std::move(result)));
        }
        catch (...)
        {
            promise.complete(Result<T>::failure(std::current_exception()));
        }
    });

    m_queue.consume([&](Queue<Runnable>& q) {
        q.append(std::move(task));
    });

    m_notifier.notify_one();

    return promise;
}

Promise<void> Threadpool::submit(const Runnable& fn)
{
    return submit<void>(to_supplier(fn));
}

// batch

template <typename T>
Promise<ArrayList<T>> Threadpool::submit(const ArrayList<Supplier<T>>& fns)
{
    Promise<ArrayList<T>> promise;
    promise.m_state->pool = this;

    if (fns.length() == 0)
    {
        promise.complete(Result<ArrayList<T>>::success({}));
        return promise;
    }

    auto results = std::make_shared<ArrayList<T>>();
    results->resize(fns.length());

    auto remaining = std::make_shared<std::atomic<size_t>>(fns.length());
    auto failed = std::make_shared<std::atomic<bool>>(false);

    for (size_t i = 0; i < fns.length(); ++i)
    {
        Supplier<T> fn = fns[i];

        Runnable task([promise, fn, results, remaining, failed, i]() mutable {

            if (failed->load()) return;

            try
            {
                (*results)[i] = fn();
            }
            catch (...)
            {
                failed->store(true);
                promise.complete(Result<ArrayList<T>>::failure(std::current_exception()));
                return;
            }

            if (--(*remaining) == 0 && !failed->load())
            {
                promise.complete(Result<ArrayList<T>>::success(std::move(*results)));
            }
        });

        m_queue.consume([&](Queue<Runnable>& q) {
            q.append(std::move(task));
        });
    }

    m_notifier.notify_many(fns.length());

    return promise;
}

Promise<void> Threadpool::submit(const ArrayList<Runnable>& fns)
{
    ArrayList<Supplier<void>> suppliers;
    suppliers.reserve(fns.length());

    for (const auto& fn : fns)
    {
        suppliers.append(to_supplier(fn));
    }

    return submit<void>(suppliers);
}

template <typename T, typename U>
Promise<ArrayList<U>> Threadpool::map(
    const Function<T, U>& fn,
    const ArrayList<T>& data)
{
    Promise<ArrayList<U>> promise;
    promise.m_state->pool = this;

    size_t n = data.length();

    if (n == 0)
    {
        promise.complete(Result<ArrayList<U>>::success({}));
        return promise;
    }

    size_t numThreads = m_pool.length();
    size_t chunkSize = (n + numThreads - 1) / numThreads;

    auto results = std::make_shared<ArrayList<U>>();
    results->resize(n);

    auto remaining = std::make_shared<std::atomic<size_t>>(n);
    auto failed = std::make_shared<std::atomic<bool>>(false);

    for (size_t t = 0; t < numThreads; ++t)
    {
        size_t start = t * chunkSize;
        size_t end = std::min(start + chunkSize, n);

        if (start >= end)
            break;

        Runnable task([=, &fn, &data, &results, &remaining, &failed]() {

            if (failed->load())
                return;

            for (size_t i = start; i < end; ++i)
            {
                if (failed->load())
                    return;

                try
                {
                    (*results)[i] = fn(const_cast<T&>(data[i]));

                    if (--(*remaining) == 0)
                    {
                        promise.complete(
                            Result<ArrayList<U>>::success(std::move(*results))
                        );
                        return;
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
            }
        });

        m_queue.consume([&](Queue<Runnable>& q) {
            q.append(std::move(task));
        });
    }

    m_notifier.notify_many(numThreads);

    return promise;
}






