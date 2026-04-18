//
// Created by Nicholas on 18/04/26.
//

#ifndef CPP_GAME_ENGINE_THREADPOOL_H
#define CPP_GAME_ENGINE_THREADPOOL_H
#include <thread>

#include "promise.h"
#include "../structs/arraylist.h"
#include "../structs/smartstring.h"
#include "../structs/queue.h"
#include "../async/lock/syncronized.h"
#include "../functional/functions.h"
#include "lock/atomic.h"
#include "lock/notifier.h"

class Threadpool;

struct Thread
{
    std::thread thread;
    Threadpool* pool;
};

size_t get_num_processors();

class Threadpool {

    static void thread_global_entrance_point(const Thread* thread);

    ArrayList<Thread> m_pool;
    SmartString m_name;
    Syncronized<Queue<Runnable>> m_queue;
    Notifier m_notifier;
    Atomic<bool> m_stopping = false;

public:

    Threadpool() : Threadpool(get_num_processors()) {}
    Threadpool(const size_t threads) : Threadpool(threads, "anonymous threadpool") {}
    Threadpool(const size_t threads, std::string&& name);

    Threadpool(Threadpool const&) = delete;
    Threadpool& operator=(Threadpool const&) = delete;
    Threadpool(Threadpool&&) = delete;

    ~Threadpool()
    {
        shutdown();
    }

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





#endif //CPP_GAME_ENGINE_THREADPOOL_H
