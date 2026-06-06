//
// Created by Nicholas on 06/05/26.
//

#pragma once

#include <atomic>
#include <array>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "../structs/sparse_set.h"
#include "../structs/templates.h"
#include "component_alias.h"

constexpr int READ_INDEX = 0;
constexpr int WRITE_INDEX = 1;

template <typename T>
class ComponentPool;

template <typename T>
class BufferedComponentPool;

template <typename T>
class RenderComponentPool;

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

class IComponentPool
{
public:
    virtual ~IComponentPool() = default;
    virtual void erase(size_t entityIndex) = 0;
    virtual void clear() = 0;
    virtual size_t size() const = 0;
    virtual void swapBuffers() = 0;
    virtual void writeFrom(IComponentPool&)
    { throw std::runtime_error("writeFrom is only supported by render component pools"); }
};

template <typename T>
class ComponentPool final : public IComponentPool
{
    friend class RenderComponentPool<T>;

    SparseSet<T> m_storage;

public:
    using value_type = T;
    using dense_array_type = ArrayList<T>;

    ComponentPool() = default;

    size_t size() const override
    { return m_storage.size(); }

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

    template <typename... Args>
    T& emplace(const size_t entityIndex, Args&&... args)
    { return m_storage.emplace(entityIndex, std::forward<Args>(args)...); }

    T& insert_or_assign(const size_t entityIndex, T value)
    { return m_storage.insert_or_assign(entityIndex, std::move(value)); }

    void erase(const size_t entityIndex) override
    { m_storage.erase(entityIndex); }

    void clear() override
    { m_storage.clear(); }

    size_t entity_at(const size_t denseIndex) const
    { return m_storage.key_at(denseIndex); }

    T& dense_at(const size_t denseIndex)
    { return m_storage.dense_at(denseIndex); }

    const T& dense_at(const size_t denseIndex) const
    { return m_storage.dense_at(denseIndex); }

    ArrayList<T>& dense()
    { return m_storage.dense_values(); }

    const ArrayList<T>& dense() const
    { return m_storage.dense_values(); }

    size_t dense_index_of(const size_t entityIndex) const
    { return m_storage.index_of(entityIndex); }

    void swapBuffers() override {}

    void writeFrom(IComponentPool&) override
    { throw std::runtime_error("ComponentPool cannot write from another pool"); }
};

template <typename T>
class BufferedComponentPool final : public IComponentPool
{
    friend class RenderComponentPool<T>;

    SparseSet<T> m_storage;
    std::array<size_t, 2> m_roleToBuffer { READ_INDEX, WRITE_INDEX };

public:
    using value_type = T;
    using dense_array_type = ArrayList<T>;

    static_assert(
        ecs::is_buffered_component_v<T>,
        "BufferedComponentPool can only store ecs::BufferedAlias component types"
    );

    BufferedComponentPool() = default;

    size_t size() const override
    { return m_storage.size(); }

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

    template <typename... Args>
    T& emplace(const size_t entityIndex, Args&&... args)
    {
        T& component = m_storage.emplace(entityIndex, std::forward<Args>(args)...);
        bind(component);
        return component;
    }

    T& insert_or_assign(const size_t entityIndex, T value)
    {
        T& component = m_storage.insert_or_assign(entityIndex, std::move(value));
        bind(component);
        return component;
    }

    void erase(const size_t entityIndex) override
    { m_storage.erase(entityIndex); }

    void clear() override
    { m_storage.clear(); }

    size_t entity_at(const size_t denseIndex) const
    { return m_storage.key_at(denseIndex); }

    T& dense_at(const size_t denseIndex)
    { return m_storage.dense_at(denseIndex); }

    const T& dense_at(const size_t denseIndex) const
    { return m_storage.dense_at(denseIndex); }

    ArrayList<T>& dense()
    { return m_storage.dense_values(); }

    const ArrayList<T>& dense() const
    { return m_storage.dense_values(); }

    size_t dense_index_of(const size_t entityIndex) const
    { return m_storage.index_of(entityIndex); }

    size_t read_index() const
    { return m_roleToBuffer[READ_INDEX]; }

    size_t write_index() const
    { return m_roleToBuffer[WRITE_INDEX]; }

