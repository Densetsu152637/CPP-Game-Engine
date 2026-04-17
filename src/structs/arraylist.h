//
// Created by Nicholas on 17/04/26.
//

#ifndef CPP_GAME_ENGINE_ARRAYLIST_H
#define CPP_GAME_ENGINE_ARRAYLIST_H

#include <cstddef>
#include <functional>

size_t round_to_nearest_2n(size_t size);

template <typename T>
class ArrayList {

    static constexpr int DEFAULT_INITIAL_CAPACITY = 16;

    T* m_arr;
    size_t m_size = 0;
    size_t m_capacity = 0;

    void _assert_within_bounds(size_t& i) const;
    void _wrap_around_size(size_t& i) const;
    void _resize(size_t new_size);
    void _pop_and_shuffle_down(size_t i);
    void _append_and_shuffle_up(const T& t, size_t i);

public:

    using iterator = T*;
    using const_iterator = const T*;

    ArrayList() : ArrayList(DEFAULT_INITIAL_CAPACITY) {}

    ArrayList(const size_t initial_capacity)
    { _resize(round_to_nearest_2n(initial_capacity)); }

    ArrayList(const ArrayList& arr);
    ArrayList(ArrayList&& arr) noexcept;

    size_t length() const
    { return m_size; }

    size_t capacity() const
    { return m_capacity; }

    void reserve(size_t space)
    { _resize(round_to_nearest_2n(m_capacity + space)); }

    void restrict(size_t space)
    { _resize(space); }

    void clamp_size()
    { restrict(m_size); }

    //

    T& operator[](const size_t index) { return at(index); };
    T& at(size_t index);

    ArrayList<T>& append(const T& t)
    {
        append(t, m_size);
        return *this;
    }

    ArrayList<T>& append(const T& t, const size_t i)
    {
        _append_and_shuffle_up(t, i);
        return *this;
    }

    ArrayList<T>& append(const ArrayList<T>& arr)
    {
        append(arr.m_arr, arr.m_size);
        return *this;
    }

    ArrayList<T>& append(const T* ts, size_t elems);

    ArrayList<T> concat(const ArrayList<T>& arr);

    //

    iterator begin()
    { return m_arr; }

    iterator end()
    { return m_arr + m_size; }

    const_iterator begin() const
    { return m_arr; }

    const_iterator end() const
    { return m_arr + m_size; }

    const_iterator cbegin() const
    { return m_arr; }

    const_iterator cend() const
    { return m_arr + m_size; }

    template <typename U> void for_each(std::function<U (T&)> func)
    {
        for (int i = 0; i < m_size; i++)
        {
            T& t = m_arr[i];
            if (NULL != t)
            {
                func(t);
            }
        }
    }

    template <typename U> ArrayList<U> map(std::function<U (T&)> func)
    {
        ArrayList<U> dest(m_capacity);
        for (int i = 0; i < m_size; i++)
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
