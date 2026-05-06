//
// Created by Nicholas on 17/04/26.
//

#pragma once

#include <algorithm>
#include <functional>
#include <new>
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

    void resize(size_t new_size)
    {
        if (new_size < size)
        {
            new_size = size;
        }

        if (new_size == 0)
        {
            new_size = ARRAY_DEFAULT_INITIAL_CAPACITY;
        }

        T* new_arr = new T[new_size];
        for (size_t i = 0; i < size; ++i)
        {
            new_arr[i] = std::move(ptr[i]);
        }

        delete[] ptr;
        ptr = new_arr;
        cap = new_size;
    }

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
    { return size >= cap; }
};

template <typename T>
void array_cpy(Array<T>& dst, const int dst_start, const Array<T>& src, const int src_start, const int length)
{
    for (int i = 0; i < length; ++i)
    {
        dst[dst_start + i] = src[src_start + i];
    }
}

template <typename T>
class ArrayList {

    Array<T> m_arr;
    void _resize(size_t new_size)
    { m_arr.resize(new_size); }
    void _resize_if_necessary();

    void _assert_within_bounds(size_t& i) const;
    void _wrap_around_size(size_t& i) const;
    T _pop_and_shuffle_down(size_t i);
    void _shuffle_up(size_t i);

public:

    using iterator = T*;
    using const_iterator = const T*;

    ArrayList() : ArrayList(ARRAY_DEFAULT_INITIAL_CAPACITY) {}

    explicit ArrayList(const size_t initial_capacity) : ArrayList(initial_capacity, false) {}

    ArrayList(const size_t initial_capacity, const bool check_size)
    {
        _resize(initial_capacity);
        this->m_arr.size = check_size ? initial_capacity : 0;
    }

    ArrayList(const ArrayList& arr);
    ArrayList(ArrayList&& arr) noexcept;
    ArrayList& operator=(const ArrayList& arr);
    ArrayList& operator=(ArrayList&& arr) noexcept;

    ~ArrayList()
    {
        delete[] m_arr.ptr;
        m_arr.ptr = nullptr;
        m_arr.size = 0;
        m_arr.cap = 0;
    }

    Array<T>& ptr() { return m_arr; }
    const Array<T>& ptr() const { return m_arr; }

    size_t length() const
    { return m_arr.size; }

    size_t capacity() const
    { return m_arr.cap; }

    bool empty() const { return length() == 0; }

    void guarantee(const size_t space)
    {
        if (space > m_arr.cap)
        {
            this->_resize(space);
        }
    }

    void reserve(const size_t space)
    {
        if (space > m_arr.cap - m_arr.size)
        { this->_resize(m_arr.cap + space); }
    }

    void restrict(const size_t space)
    { this->_resize(space); }

    void clamp_size()
    { this->restrict(m_arr.size); }

    void clear();

    //

    T& operator[](const size_t index) { return this->at(index); };
    const T& operator[](const size_t index) const { return this->at(index); };
    T& at(size_t index);
    const T& at(size_t index) const;

    // adding methods

    void appendGhost();
    ArrayList<T>& append(const T& t);
    ArrayList<T>& append(const T& t, size_t i);
    ArrayList<T>& append(const ArrayList<T>& arr);
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

        if (i < m_arr.size)
        {
            m_arr[i].~T();
        }

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

    // removing methods

    int remove(const T& target);
    T pop(size_t index);
    T pop();

    // iterating methods

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

    bool contains(const T& target) const
    { return -1 != this->find(target); }

    int find(const T& target) const
    {
        for (int i = 0; i < m_arr.size; i++)
        {
            T& elem = m_arr.ptr[i];
            if (elem == target)
            {
                return i;
            }
        }
        return -1;
    }

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

    template <typename Compare>
    void sort(Compare&& compare)
    {
        std::stable_sort(begin(), end(), std::forward<Compare>(compare));
    }

    void sort()
    {
        std::stable_sort(begin(), end());
    }

    template <typename R>
    R reduce(std::function<R(const R&, const T&)> reducer, R initial) const
    {
        R acc = std::move(initial);

        for (size_t i = 0; i < this->length(); ++i)
        {
            acc = reducer(acc, this->at(i));
        }

        return acc;
    }

};

template <typename T>
void ArrayList<T>::_resize_if_necessary()
{
    if (!m_arr.full())
    { return; }

    const size_t next_capacity = m_arr.cap == 0 ?
        ARRAY_DEFAULT_INITIAL_CAPACITY :
        m_arr.cap << 1;
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
T ArrayList<T>::_pop_and_shuffle_down(size_t i)
{
    if (i >= m_arr.size)
    {
        throw std::out_of_range("ArrayList pop index out of range");
    }

    T ret = m_arr[i];

    for (size_t j = i; j + 1 < m_arr.size; ++j)
    {
        m_arr[j] = std::move(m_arr[j + 1]);
    }

    --m_arr.size;
    m_arr[m_arr.size] = T{};
    return ret;
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
    m_arr.size = arr.m_arr.size;
    m_arr.cap = arr.m_arr.cap;

    arr.m_arr.ptr = nullptr;
    arr.m_arr.size = 0;
    arr.m_arr.cap = 0;
}

template <typename T>
void ArrayList<T>::clear()
{
    delete[] m_arr.ptr;
    m_arr.ptr = nullptr;
    m_arr.size = 0;
    m_arr.cap = 0;

    _resize(ARRAY_DEFAULT_INITIAL_CAPACITY);
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

// appending methods

template <typename T>
void ArrayList<T>::appendGhost()
{
    _resize_if_necessary();
    ++m_arr.size;
}

template <typename T>
ArrayList<T>& ArrayList<T>::append(const T& t)
{
    this->append(t, m_arr.size);
    return *this;
}

template <typename T>
ArrayList<T>& ArrayList<T>::append(const T& t, size_t i)
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

template <typename T>
ArrayList<T>& ArrayList<T>::append(const ArrayList<T>& arr)
{
    this->append(arr.m_arr.ptr, arr.m_arr.size);
    return *this;
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

template <typename T>
int ArrayList<T>::remove(const T& target)
{
    const int index = this->find(target);
    if (index != -1)
        this->pop(index);

    return index;
}

template <typename T>
T ArrayList<T>::pop(const size_t index)
{
    return this->_pop_and_shuffle_down(index);
}

template <typename T>
T ArrayList<T>::pop()
{
    return this->pop(length() - 1);
}
