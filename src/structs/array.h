//
// Created by Nicholas on 11/05/26.
//

#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

static constexpr size_t ARRAY_DEFAULT_INITIAL_CAPACITY = 16;

template <typename T, bool PREINITIALISE = false>
struct Array
{
    T* ptr = nullptr;
    size_t size = 0;
    size_t cap = 0;

    Array() = default;

    explicit Array(const size_t init_size)
    { this->resize(init_size); }

    Array(const Array& arr)
    {
        this->resize(arr.cap);

        if constexpr (PREINITIALISE)
        {
            size = cap;
            for (size_t i = 0; i < std::min(arr.size, size); ++i)
                ptr[i] = arr.ptr[i];
        }
        else
        {
            for (size_t i = 0; i < arr.size; ++i)
                construct(i, arr.ptr[i]);
            size = arr.size;
        }
    }

    Array(Array&& arr) noexcept
        : ptr(arr.ptr),
          size(arr.size),
          cap(arr.cap)
    {
        arr.ptr = nullptr;
        arr.size = 0;
        arr.cap = 0;
    }

    Array& operator=(const Array& arr)
    {
        if (this == &arr)
            return *this;

        this->clear();
        this->resize(arr.cap);

        if constexpr (PREINITIALISE)
        {
            size = cap;
            for (size_t i = 0; i < std::min(arr.size, size); ++i)
                ptr[i] = arr.ptr[i];
        }
        else
        {
            for (size_t i = 0; i < arr.size; ++i)
                construct(i, arr.ptr[i]);
            size = arr.size;
        }

        return *this;
    }

    Array& operator=(Array&& arr) noexcept
    {
        if (this == &arr)
            return *this;

        this->clear();
        ptr = arr.ptr;
        cap = arr.cap;
        size = arr.size;

        arr.ptr = nullptr;
        arr.cap = 0;
        arr.size = 0;

        return *this;
    }

    ~Array()
    { this->clear(); }

    T& operator[](const size_t index) { return ptr[index]; }
    const T& operator[](const size_t index) const { return ptr[index]; }
    T& at(const size_t index) { return ptr[index]; }
    const T& at(const size_t index) const { return ptr[index]; }

    template <typename... Args>
    T& construct(const size_t index, Args&&... args)
    {
        return *std::construct_at(ptr + index, std::forward<Args>(args)...);
    }

    void destroy(const size_t index) noexcept
    { std::destroy_at(ptr + index); }

    void resize(size_t new_size)
    {
        if (new_size < size)
            new_size = size;

        if (new_size == 0)
            new_size = ARRAY_DEFAULT_INITIAL_CAPACITY;

        T* new_arr = static_cast<T*>(::operator new(sizeof(T) * new_size));
        size_t constructed = 0;

        try
        {
            const size_t moved = std::min(size, new_size);
            for (; constructed < moved; ++constructed)
                std::construct_at(new_arr + constructed, std::move_if_noexcept(ptr[constructed]));

            if constexpr (PREINITIALISE)
            {
                for (; constructed < new_size; ++constructed)
                    std::construct_at(new_arr + constructed);
            }
        }
        catch (...)
        {
            for (size_t i = 0; i < constructed; ++i)
                std::destroy_at(new_arr + i);
            ::operator delete(new_arr);
            throw;
        }

        destroy_live_elements();
        ::operator delete(ptr);

        ptr = new_arr;
        cap = new_size;

        if constexpr (PREINITIALISE)
            size = new_size;
    }

    void guarantee(const size_t space)
    {
        if (space > cap)
            this->resize(space);
    }

    void reserve(const size_t space)
    {
        if (space > cap - size)
            this->resize(cap + space);
    }

    void restrict(const size_t space)
    { this->resize(space); }

    void clamp_size()
    { this->restrict(size); }

    bool empty() const
    { return size == 0; }

    bool full() const
    { return size >= cap; }

    size_t length() const
    { return size; }

    size_t capacity() const
    { return cap; }

    void realloc(const size_t space)
    {
        if (cap > space)
        {
            destroy_live_elements();
            if constexpr (PREINITIALISE)
            {
                for (size_t i = 0; i < cap; ++i)
                    std::construct_at(ptr + i);
                size = cap;
            }
            else
            {
                size = 0;
            }
            return;
        }

        clear();
        reserve(space);
    }

    void clear()
    {
        destroy_live_elements();
        ::operator delete(ptr);
        ptr = nullptr;
        size = 0;
        cap = 0;
    }

    using iterator = T*;
    using const_iterator = const T*;

    size_t _end() const
    {
        if constexpr (PREINITIALISE) return cap;
        else return size;
    }

    iterator begin()
    { return ptr; }

    iterator end()
    { return ptr + _end(); }

    const_iterator begin() const
    { return ptr; }

    const_iterator end() const
    { return ptr + _end(); }

    const_iterator cbegin() const
    { return ptr; }

    const_iterator cend() const
    { return ptr + _end(); }

    template <typename Func>
    void for_each(Func&& func)
    {
        for (size_t i = 0; i < size; i++)
            func(this->at(i));
    }

    template <typename Func>
    auto map(Func&& func) const
    {
        using U = std::decay_t<decltype(func(std::declval<T&>()))>;

        Array<U> dest(size);
        for (size_t i = 0; i < size; i++)
            dest.construct(i, func(ptr[i]));
        dest.size = size;

        return dest;
    }

    template <typename R>
    R reduce(std::function<R(const R&, const T&)> reducer, R initial) const
    {
        R acc = std::move(initial);

        for (size_t i = 0; i < size; ++i)
            acc = reducer(acc, ptr[i]);

        return acc;
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

private:
    void destroy_live_elements() noexcept
    {
        if (nullptr == ptr)
            return;

        for (size_t i = 0; i < size; ++i)
            std::destroy_at(ptr + i);
    }
};

template <typename T>
void array_cpy(Array<T>& dst, const size_t dst_start, const Array<T>& src, const size_t src_start, const size_t length)
{
    for (size_t i = 0; i < length; ++i)
    {
        dst[dst_start + i] = src[src_start + i];
    }
}
