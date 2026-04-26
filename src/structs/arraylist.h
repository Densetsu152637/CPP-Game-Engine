//
// Created by Nicholas on 17/04/26.
//

#pragma once

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <utility>

static constexpr size_t ARRAY_DEFAULT_INITIAL_CAPACITY = 16;

template <typename T>
struct Array
{
    T* ptr = nullptr;
    size_t size = 0;
    size_t cap = 0;

    T& operator[](const size_t index) { return ptr[index]; };
    const T& operator[](const size_t index) const { return ptr[index]; };

    using iterator = T*;
    using const_iterator = const T*;

    iterator begin()
    { return ptr; }

    iterator end()
    { return ptr + size; }

    const_iterator begin() const
    { return ptr; }

    const_iterator end() const
    { return ptr + size; }

    const_iterator cbegin() const
    { return ptr; }

    const_iterator cend() const
    { return ptr + size; }

    bool full() const
    {
        return size >= cap;
    }
};

template <typename T>
void array_cpy(Array<T>& dst, int dst_start, const Array<T>& src, int src_start, int length)
{
    for (int i = 0; i < length; ++i)
    {
        dst[dst_start + i] = src[src_start + i];
    }
}

template <typename T>
class ArrayList {

    Array<T> m_arr;
    void _resize(size_t new_size);
    void _resize_if_necessary();

    void _assert_within_bounds(size_t& i) const;
    void _wrap_around_size(size_t& i) const;
    void _pop_and_shuffle_down(size_t i);
    void _shuffle_up(size_t i);

public:

    using iterator = T*;
    using const_iterator = const T*;

    ArrayList() : ArrayList(ARRAY_DEFAULT_INITIAL_CAPACITY) {}

    explicit ArrayList(const size_t initial_capacity)
    { _resize(initial_capacity); }

    ArrayList(const ArrayList& arr);
    ArrayList(ArrayList&& arr) noexcept;
    ArrayList& operator=(const ArrayList& arr);
    ArrayList& operator=(ArrayList&& arr) noexcept;

    ~ArrayList()
    {
        delete[] m_arr.ptr;
    }

    Array<T>& ptr() { return m_arr; }
    const Array<T>& ptr() const { return m_arr; }

    size_t length() const
    { return m_arr.size; }

    size_t capacity() const
    { return m_arr.cap; }

    bool empty() const { return length() == 0; }

    void reserve(size_t space)
    {
        if (space > m_arr.cap - m_arr.size)
        {
            this->_resize(m_arr.cap + space);
        }
    }

    void restrict(const size_t space)
    { _resize(space); }

    void clamp_size()
    { this->restrict(m_arr.size); }

    //

    T& operator[](const size_t index) { return this->at(index); };
    const T& operator[](const size_t index) const { return this->at(index); };
    T& at(size_t index);
    const T& at(size_t index) const;

    ArrayList<T>& append(const T& t)
    {
        this->append(t, m_arr.size);
        return *this;
    }

    ArrayList<T>& append(const T& t, size_t i)
    {
        // clamp i to end if need be
        if (i > m_arr.size)
        {
            i = m_arr.size;
        }

        this->_shuffle_up(i);
        m_arr[i] = t;
        ++m_arr.size;
        return *this;
    }

    ArrayList<T>& append(const ArrayList<T>& arr)
    {
        this->append(arr.m_arr.ptr, arr.m_arr.size);
        return *this;
    }

    ArrayList<T>& append(const T* ts, size_t elems);

    ArrayList<T> concat(const ArrayList<T>& arr);

    template<typename... Args>
    T& emplace(size_t i, Args&&... args)
    {
        // clamp i to end if need be
        if (i > m_arr.size)
        {
            i = m_arr.size;
        }

        this->_shuffle_up(i);

        // Construct in-place
        new (m_arr.ptr + i) T(std::forward<Args>(args)...);

        ++m_arr.size;
        return m_arr[i];
    }

    template<typename... Args>
    T& emplace(Args&&... args)
    {
        size_t i = m_arr.size;
        _resize_if_necessary();

        // Construct in-place
        new (m_arr.ptr + i) T(std::forward<Args>(args)...);

        ++m_arr.size;
        return m_arr[i];
    }

    //

    iterator begin()
    { return m_arr.begin(); }

    iterator end()
    { return m_arr.end(); }

    const_iterator begin() const
    { return m_arr.begin(); }

    const_iterator end() const
    { return m_arr.end(); }

    const_iterator cbegin() const
    { return m_arr.cbegin(); }

    const_iterator cend() const
    { return m_arr.cend(); }


    template <typename Func>
    void for_each(Func&& func)
    {
        for (size_t i = 0; i < m_arr.size; i++)
        {
            func(m_arr[i]);
        }
    }

