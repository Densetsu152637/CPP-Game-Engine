//
// Render component-pool storage and publish/sync implementation.
//

#pragma once

#include <atomic>
#include <array>
#include <stdexcept>
#include <utility>

#include "../core/archetype_storage_registry.h"
#include "component_pool_simulation.h"

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

    size_t entityAt(const size_t denseIndex) const override
    { return entity_at(denseIndex); }

    bool contains(const size_t entityIndex) const
    { return readSet().contains(entityIndex); }

    bool containsEntity(const size_t entityIndex) const override
    { return contains(entityIndex); }

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

    bool copyComponentTo(const size_t entityIndex, void* destination) const override
    {
        if (nullptr == destination)
            return false;

        const T* component = try_get(entityIndex);
        if (nullptr == component)
            return false;

        *static_cast<T*>(destination) = *component;
        return true;
    }

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

    void writeFromArchetype(
        ecs::IArchetypePool& archetypePool,
        const ecs::ComponentTypeId componentTypeId
    ) override {
        if (componentTypeId != type_id() || !archetypePool.containsComponent(componentTypeId))
            throw std::invalid_argument("RenderComponentPool received incompatible archetype source");

        if (archetypePool.isComponentFullyDirty(componentTypeId))
            writeFullFromArchetype(archetypePool, componentTypeId);
        else
            writeDirtyFromArchetype(
                archetypePool,
                componentTypeId,
                archetypePool.componentDirtyEntities(componentTypeId)
            );
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

    void writeFullFromArchetype(
        const ecs::IArchetypePool& archetypePool,
        const ecs::ComponentTypeId componentTypeId
    ) {
        SparseSet<T>& storage = writeSet();
        storage.clear();

        for (size_t denseIndex = 0; denseIndex < archetypePool.size(); ++denseIndex)
        {
            const size_t entityIndex = archetypePool.entityAt(denseIndex);
            T component {};
            if (archetypePool.copyComponentTo(entityIndex, componentTypeId, &component))
                emplaceRenderCopy(storage, entityIndex, component);
        }

        markPendingFullSync();
    }

    void writeDirtyFromArchetype(
        const ecs::IArchetypePool& archetypePool,
        const ecs::ComponentTypeId componentTypeId,
        const ArrayList<size_t>& dirtyEntities
    ) {
        SparseSet<T>& storage = writeSet();

        for (const size_t entityIndex : dirtyEntities)
        {
            T component {};
            if (archetypePool.copyComponentTo(entityIndex, componentTypeId, &component))
                emplaceRenderCopy(storage, entityIndex, component);
            else
                storage.erase(entityIndex);
        }

        if (!dirtyEntities.empty())
            markPendingEntitySync(dirtyEntities);
    }
};
