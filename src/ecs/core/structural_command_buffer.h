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
public:
    struct Command { std::function<bool()> validate; std::function<void()> apply; };
private:
    mutable std::mutex m_mutex;
    std::vector<Command> m_commands;

public:
    template <typename Callable>
    void enqueue(Callable&& callable)
    {
        std::lock_guard lock(m_mutex);
        m_commands.push_back({[] { return true; }, std::forward<Callable>(callable)});
    }

    template <typename Validate, typename Apply>
    void enqueueValidated(Validate&& validate, Apply&& apply)
    {
        std::lock_guard lock(m_mutex);
        m_commands.push_back({std::forward<Validate>(validate), std::forward<Apply>(apply)});
    }

    bool empty() const
    {
        std::lock_guard lock(m_mutex);
        return m_commands.empty();
    }

    std::vector<Command> drain()
    {
        std::lock_guard lock(m_mutex);
        std::vector<Command> commands;
        commands.swap(m_commands);
        return commands;
    }

    void clear()
    {
        std::lock_guard lock(m_mutex);
        m_commands.clear();
    }
};