    template <typename Func>
    auto map(Func&& func)
    {
        using U = std::decay_t<decltype(func(std::declval<T&>()))>;

        ArrayList<U> dest(m_arr.size);

        for (size_t i = 0; i < m_arr.size; i++)
        {
            dest.append(func(m_arr[i]));
        }

        return dest;
    }

    template <typename R>
    R reduce(std::function<R(const R&, const T&)> reducer, R initial) const
    {
        R acc = initial;

        for (size_t i = 0; i < this->length(); ++i)
        {
            acc = reducer(acc, this->at(i));
        }

        return acc;
    }

};

template <typename T>
void ArrayList<T>::_resize(size_t new_size)
{
    if (new_size < m_arr.size)
    {
        new_size = m_arr.size;
    }

    if (new_size == 0)
    {
        new_size = ARRAY_DEFAULT_INITIAL_CAPACITY;
    }

    T* new_arr = new T[new_size];
    for (size_t i = 0; i < m_arr.size; ++i)
    {
        new_arr[i] = std::move(m_arr.ptr[i]);
    }

    delete[] m_arr.ptr;
    m_arr.ptr = new_arr;
    m_arr.cap = new_size;
}

template <typename T>
void ArrayList<T>::_resize_if_necessary()
{
    if (!m_arr.full())
    {
        return;
    }

    const size_t next_capacity = m_arr.cap == 0 ?
        ARRAY_DEFAULT_INITIAL_CAPACITY :
        m_arr.cap >> 1;
    _resize(next_capacity);
}

template <typename T>
void ArrayList<T>::_assert_within_bounds(size_t& i) const
{
    if (i > m_arr.size)
    {
        throw std::out_of_range("ArrayList index out of range");
    }
}

template <typename T>
void ArrayList<T>::_wrap_around_size(size_t& i) const
{
    if (i < 0)
    {
        i = (m_arr.size - i);
    }
    i %= m_arr.size;
}

template <typename T>
void ArrayList<T>::_shuffle_up(size_t i)
{
    if (i > m_arr.size)
    {
        throw std::out_of_range("ArrayList insert index out of range");
    }

    _resize_if_necessary();

    for (size_t j = m_arr.size; j > i; --j)
    {
        m_arr[j] = std::move(m_arr[j - 1]);
    }
}

template <typename T>
void ArrayList<T>::_pop_and_shuffle_down(size_t i)
{
    if (i >= m_arr.size)
    {
        throw std::out_of_range("ArrayList erase index out of range");
    }

    for (size_t j = i; j + 1 < m_arr.size; ++j)
    {
        m_arr[j] = std::move(m_arr[j + 1]);
    }

    --m_arr.size;
    m_arr[m_arr.size] = T{};
}

template <typename T>
ArrayList<T>::ArrayList(const ArrayList& arr)
{
    _resize(arr.m_arr.cap);
    m_arr.size = arr.m_arr.size;

    for (size_t i = 0; i < arr.m_arr.size; ++i)
    {
        m_arr[i] = arr.m_arr[i];
    }
}

template <typename T>
ArrayList<T>::ArrayList(ArrayList&& arr) noexcept
{
    m_arr = arr.m_arr;
    arr.m_arr.ptr = nullptr;
    arr.m_arr.size = 0;
    arr.m_arr.cap = 0;
}

template <typename T>
ArrayList<T>& ArrayList<T>::operator=(const ArrayList& arr)
{
    if (this == &arr)
    {
        return *this;
    }

    reserve(arr.m_arr.cap);
    m_arr.size = arr.m_arr.size;

    for (size_t i = 0; i < arr.m_arr.size; ++i)
    {
        m_arr[i] = arr.m_arr[i];
    }

    return *this;
}

template <typename T>
ArrayList<T>& ArrayList<T>::operator=(ArrayList&& arr) noexcept
{
    if (this == &arr)
    {
        return *this;
    }

    delete[] m_arr.ptr;
    m_arr = arr.m_arr;
    arr.m_arr.ptr = nullptr;
    arr.m_arr.size = 0;
    arr.m_arr.cap = 0;
    return *this;
}

template <typename T>
T& ArrayList<T>::at(size_t index)
{
    _assert_within_bounds(index);
    _wrap_around_size(index);
    return m_arr[index];
}

template <typename T>
const T& ArrayList<T>::at(size_t index) const
{
    _assert_within_bounds(index);
    _wrap_around_size(index);
    return m_arr[index];
}

template <typename T>
ArrayList<T>& ArrayList<T>::append(const T* ts, size_t elems)
{
    this->reserve(m_arr.size + elems);
    for (size_t i = 0; i < elems; i++)
    {
        this->append(ts[i]);
    }
    return *this;
}

template <typename T>
ArrayList<T> ArrayList<T>::concat(const ArrayList<T>& arr)
{
    ArrayList<T> ret(m_arr.size + arr.m_arr.size);
    ret.append(*this);
    ret.append(arr);
    return ret;
}


