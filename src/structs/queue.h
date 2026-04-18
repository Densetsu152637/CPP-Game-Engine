//
// Created by Nicholas on 18/04/26.
//

#ifndef CPP_GAME_ENGINE_QUEUE_H
#define CPP_GAME_ENGINE_QUEUE_H

#include <stdexcept>
#include <utility>

#include "arraylist.h"

template <typename T>
class Queue {

    Array<T> m_queue;
    int m_start = 0;
    int m_end = 0;

    void _resize(size_t new_size);
    void _resize_if_necessary();

public:

    Queue() : Queue(ARRAY_DEFAULT_INITIAL_CAPACITY) {}
    Queue(size_t initial_cap)
    {
        _resize(initial_cap);
    }
    ~Queue()
    {
        delete[] m_queue.ptr;
    }

    int length() { return m_queue.size; }

    void append(T item);
    T pop();
    T peek();
    bool empty() const { return m_queue.size == 0; }
    size_t length() const { return m_queue.size; }

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
    for (size_t i = 0; i < m_queue.size; ++i)
    {
        const size_t index = (static_cast<size_t>(m_start) + i) % m_queue.cap;
        new_arr[i] = std::move(m_queue.ptr[index]);
    }

    delete[] m_queue.ptr;
    m_queue.ptr = new_arr;
    m_queue.cap = new_size;
    m_start = 0;
    m_end = static_cast<int>(m_queue.size);
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



#endif //CPP_GAME_ENGINE_QUEUE_H
