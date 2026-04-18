//
// Created by Nicholas on 18/04/26.
//

#ifndef CPP_GAME_ENGINE_NOTIFIER_H
#define CPP_GAME_ENGINE_NOTIFIER_H
#include <condition_variable>

class Notifier
{
    std::mutex m_mutex;
    std::condition_variable m_cv;
    size_t m_count = 0;

public:

    template <typename Predicate>
    void await(Predicate pred)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, pred);
    }

    void await()
    {
        await([&]() {
            return m_count > 0;
        });

        --m_count; // consume one notification
    }

    void notify_one()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_count;
        }
        m_cv.notify_one();
    }

    void notify_many(const size_t num)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_count += num;
        }

        // wake up to num threads
        for (size_t i = 0; i < num; ++i)
        {
            m_cv.notify_one();
        }
    }
};

#endif //CPP_GAME_ENGINE_NOTIFIER_H
