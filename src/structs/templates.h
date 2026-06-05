//
// Created by Nicholas on 07/05/26.
//

#pragma once

#include <array>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "../ecs/component_alias.h"

template <typename T>
struct Pair
{
    std::array<T, 2> buffers {};

    Pair() = default;
    explicit Pair(const T& obj) : buffers { obj, obj } {}
    explicit Pair(T&& obj) : buffers { obj, std::move(obj) } {}

    template <typename U1, typename U2>
    constexpr Pair(U1&& a, U2&& b)
        : buffers {
            std::forward<U1>(a),
            std::forward<U2>(b)
        }
    {}

    T& at(const size_t index)
    {
        return buffers[index];
    }

    const T& at(const size_t index) const
    {
        return buffers[index];
    }
};

template <typename T>
struct Read
{
    using component_type = std::remove_cvref_t<T>;
    using value_type = ecs::component_value_t<component_type>;
    using src_type = value_type;

    value_type& src;

    explicit Read(Pair<value_type>& p, const size_t* roleLookup) :
        src(p.at(roleLookup[0]))
    {}
    explicit operator const value_type&() const { return src; }
    Read& operator=(value_type& _ignored)
    { throw std::runtime_error("Illegal use of Read operator="); }
};

template <typename T>
struct Write
{
    using component_type = std::remove_cvref_t<T>;
    using value_type = ecs::component_value_t<component_type>;
    using dst_type = value_type;

    value_type& dst;

    explicit Write(Pair<value_type>& p, const size_t* roleLookup) :
        dst(p.at(roleLookup[1]))
    {}
    explicit operator value_type&()
    { throw std::runtime_error("Illegal use of Write implicit conversion="); }
    Write& operator=(value_type value)
    {
        dst = std::move(value);
        return *this;
    }
};

template <typename T>
struct ReadWrite
{
    using component_type = std::remove_cvref_t<T>;
    using value_type = ecs::component_value_t<component_type>;

    value_type& src;
    value_type& dst;

    explicit ReadWrite(Pair<value_type>& p, const size_t* roleLookup) :
        src(p.at(roleLookup[0])),
        dst(p.at(roleLookup[1]))
    {}
    explicit operator value_type&() { return src; }
    ReadWrite& operator=(value_type value)
    {
        dst = std::move(value);
        return *this;
    }
};


