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
#include "structs/templates.h"

constexpr int READ_INDEX = 0;
constexpr int WRITE_INDEX = 1;

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

    SparseSet<Pair<T>> m_storage;
    std::array<size_t, 2> m_roleToBuffer {READ_INDEX, WRITE_INDEX};

public:
    using value_type = T;
    using pair_type = Pair<T>;

    ComponentPool() = default;

    size_t size() const override
    { return m_storage.size(); }

    bool contains(const size_t entityIndex) const
    { return m_storage.contains(entityIndex); }

    Pair<T>* try_get(const size_t entityIndex)
    { return m_storage.try_get(entityIndex); }

    const Pair<T>* try_get(const size_t entityIndex) const
    { return m_storage.try_get(entityIndex); }

    Pair<T>& at(const size_t entityIndex)
    { return m_storage.at(entityIndex); }

    const Pair<T>& at(const size_t entityIndex) const
    { return m_storage.at(entityIndex); }

    template <typename... Args>
    Pair<T>& emplace(const size_t entityIndex, Args&&... args)
    { return m_storage.emplace(entityIndex, std::forward<Args>(args)...); }

    Pair<T>& insert_or_assign(const size_t entityIndex, T value)
    { return m_storage.insert_or_assign(entityIndex, Pair { value } ); }

    void erase(const size_t entityIndex) override
    { m_storage.erase(entityIndex); }

    void clear() override
    { m_storage.clear(); }

    size_t entity_at(const size_t denseIndex) const
    { return m_storage.key_at(denseIndex); }

    Pair<T>& dense_at(const size_t denseIndex)
    { return m_storage.dense_at(denseIndex); }

    const Pair<T>& dense_at(const size_t denseIndex) const
    { return m_storage.dense_at(denseIndex); }

    size_t dense_index_of(const size_t entityIndex) const
    { return m_storage.index_of(entityIndex); }

    size_t read_index() const
    { return m_roleToBuffer[READ_INDEX]; }

    size_t write_index() const
    { return m_roleToBuffer[WRITE_INDEX]; }

    const size_t* role_lookup() const
    { return m_roleToBuffer.data(); }

    void swapBuffers() override
    { std::swap(m_roleToBuffer[READ_INDEX], m_roleToBuffer[WRITE_INDEX]); }

    void writeFrom(IComponentPool&) override
    { throw std::runtime_error("ComponentPool cannot write from another pool"); }

};

template <typename T>
class RenderComponentPool final : public IComponentPool
{
    friend ComponentPool<T>;
    friend SparseSet<T>;
    friend SparseSet<Pair<T>>;

    Pair<SparseSet<T>> m_storage;
    std::array<size_t, 2> m_roleToBuffer {READ_INDEX, WRITE_INDEX};
    std::atomic_bool m_shouldSwap = false;

public:

    RenderComponentPool() = default;

    size_t size() const override
    { return readSet().size(); }

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
        const SparseSet<Pair<T>>& readSet = componentPool.m_storage;
        const size_t readIndex = componentPool.read_index();
        SparseSet<T>& storage = m_storage.at(write_index());

        storage.clear();

        for (size_t i = 0; i < readSet.size(); ++i)
        {
            const Pair<T>& pair = readSet.dense_at(i);
            storage.emplace(readSet.key_at(i), pair.at(readIndex));
        }

        m_shouldSwap.store(true, std::memory_order_release);
    }

    void writeFrom(IComponentPool& componentPool) override
    {
        auto* typedPool = dynamic_cast<ComponentPool<T>*>(&componentPool);
        if (nullptr == typedPool)
            throw std::invalid_argument("RenderComponentPool received incompatible source pool");

        writeFrom(*typedPool);
    }

};