    void swapBuffers() override
    {
        std::swap(m_roleToBuffer[READ_INDEX], m_roleToBuffer[WRITE_INDEX]);
    }

    void writeFrom(IComponentPool&) override
    { throw std::runtime_error("BufferedComponentPool cannot write from another pool"); }

private:
    void bind(T& component) const noexcept
    { component_pool_detail::bind_to_role_lookup(component, m_roleToBuffer.data()); }
};

template <typename T>
class RenderComponentPool final : public IComponentPool
{
    friend ComponentPool<T>;
    friend BufferedComponentPool<T>;
    friend SparseSet<T>;

    Pair<SparseSet<T>> m_storage;
    std::array<size_t, 2> m_roleToBuffer {READ_INDEX, WRITE_INDEX};
    std::atomic_bool m_shouldSwap = false;

public:
    RenderComponentPool() = default;

    size_t size() const override
    { return readSet().size(); }

    bool contains(const size_t entityIndex) const
    { return readSet().contains(entityIndex); }

    const T* try_get(const size_t entityIndex) const
    { return readSet().try_get(entityIndex); }

    void erase(const size_t entityIndex) override
    {
        m_storage.at(READ_INDEX).erase(entityIndex);
        m_storage.at(WRITE_INDEX).erase(entityIndex);
    }

    void clear() override
    {
        m_storage.at(READ_INDEX).clear();
        m_storage.at(WRITE_INDEX).clear();
        m_shouldSwap.store(false, std::memory_order_release);
    }

    SparseSet<T>& readSet()
    { return m_storage.at(read_index()); }

    const SparseSet<T>& readSet() const
    { return m_storage.at(read_index()); }

    size_t entity_at(const size_t denseIndex) const
    { return readSet().key_at(denseIndex); }

    const T& dense_at(const size_t denseIndex) const
    { return readSet().dense_at(denseIndex); }

    size_t read_index() const
    { return m_roleToBuffer[READ_INDEX]; }

    size_t write_index() const
    { return m_roleToBuffer[WRITE_INDEX]; }

    void swapBuffers() override
    {
        if (m_shouldSwap)
        {
            m_shouldSwap = false;
            std::swap(m_roleToBuffer[READ_INDEX], m_roleToBuffer[WRITE_INDEX]);
        }
    }

    void writeFrom(ComponentPool<T>& componentPool)
    {
        writeDirectFromSet(componentPool.m_storage);
    }

    template <typename U = T>
        requires ecs::is_buffered_component_v<U>
    void writeFrom(BufferedComponentPool<U>& componentPool)
    {
        writeBufferedFromSet(componentPool.m_storage);
    }

    void writeFrom(IComponentPool& componentPool) override
    {
        if (auto* typedPool = dynamic_cast<ComponentPool<T>*>(&componentPool))
        {
            writeFrom(*typedPool);
            return;
        }

        if constexpr (ecs::is_buffered_component_v<T>)
        {
            if (auto* bufferedPool = dynamic_cast<BufferedComponentPool<T>*>(&componentPool))
            {
                writeFrom(*bufferedPool);
                return;
            }
        }

        throw std::invalid_argument("RenderComponentPool received incompatible source pool");
    }

private:
    void writeDirectFromSet(const SparseSet<T>& readSet)
    {
        SparseSet<T>& storage = m_storage.at(write_index());
        storage.clear();

        for (size_t i = 0; i < readSet.size(); ++i)
        {
            T& component = storage.emplace(readSet.key_at(i), readSet.dense_at(i));
            component_pool_detail::bind_to_role_lookup(component, m_roleToBuffer.data());
        }

        m_shouldSwap.store(true, std::memory_order_release);
    }

    template <typename U = T>
        requires ecs::is_buffered_component_v<U>
    void writeBufferedFromSet(const SparseSet<U>& readSet)
    {
        SparseSet<T>& storage = m_storage.at(write_index());
        storage.clear();

        for (size_t i = 0; i < readSet.size(); ++i)
        {
            T& component = storage.emplace(readSet.key_at(i), readSet.dense_at(i).read());
            component_pool_detail::bind_to_role_lookup(component, m_roleToBuffer.data());
        }

        m_shouldSwap.store(true, std::memory_order_release);
    }
};
