//
// Created by Nicholas on 18/04/26.
//

#pragma once

#include <deque>
#include <stdexcept>
#include <utility>

template <typename T>
class Queue
{
    std::deque<T> m_queue;

public:
    using iterator = typename std::deque<T>::iterator;
    using const_iterator = typename std::deque<T>::const_iterator;

    Queue() = default;
    explicit Queue(size_t)
    {}

    Queue(const Queue&) = default;
    Queue(Queue&&) noexcept = default;
    Queue& operator=(const Queue&) = default;
    Queue& operator=(Queue&&) noexcept = default;
    ~Queue() = default;

    int length()
    { return static_cast<int>(m_queue.size()); }

    size_t length() const
    { return m_queue.size(); }

    bool empty() const
    { return m_queue.empty(); }

    void append(T item)
    { m_queue.push_back(std::move(item)); }

    T pop()
    {
        if (empty())
            throw std::out_of_range("Queue is empty");

        T value = std::move(m_queue.front());
        m_queue.pop_front();
        return value;
    }

    T peek() const
    {
        if (empty())
            throw std::out_of_range("Queue is empty");

        return m_queue.front();
    }

    iterator begin()
    { return m_queue.begin(); }

    iterator end()
    { return m_queue.end(); }

    const_iterator begin() const
    { return m_queue.begin(); }

    const_iterator end() const
    { return m_queue.end(); }

    const_iterator cbegin() const
    { return m_queue.cbegin(); }

    const_iterator cend() const
    { return m_queue.cend(); }
};
