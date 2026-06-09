//
// Created by Nicholas on 07/05/26.
//

#pragma once

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

class ECS;

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
    class ViewOf
    {
    public:
        using component_type = std::remove_cvref_t<T>;
        using value_type = component_type;

        class iterator;

        ViewOf() = default;
        explicit ViewOf(const ::ECS& ecs)
            : m_ecs(&ecs)
        {}

        iterator begin() const;
        iterator end() const;
        size_t size() const;
        bool empty() const;

    private:
        const ::ECS* m_ecs = nullptr;
    };

    template <typename T>
    struct Dirty
    {
        using component_type = std::remove_cvref_t<T>;
    };
}

template <typename T>
using Dirty = ecs::Dirty<T>;
