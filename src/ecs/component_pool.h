//
// Created by Nicholas on 06/05/26.
//

#pragma once

#include <atomic>
#include <array>
#include <stdexcept>
#include <typeinfo>
#include <utility>

#include "../structs/templates.h"
#include "component_pool_dirty.h"
#include "component_pool_storage.h"
#include "component_type_id.h"

template <typename T>
class ComponentPool;

template <typename T>
class BufferedComponentPool;

template <typename T>
class RenderComponentPool;

class IComponentPool
{
    ComponentPoolDirtyTracker m_dirty;
    size_t m_generation = 0;

protected:
    void bumpGeneration()
    { ++m_generation; }

public:
    virtual ~IComponentPool() = default;
    virtual ecs::ComponentTypeId type_id() const = 0;
    virtual const char* type_name() const = 0;
    virtual void erase(size_t entityIndex) = 0;
    virtual void clear() = 0;
    virtual size_t size() const = 0;
    virtual void swapBuffers() = 0;
    virtual void writeFrom(IComponentPool&)
    { throw std::runtime_error("writeFrom is only supported by render component pools"); }

    size_t generation() const
    { return m_generation; }

    bool isDirty() const
    { return m_dirty.dirty(); }

    bool isFullyDirty() const
    { return m_dirty.fullyDirty(); }

    const ArrayList<size_t>& dirtyEntities() const
    { return m_dirty.entities(); }

    virtual void markDirty()
    { m_dirty.markFull(); }

    virtual void markEntityDirty(const size_t entityIndex)
    { m_dirty.markEntity(entityIndex, size()); }

    virtual void clearDirty()
    { m_dirty.clear(); }
};

template <typename T, typename StoragePolicy>
class SimulationComponentPoolBase : public IComponentPool
{
    friend class RenderComponentPool<T>;

    StoragePolicy m_storage;

public:
    using value_type = T;
    using dense_array_type = ArrayList<T>;

    SimulationComponentPoolBase() = default;

    ecs::ComponentTypeId type_id() const override
    { return ecs::component_type_id<T>(); }

    const char* type_name() const override
    { return ecs::component_type_name<T>(); }

    size_t size() const override
    { return sourceSet().size(); }

    bool contains(const size_t entityIndex) const
    { return sourceSet().contains(entityIndex); }

    T* try_get(const size_t entityIndex)
    { return sourceSet().try_get(entityIndex); }

    const T* try_get(const size_t entityIndex) const
    { return sourceSet().try_get(entityIndex); }

    T& at(const size_t entityIndex)
    { return sourceSet().at(entityIndex); }

    const T& at(const size_t entityIndex) const
    { return sourceSet().at(entityIndex); }

    template <typename... Args>
    T& emplace(const size_t entityIndex, Args&&... args)
    {
        const bool existed = contains(entityIndex);
        T& component = sourceSet().emplace(entityIndex, std::forward<Args>(args)...);
        m_storage.bind(component);
        noteEntityMutation(entityIndex, !existed);
        return component;
    }

    T& insert_or_assign(const size_t entityIndex, T value)
    {
        const bool existed = contains(entityIndex);
        T& component = sourceSet().insert_or_assign(entityIndex, std::move(value));
        m_storage.bind(component);
        noteEntityMutation(entityIndex, !existed);
        return component;
    }

    void erase(const size_t entityIndex) override
    {
        if (!contains(entityIndex))
            return;

        this->markEntityDirty(entityIndex);
        this->bumpGeneration();
        sourceSet().erase(entityIndex);
    }

    void clear() override
    {
        if (!sourceSet().empty())
            this->bumpGeneration();

        this->markDirty();
        sourceSet().clear();
    }

    size_t entity_at(const size_t denseIndex) const
    { return sourceSet().key_at(denseIndex); }

    T& dense_at(const size_t denseIndex)
    { return sourceSet().dense_at(denseIndex); }

    const T& dense_at(const size_t denseIndex) const
    { return sourceSet().dense_at(denseIndex); }

    ArrayList<T>& dense()
    { return sourceSet().dense_values(); }

    const ArrayList<T>& dense() const
    { return sourceSet().dense_values(); }

    size_t dense_index_of(const size_t entityIndex) const
    { return sourceSet().index_of(entityIndex); }

    void swapBuffers() override
    { m_storage.swapBuffers(); }

    void writeFrom(IComponentPool&) override
    { throw std::runtime_error("Simulation component pools cannot write from another pool"); }

protected:
    SparseSet<T>& sourceSet()
    { return m_storage.set(); }

