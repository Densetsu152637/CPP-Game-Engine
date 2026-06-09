//
// Thread-safe queue for deferred ECS structural mutations.
//

#pragma once

#include <functional>
#include <mutex>
#include <utility>
#include <vector>

class StructuralCommandBuffer
{
    mutable std::mutex m_mutex;
    std::vector<std::function<void()>> m_commands;

public:
    template <typename Callable>
    void enqueue(Callable&& callable)
    {
        std::lock_guard lock(m_mutex);
        m_commands.emplace_back(std::forward<Callable>(callable));
    }

    bool empty() const
    {
        std::lock_guard lock(m_mutex);
        return m_commands.empty();
    }

    std::vector<std::function<void()>> drain()
    {
        std::lock_guard lock(m_mutex);
        std::vector<std::function<void()>> commands;
        commands.swap(m_commands);
        return commands;
    }

    void clear()
    {
        std::lock_guard lock(m_mutex);
        m_commands.clear();
    }
};
