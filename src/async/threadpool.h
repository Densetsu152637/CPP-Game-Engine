//
// Created by Nicholas on 18/04/26.
//

#pragma once

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

        Queue<Runnable> m_queue;

        std::mutex m_mutex;
        std::condition_variable m_cv;

        bool m_stopping = false;

public:

    Threadpool() : Threadpool(get_num_processors()) {}
    Threadpool(const size_t threads) : Threadpool(threads, "anonymous threadpool") {}
    Threadpool(const size_t threads, std::string&& name);

    Threadpool(Threadpool const&) = delete;
    Threadpool& operator=(Threadpool const&) = delete;
    Threadpool(Threadpool&&) = delete;

    ~Threadpool()
    { shutdown(); }

    void shutdown();

    template <typename T> Promise<T> submit(const Supplier<T>& fn);
    template <typename T> Promise<ArrayList<T>> submit(const ArrayList<Supplier<T>>& fns);

    Promise<void> submit(const Runnable& fn);
    Promise<void> submit(const ArrayList<Runnable>& fns);

    template <typename T, typename U>
    Promise<ArrayList<U>> map(
        const Function<T, U>& fn,
        const ArrayList<T>& data
    );

};