    const SparseSet<T>& sourceSet() const
    { return m_storage.set(); }

private:
    void noteEntityMutation(const size_t entityIndex, const bool structural)
    {
        if (structural)
            this->bumpGeneration();

        this->markEntityDirty(entityIndex);
    }
};

template <typename T>
class ComponentPool final : public SimulationComponentPoolBase<T, DirectComponentStorage<T>>
{};

template <typename T>
class BufferedComponentPool final : public SimulationComponentPoolBase<T, BufferedComponentStorage<T>>
{
    using base_type = SimulationComponentPoolBase<T, BufferedComponentStorage<T>>;

    bool m_swapPending = false;

public:
    void markDirty() override
    {
        base_type::markDirty();
        m_swapPending = true;
    }

    void markEntityDirty(const size_t entityIndex) override
    {
        base_type::markEntityDirty(entityIndex);
        m_swapPending = true;
    }

    void swapBuffers() override
    {
        if (!m_swapPending)
            return;

        base_type::swapBuffers();
        m_swapPending = false;
    }
};

enum class RenderPoolSyncMode
{
    None,
    Entities,
    Full
};

template <typename T>
class RenderComponentPool final : public IComponentPool
{
    friend ComponentPool<T>;
    friend BufferedComponentPool<T>;
    friend SparseSet<T>;

    Pair<SparseSet<T>> m_storage;
    std::array<size_t, 2> m_roleToBuffer { READ_INDEX, WRITE_INDEX };
    std::atomic_bool m_shouldSwap = false;
    RenderPoolSyncMode m_pendingSyncMode = RenderPoolSyncMode::None;
    ArrayList<size_t> m_pendingSyncEntities;

public:
    RenderComponentPool() = default;

    ecs::ComponentTypeId type_id() const override
    { return ecs::component_type_id<T>(); }

    const char* type_name() const override
    { return ecs::component_type_name<T>(); }

    size_t size() const override
    { return readSet().size(); }

    bool contains(const size_t entityIndex) const
    { return readSet().contains(entityIndex); }

    const T* try_get(const size_t entityIndex) const
    { return readSet().try_get(entityIndex); }

    void erase(const size_t entityIndex) override
    {
        const bool changed = m_storage.at(READ_INDEX).contains(entityIndex)
            || m_storage.at(WRITE_INDEX).contains(entityIndex);

        m_storage.at(READ_INDEX).erase(entityIndex);
        m_storage.at(WRITE_INDEX).erase(entityIndex);

        if (changed)
            this->bumpGeneration();
    }

    void clear() override
    {
        if (!m_storage.at(READ_INDEX).empty() || !m_storage.at(WRITE_INDEX).empty())
            this->bumpGeneration();

        m_storage.at(READ_INDEX).clear();
        m_storage.at(WRITE_INDEX).clear();
        m_pendingSyncEntities.clear();
        m_pendingSyncMode = RenderPoolSyncMode::None;
        m_shouldSwap.store(false, std::memory_order_release);
    }

    SparseSet<T>& readSet()
    { return m_storage.at(read_index()); }

    const SparseSet<T>& readSet() const
    { return m_storage.at(read_index()); }

    SparseSet<T>& writeSet()
    { return m_storage.at(write_index()); }

    const SparseSet<T>& writeSet() const
    { return m_storage.at(write_index()); }

    size_t entity_at(const size_t denseIndex) const
    { return readSet().key_at(denseIndex); }

    const T& dense_at(const size_t denseIndex) const
    { return readSet().dense_at(denseIndex); }

    size_t dense_index_of(const size_t entityIndex) const
    { return readSet().index_of(entityIndex); }

    size_t read_index() const
    { return m_roleToBuffer[READ_INDEX]; }

    size_t write_index() const
    { return m_roleToBuffer[WRITE_INDEX]; }

    void swapBuffers() override
    { publishPendingWrites(); }

    void writeFrom(ComponentPool<T>& componentPool)
    {
        if (componentPool.isFullyDirty())
            writeFullDirectFromSet(componentPool.sourceSet());
        else
            writeDirtyDirectFromSet(componentPool.sourceSet(), componentPool.dirtyEntities());
    }

