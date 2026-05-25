//
// Created by Nicholas on 07/05/26.
//

#pragma once

#include <stdexcept>
#include <utility>

template <typename T>
struct Pair
{
    union
    {
        T buff[2];

        struct
        {
            T first;
            T second;
        };
    };

    Pair() = default;
    explicit Pair(T& obj) : first(obj), second(obj) {}

    template <typename U1, typename U2>
    constexpr Pair(U1&& a, U2&& b)
        : first(std::forward<U1>(a))
        , second(std::forward<U2>(b))
    {}
};

template <typename T>
struct Read
{
    using value_type = T;
    using src_type = T;

    T& src;

    explicit Read(Pair<T>& p, size_t read, size_t write) :
        src(p.buff[read])
    {}
    explicit Read(T& s) : src(s) {}
    explicit operator const T&() const { return src; }
    Read& operator=(T& _ignored)
    { throw std::runtime_error("Illegal use of Read operator="); }
};

template <typename T>
struct Write
{
    using value_type = T;
    using dst_type = T;

    T& dst;

    explicit Write(Pair<T>& p, size_t read, size_t write) :
        dst(p.buff[write])
    {}
    explicit Write(T& d) : dst(d) {}
    explicit operator T&()
    { throw std::runtime_error("Illegal use of Write implicit conversion="); }
    Write& operator=(T value)
    {
        dst = std::move(value);
        return *this;
    }
};

template <typename T>
struct ReadWrite
{
    using value_type = T;

    T& src;
    T& dst;

    explicit ReadWrite(Pair<T>& p, size_t read, size_t write) :
        src(p.buff[read]),
        dst(p.buff[write])
    {}
    explicit ReadWrite(T& s, T& d) : src(s), dst(d) {}
    explicit operator T&() { return src; }
    ReadWrite& operator=(T value)
    {
        dst = std::move(value);
        return *this;
    }
};


