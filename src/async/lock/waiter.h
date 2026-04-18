//
// Created by Nicholas on 18/04/26.
//

#ifndef CPP_GAME_ENGINE_WAITER_H
#define CPP_GAME_ENGINE_WAITER_H

#include <condition_variable>
#include <mutex>

class Waiter
{
    bool signaled = false;
    std::mutex m_mutex;
    std::condition_variable m_cv;

public:
    void reset_signal()
    {
        std::scoped_lock lock(m_mutex);
        signaled = false;
    }

    void signal()
    {
        {
            std::scoped_lock lock(m_mutex);
            signaled = true;
        }
        m_cv.notify_all();
    }

    void await()
    {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [this] { return signaled; });
    }

};

#endif //CPP_GAME_ENGINE_WAITER_H
