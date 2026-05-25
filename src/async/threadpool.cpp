//
// Created by Nicholas on 18/04/26.
//

#include "threadpool.h"

#include <atomic>
#include <thread>

size_t get_num_processors()
{
    return std::thread::hardware_concurrency();
}

void Threadpool::thread_global_entrance_point(Threadpool* pool)
{
    while (true)
    {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(pool->m_mutex);

            pool->m_cv.wait(lock, [&] {
                return pool->m_stopping || !pool->m_queue.empty();
            });

            if (pool->m_stopping && pool->m_queue.empty())
                return;

            task = pool->m_queue.pop();
        }

        try
        {
            task();
        }
        catch (...)
        {
            // logging hook if needed
        }
    }
}

Threadpool::Threadpool(size_t threads, std::string&& name = "Anonymous Threadpool")
    : m_name(std::move(name))
{
    threads = std::max<size_t>(1, threads);

    for (size_t i = 0; i < threads; ++i)
    {
        m_pool.emplace(thread_global_entrance_point, this); // creates new threads in place
    }
}

void Threadpool::shutdown()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
    }

    m_cv.notify_all();

    for (auto& t : m_pool)
    {
        if (t.joinable())
            t.join();
    }
}








