//
// Created by Nicholas on 18/04/26.
//

#include "queue.h"

template <typename T>
void Queue<T>::_resize(size_t new_size)
{
    if (new_size < m_queue.size)
    { new_size = m_queue.size; }

    if (new_size <= 0)
    { new_size = Array<T>::DEFAULT_INITIAL_CAPACITY; }

    size_t old_size = m_queue.size;
    T* new_arr = new T[new_size]; // allocate new array on heap
    if (nullptr != m_queue.ptr)
    {
        // copy elements across
        for (size_t i = 0; i < old_size; i++)
        {
            int index = (i + m_start) % m_queue.cap;
            new_arr[i] = m_queue.ptr[index];
        }
        delete[] m_queue.ptr;
    } // delete old array
    m_queue.ptr = new_arr; // realloc new array
    m_queue.cap = new_size; // redefine new capacity
    m_start = 0;
    m_end = new_size;
}

template <typename T>
void Queue<T>::_resize_if_necessary()
{
    if (this->m_queue.full())
    {
        // rounds up to the nearest 2^n integer to ensure O(1) insertion time
        const int goal = m_queue.cap >> 1;
        int current = round_to_nearest_2n(m_queue.cap);
        while (current < goal)
        {
            current >>= 1;
        }
        this->_resize(current);
    }
}
template <typename T>
void Queue<T>::append(T item)
{
    _resize_if_necessary();
    m_queue.ptr[m_end] = item;
    m_end = (m_end + 1) % m_queue.cap;
}

template <typename T>
T Queue<T>::pop()
{
    T& val = m_queue.ptr[m_start];
    m_start = (m_start + 1) % m_queue.cap;
    return std::move(val);
}
