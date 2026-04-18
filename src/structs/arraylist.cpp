//
// Created by Nicholas on 17/04/26.
//

#include "arraylist.h"

#include <format>
#include <string>
#include <algorithm>
#include <stdexcept>

size_t round_to_nearest_2n(size_t size)
{ return std::__bit_floor(size); }

// ARRAY

template <typename T>
void array_cpy(Array<T>& dst, int dst_start, Array<T>& src, int src_start, int length)
{
    int dst_i = dst_start;
    int src_i = src_start;
    for (int i = 0; i < length; i++)
    {
        dst[dst_i++] = src[src_i++];
    }
}

template void array_cpy<char>(Array<char>& dst, int dst_start, Array<char>& src, int src_start, int length);

// ARRAYLIST

template <typename T>
void ArrayList<T>::_resize(size_t new_size)
{
    if (new_size < m_arr.size)
    { new_size = m_arr.size; }

    if (new_size <= 0)
    { new_size = Array<T>::DEFAULT_INITIAL_CAPACITY; }

    size_t old_size = m_arr.size;
    T* new_arr = new T[new_size]; // allocate new array on heap
    if (nullptr != m_arr.ptr)
    {
        // copy elements across
        for (size_t i = 0; i < old_size; i++)
        {
            new_arr[i] = m_arr.ptr[i];
        }
        delete[] m_arr.ptr;
    } // delete old array
    m_arr.ptr = new_arr; // realloc new array
    m_arr.cap = new_size; // redefine new capacity
}

template <typename T>
void ArrayList<T>::_resize_if_necessary()
{
    if (this->m_arr.full())
    {
        // rounds up to the nearest 2^n integer to ensure O(1) insertion time
        const int goal = m_arr.cap >> 1;
        int current = round_to_nearest_2n(m_arr.cap);
        while (current < goal)
        {
            current >>= 1;
        }
        this->_resize(current);
    }
}

template <typename T>
void ArrayList<T>::_assert_within_bounds(size_t& i) const
{
    if (i > m_arr.size)
        throw std::out_of_range(std::format("Failed to grab element {} from array size {}", i, m_size));
}

template <typename T>
void ArrayList<T>::_wrap_around_size(size_t& i) const
{
    i = i < 0 ? (m_arr.size - i) % m_arr.size : i % m_arr.size;
}

template <typename T>
void ArrayList<T>::_append_and_shuffle_up(const T& t, size_t i)
{
    // ensure 'i' is within bounds (and we have enough memory, unless allocate more)
    _assert_within_bounds(i);
    _resize_if_necessary();
    _wrap_around_size(i);

    for (int j = m_arr.size; j > i; --j)
    {
        size_t k = j + 1;
        m_arr[k] = std::move(m_arr[j]);
    }
    // here our m_arr[i] should be the new element
    m_arr[i] = t;
    ++m_arr.size; // increment size to show we have added an element
}

template <typename T>
void ArrayList<T>::_pop_and_shuffle_down(size_t i)
{
    _assert_within_bounds(i);
    _wrap_around_size(i);

    for (; i < m_arr.size; ++i)
    {
        size_t k = i + 1;
        m_arr[k] = std::move(m_arr[i]);
    }
    m_arr[i] = NULL; // delete last entry from memory
}

template <typename T>
ArrayList<T>::ArrayList(const ArrayList& arr)
{
    // shallow copy
    if (nullptr != m_arr.ptr) delete[] m_arr.ptr;
    m_arr = new T[arr.m_arr.cap];
    m_arr.size = arr.m_arr.size;
    m_arr.cap = arr.m_arr.cap;

    // deep copy elements from other array into this one
    for (int i = 0; i < arr.m_arr.size; i++)
    {
        m_arr[i] = arr.m_arr[i];
    }
}

template <typename T>
ArrayList<T>::ArrayList(ArrayList&& arr) noexcept
{
    // takes ownership of arr's memory
    if (nullptr != m_arr.ptr) delete[] m_arr.ptr;
    m_arr = arr.m_arr;
    m_arr.cap = arr.m_arr.cap;
    m_arr.size = arr.m_arr.size;

    arr.m_arr = nullptr;
    arr.m_arr.cap = 0;
    arr.m_arr.size = 0;
}

template <typename T>
T& ArrayList<T>::at(size_t index)
{
    _wrap_around_size(index);
    return m_arr[index];
}

template <typename T>
ArrayList<T>& ArrayList<T>::append(const T* ts, size_t elems)
{
    // guarantee one memory allocation
    reserve();
    for (int i = 0; i < elems; i++)
    {
        append(ts[i]);
    }
    return *this;
}
template <typename T>
ArrayList<T> ArrayList<T>::concat(const ArrayList<T>& arr)
{
    ArrayList<T> ret(m_arr.size + arr.m_arr.size);
    ret.append(this);
    ret.append(arr);
    return ret;
}


