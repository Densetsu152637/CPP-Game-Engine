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

namespace ecs
{
    template <typename T>
    struct Tag
    {
        using tag_type = std::remove_cvref_t<T>;
    };

    template <typename T>
    struct Exclude
    {
        using filter_type = std::remove_cvref_t<T>;
    };

    template <typename T>
    struct tag_name
    {
        using type = std::remove_cvref_t<T>;
    };

    template <typename T>
    struct tag_name<Tag<T>>
    {
        using type = std::remove_cvref_t<T>;
    };

    template <typename T>
    using tag_name_t = typename tag_name<std::remove_cvref_t<T>>::type;

    template <typename T>
    struct excluded_filter
    {
        using type = void;
    };

    template <typename T>
    struct excluded_filter<Exclude<T>>
    {
        using type = std::remove_cvref_t<T>;
    };

    template <typename T>
    using excluded_filter_t = typename excluded_filter<std::remove_cvref_t<T>>::type;

    template <typename... Components>
    struct ViewOf
    {
        static constexpr size_t component_count = sizeof...(Components);
    };

    template <typename T>
    struct Dirty
    {
        using component_type = std::remove_cvref_t<T>;
    };

    template <typename T>
    struct Shared
    {
        using component_type = std::remove_cvref_t<T>;
    };
}

template <typename T>
using Dirty = ecs::Dirty<T>;

template <typename T>
using Exclude = ecs::Exclude<T>;

template <typename T>
using Shared = ecs::Shared<T>;
