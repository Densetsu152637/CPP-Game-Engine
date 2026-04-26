//
// Created by Nicholas on 18/04/26.
//

#pragma once

#include <stdexcept>
#include <utility>

#include "arraylist.h"

template <typename T, bool IsConst>
struct CircularQueueIterator;

template <typename T>
class Queue {

    Array<T> m_queue;
    size_t m_start = 0;
    size_t m_end = 0;

    void _resize(size_t new_size);
    void _resize_if_necessary();

public:

    Queue() : Queue(ARRAY_DEFAULT_INITIAL_CAPACITY) {}

    explicit Queue(const size_t initial_cap)
    { _resize(initial_cap); }

    Queue(const Queue& queue);
    Queue(Queue&& queue) noexcept;
    Queue& operator=(const Queue& queue);
    Queue& operator=(Queue&& queue) noexcept;

    ~Queue()
    {
        if (nullptr != m_queue.ptr)
            delete[] m_queue.ptr;
    }

    int length() { return m_queue.size; }

    void append(T item);
    T pop();
    T peek();
    bool empty() const { return m_queue.size == 0; }
    size_t length() const { return m_queue.size; }

    //

    using iterator = CircularQueueIterator<T, false>;
    using const_iterator = const CircularQueueIterator<T, true>;

    iterator begin()
    { return iterator { m_start, this }; }

    iterator end()
    { return iterator { m_end, this }; }

    const_iterator begin() const
    { return const_iterator { m_start, this }; }

    const_iterator end() const
    { return const_iterator { m_end, this }; }

    const_iterator cbegin() const
    { return const_iterator { m_start, this }; }

    const_iterator cend() const
    { return const_iterator { m_end, this }; }

};

// iterator
template <typename T, bool IsConst>
struct CircularQueueIterator
{
    using reference = std::conditional_t<IsConst, const T&, T&>;

    size_t pos;
    std::conditional_t<IsConst, const Queue<T>*, Queue<T>*> parent;

    reference operator*() const
    {
        return parent->m_queue[pos];
    }

    CircularQueueIterator& operator++()
    {
        pos = (pos + 1) % parent->m_queue.cap;
        return *this;
    }

    CircularQueueIterator& operator==(const CircularQueueIterator& other)
    {
        return pos == other.pos && parent == other.parent;
    }

    bool operator!=(const CircularQueueIterator& other) const {
        return pos != other.pos || parent != other.parent;
    }
};

template <typename T>
void Queue<T>::_resize(size_t new_size)
{
    if (new_size < m_queue.size)
    {
        new_size = m_queue.size;
    }

    if (new_size == 0)
    {
        new_size = ARRAY_DEFAULT_INITIAL_CAPACITY;
    }

    T* new_arr = new T[new_size];
    if (nullptr != m_queue.ptr)
    {
        for (size_t i = 0; i < m_queue.size; ++i)
        {
            const size_t index = (m_start + i) % m_queue.cap;
            new_arr[i] = std::move(m_queue.ptr[index]);
        }
    }

    delete[] m_queue.ptr;
    m_queue.ptr = new_arr;
    m_queue.cap = new_size;
    m_start = 0;
    m_end = m_queue.size;
}

template <typename T>
void Queue<T>::_resize_if_necessary()
{
    if (!m_queue.full())
    {
        return;
    }

    const size_t next_capacity = m_queue.cap == 0 ? ARRAY_DEFAULT_INITIAL_CAPACITY : m_queue.cap * 2;
    _resize(next_capacity);
}

template <typename T>
Queue<T>::Queue(const Queue& queue)
{
    // copy
    if (nullptr != m_queue.ptr)
        delete[] m_queue.ptr;

    m_start = 0;
    m_end = 0;

    this->_resize(queue.m_queue.cap);
    for (T& elem : queue)
    {
        this->m_queue.ptr[m_end++] = elem;
    }
}

template <typename T>
Queue<T>::Queue(Queue&& queue) noexcept
{
    // move
    if (nullptr != m_queue.ptr)
        delete[] m_queue.ptr;

    m_start = 0;
    m_end = 0;

    this->_resize(queue.m_queue.cap);
    for (T elem : queue) // copy to stack and then move
    {
        this->m_queue.ptr[m_end++] = std::move(elem);
    }
}

template <typename T>
Queue<T>& Queue<T>::operator=(const Queue& queue)
{
    // copy
    if (nullptr != m_queue.ptr)
        delete[] m_queue.ptr;

    m_start = 0;
    m_end = 0;

    this->_resize(queue.m_queue.cap);
    for (T& elem : queue)
    {
        this->m_queue.ptr[m_end++] = elem;
    }

    return *this;
}

template <typename T>
Queue<T>& Queue<T>::operator=(Queue&& queue) noexcept
{
    // move
    if (nullptr != m_queue.ptr)
        delete[] m_queue.ptr;

    m_start = 0;
    m_end = 0;

    this->_resize(queue.m_queue.cap);
    for (T elem : queue)
    {
        this->m_queue.ptr[m_end++] = std::move(elem);
    }

    return *this;
}

template <typename T>
void Queue<T>::append(T item)
{
    _resize_if_necessary();
    m_queue.ptr[m_end] = std::move(item);
    m_end = (m_end + 1) % static_cast<int>(m_queue.cap);
    ++m_queue.size;
}

template <typename T>
T Queue<T>::pop()
{
    if (empty())
    {
        throw std::out_of_range("Queue is empty");
    }

    T value = std::move(m_queue.ptr[m_start]);
    m_queue.ptr[m_start] = T{};
    m_start = (m_start + 1) % static_cast<int>(m_queue.cap);
    --m_queue.size;
    return value;
}

template <typename T>
T Queue<T>::peek()
{
    if (empty())
    {
        throw std::out_of_range("Queue is empty");
    }

    T value = std::move(m_queue.ptr[m_start]);
    m_queue.ptr[m_start] = T{};
    return value;
}
