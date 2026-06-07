//
// Created by Nicholas on 07/05/26.
//

#pragma once

#include <array>
#include <type_traits>
#include <utility>

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
struct ArrayFor
{
    using component_type = std::remove_cvref_t<T>;
};

namespace ecs
{
    template <typename T>
    struct Dirty
    {
        using component_type = std::remove_cvref_t<T>;
    };
}

template <typename T>
using Dirty = ecs::Dirty<T>;
