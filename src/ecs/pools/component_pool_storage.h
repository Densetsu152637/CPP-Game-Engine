//
// Simulation component storage policies.
//

#pragma once

#include <array>
#include <cstddef>
#include <utility>

#include "../../structs/sparse_set.h"
#include "../aliases/component_alias.h"

constexpr int READ_INDEX = 0;
constexpr int WRITE_INDEX = 1;

namespace component_pool_detail
{
    template <typename T>
    void bind_to_role_lookup(T&, const size_t*) noexcept
    {}

    template <typename T>
        requires ecs::is_buffered_component_v<T>
    void bind_to_role_lookup(T& component, const size_t* roleLookup) noexcept
    { component.bindRoleLookup(roleLookup); }
}

template <typename T>
class DirectComponentStorage
{
    SparseSet<T> m_storage;

public:
    using value_type = T;

    SparseSet<T>& set()
    { return m_storage; }

    const SparseSet<T>& set() const
    { return m_storage; }

    T& bind(T& component) const noexcept
    { return component; }

    void swapBuffers() noexcept
    {}
};

template <typename T>
class BufferedComponentStorage
{
    SparseSet<T> m_storage;
    std::array<size_t, 2> m_roleToBuffer { READ_INDEX, WRITE_INDEX };

public:
    using value_type = T;

    static_assert(
        ecs::is_buffered_component_v<T>,
        "BufferedComponentStorage can only store ecs::BufferedAlias component types"
    );

    SparseSet<T>& set()
    { return m_storage; }

    const SparseSet<T>& set() const
    { return m_storage; }

    T& bind(T& component) const noexcept
    {
        component_pool_detail::bind_to_role_lookup(component, m_roleToBuffer.data());
        return component;
    }

    size_t read_index() const
    { return m_roleToBuffer[READ_INDEX]; }

    size_t write_index() const
    { return m_roleToBuffer[WRITE_INDEX]; }

    void swapBuffers() noexcept
    {
        std::swap(m_roleToBuffer[READ_INDEX], m_roleToBuffer[WRITE_INDEX]);
    }
};
