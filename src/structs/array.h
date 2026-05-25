//
// Created by Nicholas on 11/05/26.
//

#pragma once

static constexpr size_t ARRAY_DEFAULT_INITIAL_CAPACITY = 16;

template <typename T, bool PREINITIALISE = false>
struct Array
{
    T* ptr = nullptr;
    size_t size = 0;
    size_t cap = 0;

    Array() = default;
    Array(const size_t init_size)
    { this->resize(init_size); }

    Array(const Array& arr)
    {
        this->clear();
        this->resize(arr.cap);
        for (size_t i = 0; i < this->size; i++)
        {
            this->ptr[i] = arr.ptr[i];
        }
    }

    Array(Array&& arr) noexcept
    {
        delete[] ptr;
        ptr = arr.ptr;
        cap = arr.cap;
        size = arr.size;

        arr.ptr = nullptr;
        arr.cap = 0;
        arr.size = 0;
    }

    Array& operator=(const Array& arr)
    {
        this->clear();
        this->resize(arr.cap);
        for (size_t i = 0; i < this->size; i++)
        {
            this->ptr[i] = arr.ptr[i];
        }
        return *this;
    }

    Array& operator=(Array&& arr) noexcept
    {
        delete[] ptr;
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

    T& operator[](const size_t index) { return ptr[index]; };
    const T& operator[](const size_t index) const { return ptr[index]; };
    T& at(size_t index) { return ptr[index]; }
    const T& at(size_t index) const { return ptr[index]; };

    void resize(size_t new_size)
    {
        if (new_size < size)
            new_size = size;

        if (new_size == 0)
            new_size = ARRAY_DEFAULT_INITIAL_CAPACITY;

        T* new_arr;
        if constexpr (PREINITIALISE)
            new_arr = new T[new_size] {};
        else
            new_arr = new T[new_size];

        if (nullptr != ptr)
        {
            for (size_t i = 0; i < size; ++i)
                new_arr[i] = std::move(ptr[i]);
        }

        delete[] ptr;
        ptr = new_arr;
        cap = new_size;
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

    bool empty() const { return size == 0; }
    bool full() const
    { return size >= cap; }

    size_t length() const { return size; }
    size_t capacity() const { return cap; }

    void clear()
    {
        delete[] ptr;
        ptr = nullptr;
        size = 0;
        cap = 0;
    }

    using iterator = T*;
    using const_iterator = const T*;

    size_t _end()
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
        for (size_t i = 0; i < this->size; i++)
            func(this->at(i));
    }

    template <typename Func>
    auto map(Func&& func) const
    {
        using U = std::decay_t<decltype(func(std::declval<T&>()))>;

        Array<U> dest(this->size);

        for (size_t i = 0; i < this->size; i++)
            dest[i] = std::move(func(this->ptr[i]));

        return dest;
    }

    template <typename R>
    R reduce(std::function<R(const R&, const T&)> reducer, R initial) const
    {
        R acc = std::move(initial);

        for (size_t i = 0; i < this->size; ++i)
            acc = reducer(acc, this->ptr[i]);

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

};

template <typename T>
void array_cpy(Array<T>& dst, const int dst_start, const Array<T>& src, const int src_start, const int length)
{
    for (int i = 0; i < length; ++i)
    {
        dst[dst_start + i] = src[src_start + i];
    }
}