    template <typename U = T>
        requires ecs::is_buffered_component_v<U>
    void writeFrom(BufferedComponentPool<U>& componentPool)
    {
        if (componentPool.isFullyDirty())
            writeFullBufferedFromSet(componentPool.sourceSet());
        else
            writeDirtyBufferedFromSet(componentPool.sourceSet(), componentPool.dirtyEntities());
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
    bool consumePendingPublish()
    {
        return m_shouldSwap.exchange(false, std::memory_order_acq_rel);
    }

    void swapReadWriteRoles()
    {
        std::swap(m_roleToBuffer[READ_INDEX], m_roleToBuffer[WRITE_INDEX]);
    }

    void publishPendingWrites()
    {
        if (!consumePendingPublish())
            return;

        swapReadWriteRoles();
        this->bumpGeneration();
        synchronizeInactiveBuffer();
    }

    template <typename Value>
    T& emplaceInto(SparseSet<T>& destination, const size_t entityIndex, Value&& value)
    {
        T& component = destination.emplace(entityIndex, std::forward<Value>(value));
        component_pool_detail::bind_to_role_lookup(component, m_roleToBuffer.data());
        return component;
    }

    void emplaceRenderCopy(SparseSet<T>& destination, const size_t entityIndex, const T& value)
    {
        if constexpr (ecs::is_buffered_component_v<T>)
            emplaceInto(destination, entityIndex, value.read());
        else
            emplaceInto(destination, entityIndex, value);
    }

    void copyRenderSet(SparseSet<T>& destination, const SparseSet<T>& source)
    {
        destination.clear();

        for (size_t i = 0; i < source.size(); ++i)
            emplaceRenderCopy(destination, source.key_at(i), source.dense_at(i));
    }

    void markPendingFullSync()
    {
        m_pendingSyncEntities.clear();
        m_pendingSyncMode = RenderPoolSyncMode::Full;
        m_shouldSwap.store(true, std::memory_order_release);
    }

    void markPendingEntitySync(const ArrayList<size_t>& dirtyEntities)
    {
        m_pendingSyncEntities = dirtyEntities;
        m_pendingSyncMode = RenderPoolSyncMode::Entities;
        m_shouldSwap.store(true, std::memory_order_release);
    }

    void syncEntityFromReadToWrite(const size_t entityIndex)
    {
        const T* component = readSet().try_get(entityIndex);
        if (nullptr == component)
        {
            writeSet().erase(entityIndex);
            return;
        }

        emplaceRenderCopy(writeSet(), entityIndex, *component);
    }

    void synchronizeInactiveBuffer()
    {
        switch (m_pendingSyncMode)
        {
            case RenderPoolSyncMode::None:
                return;
            case RenderPoolSyncMode::Full:
                copyRenderSet(writeSet(), readSet());
                break;
            case RenderPoolSyncMode::Entities:
                for (const size_t entityIndex : m_pendingSyncEntities)
                    syncEntityFromReadToWrite(entityIndex);
                break;
        }

        m_pendingSyncEntities.clear();
        m_pendingSyncMode = RenderPoolSyncMode::None;
    }

    void writeFullDirectFromSet(const SparseSet<T>& source)
    {
        SparseSet<T>& storage = writeSet();
        storage.clear();

        for (size_t i = 0; i < source.size(); ++i)
            emplaceInto(storage, source.key_at(i), source.dense_at(i));

        markPendingFullSync();
    }

    void writeDirtyDirectFromSet(const SparseSet<T>& source, const ArrayList<size_t>& dirtyEntities)
    {
        SparseSet<T>& storage = writeSet();

        for (const size_t entityIndex : dirtyEntities)
        {
            const T* component = source.try_get(entityIndex);
            if (nullptr == component)
                storage.erase(entityIndex);
            else
                emplaceInto(storage, entityIndex, *component);
        }

        if (!dirtyEntities.empty())
            markPendingEntitySync(dirtyEntities);
    }

    template <typename U = T>
        requires ecs::is_buffered_component_v<U>
    void writeFullBufferedFromSet(const SparseSet<U>& source)
    {
        SparseSet<T>& storage = writeSet();
        storage.clear();

        for (size_t i = 0; i < source.size(); ++i)
            emplaceInto(storage, source.key_at(i), source.dense_at(i).read());

        markPendingFullSync();
    }

    template <typename U = T>
        requires ecs::is_buffered_component_v<U>
    void writeDirtyBufferedFromSet(const SparseSet<U>& source, const ArrayList<size_t>& dirtyEntities)
    {
        SparseSet<T>& storage = writeSet();

        for (const size_t entityIndex : dirtyEntities)
        {
            const U* component = source.try_get(entityIndex);
            if (nullptr == component)
                storage.erase(entityIndex);
            else
                emplaceInto(storage, entityIndex, component->read());
        }

        if (!dirtyEntities.empty())
            markPendingEntitySync(dirtyEntities);
    }
};
