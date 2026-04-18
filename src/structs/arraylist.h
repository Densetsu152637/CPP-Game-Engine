//
// Created by Nicholas on 17/04/26.
//

#ifndef CPP_GAME_ENGINE_ARRAYLIST_H
#define CPP_GAME_ENGINE_ARRAYLIST_H

#include <cstddef>
#include <functional>

size_t round_to_nearest_2n(size_t size);

static constexpr size_t ARRAY_DEFAULT_INITIAL_CAPACITY = 16;

template <typename T>
struct Array
{

    T* ptr = nullptr;
    size_t size = 0;
    size_t cap = 0;

    T& operator[](const size_t index) { return ptr[index]; };

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

    bool full()
    {
        return size >= cap;
    }

};

template <typename T>
void array_cpy(Array<T>& dst, int dst_start, Array<T>& src, int src_start, int length);

template <typename T>
class ArrayList {

    Array<T> m_arr;
    void _resize(size_t new_size);
    void _resize_if_necessary();

    void _assert_within_bounds(size_t& i) const;
    void _wrap_around_size(size_t& i) const;
    void _pop_and_shuffle_down(size_t i);
    void _append_and_shuffle_up(const T& t, size_t i);

public:

    using iterator = T*;
    using const_iterator = const T*;

    ArrayList() : ArrayList(Array::DEFAULT_INITIAL_CAPACITY) {}

    ArrayList(const size_t initial_capacity)
    { _resize(round_to_nearest_2n(initial_capacity)); }

    ArrayList(const ArrayList& arr);
    ArrayList(ArrayList&& arr) noexcept;

    ~ArrayList()
    {
        if (nullptr != m_arr) delete[] m_arr.ptr;
        m_arr = nullptr;
    }

    Array<T>& ptr() { return m_arr; }

    size_t length() const
    { return m_arr.size; }

    size_t capacity() const
    { return m_arr.cap; }

    void reserve(size_t space)
    {
        if (m_arr.size + space >= m_arr.cap)
            this->_resize(m_arr.capacity + space);
    }

    void restrict(size_t space)
    { _resize(space); }

    void clamp_size()
    { this->restrict(m_arr.size); }

    //

    T& operator[](const size_t index) { return this->at(index); };
    T& at(size_t index);

    ArrayList<T>& append(const T& t)
    {
        this->append(t, m_arr.size);
        return *this;
    }

    ArrayList<T>& append(const T& t, const size_t i)
    {
        this->_append_and_shuffle_up(t, i);
        return *this;
    }

    ArrayList<T>& append(const ArrayList<T>& arr)
    {
        this->append(arr.m_arr, arr.m_arr.size);
        return *this;
    }

    ArrayList<T>& append(const T* ts, size_t elems);

    ArrayList<T> concat(const ArrayList<T>& arr);

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

    template <typename U> void for_each(std::function<U (T&)>&& func)
    {
        for (int i = 0; i < m_arr.size; i++)
        {
            T& t = m_arr[i];
            if (NULL != t)
            {
                func(t);
            }
        }
    }

    template <typename U> ArrayList<U> map(std::function<U (T&)>&& func)
    {
        ArrayList<U> dest(m_arr.cap);
        for (int i = 0; i < m_arr.size; i++)
        {
            T& t = m_arr[i];
            if (NULL != t)
            {
                dest.marr[i] = func(t);
            }
        }
        return dest;
    }

};

#endif //CPP_GAME_ENGINE_ARRAYLIST_H
