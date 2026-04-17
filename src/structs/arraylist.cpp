//
// Created by Nicholas on 17/04/26.
//

#include "arraylist.h"

#include <format>
#include <string>
#include <algorithm>
#include <stdexcept>

size_t round_to_nearest_2n(size_t size)
{ return std::bit_ceil(size); }

template <typename T>
void ArrayList<T>::_resize(size_t new_size)
{
    if (new_size < m_size)
    { new_size = m_size; }

    size_t old_size = m_size;
    T* new_arr = new T[new_size]; // allocate new array on heap
    if (nullptr != m_arr)
    {
        // copy elements across
        for (size_t i = 0; i < old_size; i++)
        {
            new_arr[i] = m_arr[i];
        }
        delete[] m_arr;
    } // delete old array
    m_arr = new_arr; // realloc new array
    m_capacity = new_size; // redefine new capacity
}

template <typename T>
void ArrayList<T>::_assert_within_bounds(size_t& i) const
{
    if (i > m_size)
        throw std::out_of_range(std::format("Failed to grab element {} from array size {}", i, m_size));
}

template <typename T>
void ArrayList<T>::_wrap_around_size(size_t& i) const
{
    i = i < 0 ? (m_size - i) % m_size : i % m_size;
}

template <typename T>
void ArrayList<T>::_append_and_shuffle_up(const T& t, size_t i)
{
    // ensure 'i' is within bounds (and we have enough memory, unless allocate more)
    _assert_within_bounds(i);
    _wrap_around_size(i);

    if (m_size >= m_capacity)
    { _resize(round_to_nearest_2n(m_size) >> 1); }

    for (int j = m_size; j > i; --j)
    {
        size_t k = j + 1;
        m_arr[k] = std::move(m_arr[j]);
    }
    // here our m_arr[i] should be the new element
    m_arr[i] = t;
    m_size++; // increment size to show we have added an element
}

template <typename T>
void ArrayList<T>::_pop_and_shuffle_down(size_t i)
{
    _assert_within_bounds(i);
    _wrap_around_size(i);
    for (; i < m_size; ++i)
    {
        size_t k = i + 1;
        m_arr[k] = std::move(m_arr[i]);
    }
    m_arr[i] = NULL; // delete last entry from memory
}

template <typename T>
ArrayList<T>::ArrayList(const ArrayList& arr)
{
    if (nullptr != m_arr) delete[] m_arr;
    m_arr = arr.m_arr;
    m_capacity = arr.m_capacity;
    m_size = arr.m_size;
}

template <typename T>
ArrayList<T>::ArrayList(ArrayList&& arr)
{
    if (nullptr != m_arr) delete[] m_arr;
    m_arr = arr.m_arr;
    m_capacity = arr.m_capacity;
    m_size = arr.m_size;

    arr.m_arr = nullptr;
    arr.m_capacity = 0;
    arr.m_size = 0;
}
