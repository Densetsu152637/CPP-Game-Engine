//
// Created by Nicholas on 18/04/26.
//

#pragma once

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>

template <typename T>
class LinkedQueue
{
    struct Node
    {
        T value;
        std::unique_ptr<Node> next;

        explicit Node(T&& value)
            : value(std::move(value))
        {}
    };

    std::unique_ptr<Node> m_head;
    Node* m_tail = nullptr;
    size_t m_size = 0;

public:
    LinkedQueue() = default;
    explicit LinkedQueue(size_t)
    {}

    LinkedQueue(const LinkedQueue&) = delete;
    LinkedQueue& operator=(const LinkedQueue&) = delete;

    LinkedQueue(LinkedQueue&& other) noexcept
        : m_head(std::move(other.m_head)),
          m_tail(other.m_tail),
          m_size(other.m_size)
    {
        other.m_tail = nullptr;
        other.m_size = 0;
    }

    LinkedQueue& operator=(LinkedQueue&& other) noexcept
    {
        if (this == &other)
            return *this;

        m_head = std::move(other.m_head);
        m_tail = other.m_tail;
        m_size = other.m_size;

        other.m_tail = nullptr;
        other.m_size = 0;
        return *this;
    }

    int length()
    { return static_cast<int>(m_size); }

    size_t length() const
    { return m_size; }

    bool empty() const
    { return 0 == m_size; }

    void append(T item)
    {
        auto node = std::make_unique<Node>(std::move(item));
        Node* newTail = node.get();

        if (nullptr == m_tail)
        {
            m_head = std::move(node);
        }
        else
        {
            m_tail->next = std::move(node);
        }

        m_tail = newTail;
        ++m_size;
    }

    T pop()
    {
        if (empty())
            throw std::out_of_range("Queue is empty");

        std::unique_ptr<Node> node = std::move(m_head);
        m_head = std::move(node->next);
        if (nullptr == m_head)
            m_tail = nullptr;

        --m_size;
        return std::move(node->value);
    }

    const T& peek() const
    {
        if (empty())
            throw std::out_of_range("Queue is empty");

        return m_head->value;
    }
};
