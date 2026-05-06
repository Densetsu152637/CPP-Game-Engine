//
// Created by Codex on 07/05/26.
//

#pragma once

#include <algorithm>
#include <functional>
#include <utility>

#include "arraylist.h"

template <typename T>
class SortedArray
{
    ArrayList<T> m_data;
    std::function<bool(const T&, const T&)> m_compare;

    static bool default_compare(const T& lhs, const T& rhs)
    { return lhs < rhs; }

    bool less(const T& lhs, const T& rhs) const
    { return m_compare(lhs, rhs); }

    bool equivalent(const T& lhs, const T& rhs) const
    { return !less(lhs, rhs) && !less(rhs, lhs); }

    size_t lower_bound_index(const T& value) const
    {
        size_t first = 0;
        size_t count = m_data.length();

        while (count > 0)
        {
            const size_t step = count / 2;
            const size_t mid = first + step;

            if (less(m_data[mid], value))
            {
                first = mid + 1;
                count -= step + 1;
            }
            else
            {
                count = step;
            }
        }

        return first;
    }

    size_t upper_bound_index(const T& value) const
    {
        size_t first = 0;
        size_t count = m_data.length();

        while (count > 0)
        {
            const size_t step = count / 2;
            const size_t mid = first + step;

            if (!less(value, m_data[mid]))
            {
                first = mid + 1;
                count -= step + 1;
            }
            else
            {
                count = step;
            }
        }

        return first;
    }

    size_t insert_sorted(const T& value)
    {
        const size_t index = upper_bound_index(value);
        m_data.append(value, index);
        return index;
    }

    size_t insert_sorted(T&& value)
    {
        const size_t index = upper_bound_index(value);
        m_data.append(std::move(value), index);
        return index;
    }

public:
    using iterator = typename ArrayList<T>::iterator;
    using const_iterator = typename ArrayList<T>::const_iterator;

    SortedArray()
        : SortedArray(std::function<bool(const T&, const T&)>(default_compare), ARRAY_DEFAULT_INITIAL_CAPACITY)
    {}

    explicit SortedArray(const size_t initial_capacity)
        : SortedArray(std::function<bool(const T&, const T&)>(default_compare), initial_capacity)
    {}

    explicit SortedArray(std::function<bool(const T&, const T&)> compare, const size_t initial_capacity = ARRAY_DEFAULT_INITIAL_CAPACITY)
        : m_data(initial_capacity),
          m_compare(std::move(compare))
    {}

    SortedArray(const SortedArray&) = default;
    SortedArray(SortedArray&&) noexcept = default;
    SortedArray& operator=(const SortedArray&) = default;
    SortedArray& operator=(SortedArray&&) noexcept = default;

    size_t length() const
    { return m_data.length(); }

    size_t capacity() const
    { return m_data.capacity(); }

    bool empty() const
    { return m_data.empty(); }

    ArrayList<T>& ptr()
    { return m_data; }

    const ArrayList<T>& ptr() const
    { return m_data; }

    void guarantee(const size_t space)
    { m_data.guarantee(space); }

    void reserve(const size_t space)
    { m_data.reserve(space); }

    void restrict(const size_t space)
    { m_data.restrict(space); }

    void clamp_size()
    { m_data.clamp_size(); }

    void clear()
    { m_data.clear(); }

    T& operator[](const size_t index)
    { return m_data[index]; }

    const T& operator[](const size_t index) const
    { return m_data[index]; }

    T& at(const size_t index)
    { return m_data.at(index); }

    const T& at(const size_t index) const
    { return m_data.at(index); }

    void appendGhost()
    {
        m_data.appendGhost();
        sort();
    }

    SortedArray<T>& append(const T& value)
    {
        insert_sorted(value);
        return *this;
    }

    SortedArray<T>& append(T&& value)
    {
        insert_sorted(std::move(value));
        return *this;
    }

    SortedArray<T>& append(const T& value, size_t)
    {
        return append(value);
    }

    SortedArray<T>& append(const SortedArray<T>& arr)
    {
        for (size_t i = 0; i < arr.length(); ++i)
        {
            append(arr[i]);
        }

        return *this;
    }

    SortedArray<T>& append(const ArrayList<T>& arr)
    {
        for (size_t i = 0; i < arr.length(); ++i)
        {
            append(arr[i]);
        }

        return *this;
    }

    SortedArray<T>& append(const T* ts, size_t elems)
    {
        for (size_t i = 0; i < elems; ++i)
        {
            append(ts[i]);
        }

        return *this;
    }

    SortedArray<T> concat(const SortedArray<T>& arr)
    {
        SortedArray<T> ret(m_compare, m_data.capacity() + arr.length());
        ret.append(*this);
        ret.append(arr);
        return ret;
    }

    SortedArray<T> concat(const ArrayList<T>& arr)
    {
        SortedArray<T> ret(m_compare, m_data.capacity() + arr.length());
        ret.append(*this);
        ret.append(arr);
        return ret;
    }

    template <typename... Args>
    T& emplace(size_t, Args&&... args)
    {
        T value(std::forward<Args>(args)...);
        const size_t index = insert_sorted(std::move(value));
        return m_data[index];
    }

    template <typename... Args>
    T& emplace(Args&&... args)
    {
        T value(std::forward<Args>(args)...);
        const size_t index = insert_sorted(std::move(value));
        return m_data[index];
    }

    T pop(const size_t index)
    { return m_data.pop(index); }

    T pop()
    { return m_data.pop(); }

    int remove(const T& target)
    {
        const int index = find(target);
        if (index != -1)
            m_data.pop(static_cast<size_t>(index));

        return index;
    }

    bool contains(const T& target) const
    { return -1 != find(target); }

    int find(const T& target) const
    {
        const size_t index = lower_bound_index(target);
        if (index >= m_data.length())
            return -1;

        return equivalent(m_data[index], target) ? static_cast<int>(index) : -1;
    }

    template <typename Func>
    void for_each(Func&& func)
    {
        m_data.for_each(std::forward<Func>(func));
    }

    template <typename Func>
    auto map(Func&& func)
    {
        using U = std::decay_t<decltype(func(std::declval<T&>()))>;
        SortedArray<U> dest;

        for (size_t i = 0; i < length(); ++i)
        {
            dest.append(func(m_data[i]));
        }

        return dest;
    }

    template <typename Compare>
    void sort(Compare&& compare)
    {
        m_compare = std::function<bool(const T&, const T&)>(std::forward<Compare>(compare));
        m_data.sort(m_compare);
    }

    void sort()
    {
        m_data.sort(m_compare);
    }

    template <typename R>
    R reduce(std::function<R(const R&, const T&)> reducer, R initial) const
    { return m_data.reduce(std::move(reducer), std::move(initial)); }

    iterator begin()
    { return m_data.begin(); }

    iterator end()
    { return m_data.end(); }

    const_iterator begin() const
    { return m_data.begin(); }

    const_iterator end() const
    { return m_data.end(); }

    const_iterator cbegin() const
    { return m_data.cbegin(); }

    const_iterator cend() const
    { return m_data.cend(); }
};
