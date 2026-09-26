//
// Simulation component storage policies.
//

#pragma once

#include <array>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include "../core/entt_storage.h"
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
class SparseComponentStorage
{
    ecs::EnTTStorage<T> m_storage;

public:
    using value_type = T;

    ecs::EnTTStorage<T>& set()
    { return m_storage; }

    const ecs::EnTTStorage<T>& set() const
    { return m_storage; }

    ecs::BackendSet& backend() { return m_storage.backend(); }

    size_t size() const
    { return m_storage.size(); }

    bool empty() const
    { return m_storage.empty(); }

    bool contains(const size_t entityIndex) const
    { return m_storage.contains(entityIndex); }

    T* try_get(const size_t entityIndex)
    { return m_storage.try_get(entityIndex); }

    const T* try_get(const size_t entityIndex) const
    { return m_storage.try_get(entityIndex); }

    T& at(const size_t entityIndex)
    { return m_storage.at(entityIndex); }

    const T& at(const size_t entityIndex) const
    { return m_storage.at(entityIndex); }

    void erase(const size_t entityIndex)
    { m_storage.erase(entityIndex); }

    void clear()
    { m_storage.clear(); }

    size_t entity_at(const size_t denseIndex) const
    { return m_storage.key_at(denseIndex); }

    size_t dense_index_of(const size_t entityIndex) const
    { return m_storage.index_of(entityIndex); }

    T& dense_at(const size_t denseIndex)
    { return m_storage.dense_at(denseIndex); }

    const T& dense_at(const size_t denseIndex) const
    { return m_storage.dense_at(denseIndex); }

    auto dense()
    { return m_storage.dense_values(); }

    auto dense() const
    { return m_storage.dense_values(); }

    template <typename... Args>
    T& emplace_record(const size_t entityIndex, Args&&... args)
    { return m_storage.emplace(entityIndex, std::forward<Args>(args)...); }

    T& insert_or_assign_record(const size_t entityIndex, T value)
    { return m_storage.insert_or_assign(entityIndex, std::move(value)); }
};

template <typename T>
class DirectComponentStorage : public SparseComponentStorage<T>
{
    using base_type = SparseComponentStorage<T>;

public:
    using value_type = T;

    T& bind(T& component) const noexcept
    { return component; }

    template <typename... Args>
    T& emplace(const size_t entityIndex, Args&&... args)
    {
        T& component = base_type::emplace_record(entityIndex, std::forward<Args>(args)...);
        return bind(component);
    }

    T& insert_or_assign(const size_t entityIndex, T value)
    {
        T& component = base_type::insert_or_assign_record(entityIndex, std::move(value));
        return bind(component);
    }

    void swapBuffers() noexcept
    {}
};

template <typename T>
class BufferedComponentStorage : public SparseComponentStorage<T>
{
    using base_type = SparseComponentStorage<T>;

    std::array<size_t, 2> m_roleToBuffer { READ_INDEX, WRITE_INDEX };

public:
    using value_type = T;

    static_assert(
        ecs::is_buffered_component_v<T>,
        "BufferedComponentStorage can only store ecs::BufferedAlias component types"
    );

    T& bind(T& component) const noexcept
    {
        component_pool_detail::bind_to_role_lookup(component, m_roleToBuffer.data());
        return component;
    }

    template <typename... Args>
    T& emplace(const size_t entityIndex, Args&&... args)
    {
        T& component = base_type::emplace_record(entityIndex, std::forward<Args>(args)...);
        return bind(component);
    }

    T& insert_or_assign(const size_t entityIndex, T value)
    {
        T& component = base_type::insert_or_assign_record(entityIndex, std::move(value));
        return bind(component);
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

template <typename... Components>
class SparseTupleComponentStorage : public SparseComponentStorage<std::tuple<Components...>>
{
    using tuple_type = std::tuple<Components...>;
    using base_type = SparseComponentStorage<tuple_type>;

public:
    using value_type = tuple_type;

    static_assert(
        (... && !ecs::is_buffered_component_v<Components>),
        "SparseTupleComponentStorage currently supports direct components only"
    );

    template <typename... Args>
    tuple_type& emplace(const size_t entityIndex, Args&&... args)
    { return base_type::emplace_record(entityIndex, std::forward<Args>(args)...); }

    tuple_type& insert_or_assign(const size_t entityIndex, tuple_type value)
    { return base_type::insert_or_assign_record(entityIndex, std::move(value)); }

    template <typename Component>
    Component* try_get_component(const size_t entityIndex)
    {
        tuple_type* record = this->try_get(entityIndex);
        if (nullptr == record)
            return nullptr;

        return &std::get<Component>(*record);
    }

    template <typename Component>
    const Component* try_get_component(const size_t entityIndex) const
    {
        const tuple_type* record = this->try_get(entityIndex);
        if (nullptr == record)
            return nullptr;

        return &std::get<Component>(*record);
    }

    template <typename Component>
    Component& component_at(const size_t entityIndex)
    { return std::get<Component>(this->at(entityIndex)); }

    template <typename Component>
    const Component& component_at(const size_t entityIndex) const
    { return std::get<Component>(this->at(entityIndex)); }

    template <typename Component>
    Component& dense_component_at(const size_t denseIndex)
    { return std::get<Component>(this->dense_at(denseIndex)); }

    template <typename Component>
    const Component& dense_component_at(const size_t denseIndex) const
    { return std::get<Component>(this->dense_at(denseIndex)); }
};
