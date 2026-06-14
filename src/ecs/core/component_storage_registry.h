//
// Component pool ownership and lookup for simulation and render storage.
//

#pragma once

#include <array>
#include <memory>
#include <ranges>
#include <type_traits>
#include <unordered_map>

#include "../pools/component_pool.h"
#include "archetype_storage_registry.h"
#include "component_type_id.h"

class ComponentStorageRegistry
{
public:
    using PoolMap = std::unordered_map<ecs::ComponentTypeId, std::unique_ptr<IComponentPool>>;

    template <typename T>
    using component_key_t = ecs::component_key_t<T>;

    template <typename T>
    using component_value_t = ecs::component_value_t<T>;

    template <typename T>
    using pool_t = SimulationComponentPoolFor<component_value_t<T>>;

    template <typename T>
    using render_pool_t = RenderComponentPool<component_value_t<T>>;

private:
    class RenderArchetypeBuffers
    {
        struct PendingSync
        {
            RenderPoolSyncMode mode = RenderPoolSyncMode::None;
            ArrayList<size_t> entities;
        };

        std::array<ecs::ArchetypeStorageRegistry, 2> m_registries;
        std::array<size_t, 2> m_roleToBuffer { READ_INDEX, WRITE_INDEX };
        std::unordered_map<ecs::ComponentTypeId, PendingSync> m_pendingSyncs;
        bool m_shouldSwap = false;
        size_t m_generation = 0;

        ecs::ArchetypeStorageRegistry& read()
        { return m_registries[m_roleToBuffer[READ_INDEX]]; }

        const ecs::ArchetypeStorageRegistry& read() const
        { return m_registries[m_roleToBuffer[READ_INDEX]]; }

        ecs::ArchetypeStorageRegistry& write()
        { return m_registries[m_roleToBuffer[WRITE_INDEX]]; }

        const ecs::ArchetypeStorageRegistry& write() const
        { return m_registries[m_roleToBuffer[WRITE_INDEX]]; }

        static void copyFullComponent(
            const ecs::IArchetypePool& source,
            ecs::IArchetypePool& destination,
            const ecs::ComponentTypeId componentTypeId
        ) {
            destination.clearComponentStorage(componentTypeId);

            const size_t componentCount = source.componentSize(componentTypeId);
            for (size_t componentDenseIndex = 0; componentDenseIndex < componentCount; ++componentDenseIndex)
            {
                const size_t entityIndex = source.componentEntityAt(componentTypeId, componentDenseIndex);
                copyEntityComponent(source, destination, componentTypeId, entityIndex);
            }
        }

        static void copyEntityComponent(
            const ecs::IArchetypePool& source,
            ecs::IArchetypePool& destination,
            const ecs::ComponentTypeId componentTypeId,
            const size_t entityIndex
        ) {
            void* destinationComponent = destination.ensureComponentPointer(entityIndex, componentTypeId);
            if (nullptr != destinationComponent && source.copyComponentTo(entityIndex, componentTypeId, destinationComponent))
                return;

            destination.removeComponent(entityIndex, componentTypeId);
        }

        static void removeEntityComponent(
            ecs::IArchetypePool& destination,
            const ecs::ComponentTypeId componentTypeId,
            const size_t entityIndex
        ) {
            destination.removeComponent(entityIndex, componentTypeId);
        }

        void synchronizeInactiveBuffer()
        {
            for (const auto& [componentTypeId, pending] : m_pendingSyncs)
            {
                const ecs::IArchetypePool* source = read().poolForComponent(componentTypeId);
                ecs::IArchetypePool* destination = write().poolForComponent(componentTypeId);
                if (nullptr == destination)
                    continue;

                if (RenderPoolSyncMode::Full == pending.mode)
                {
                    if (nullptr == source)
                        destination->clearComponentStorage(componentTypeId);
                    else
                        copyFullComponent(*source, *destination, componentTypeId);
                    destination->clearComponentDirty(componentTypeId);
                    continue;
                }

                if (RenderPoolSyncMode::Entities != pending.mode)
                    continue;

                for (const size_t entityIndex : pending.entities)
                {
                    if (nullptr != source && source->hasComponent(entityIndex, componentTypeId))
                        copyEntityComponent(*source, *destination, componentTypeId, entityIndex);
                    else
                        removeEntityComponent(*destination, componentTypeId, entityIndex);
                }

                destination->clearComponentDirty(componentTypeId);
            }

            m_pendingSyncs.clear();
        }

    public:
        size_t generation() const
        { return m_generation; }

        ecs::ArchetypeStorageRegistry& readStorage()
        { return read(); }

        const ecs::ArchetypeStorageRegistry& readStorage() const
        { return read(); }

        ecs::ArchetypeStorageRegistry& writeStorage()
        { return write(); }

        const ecs::ArchetypeStorageRegistry& writeStorage() const
        { return write(); }

        template <typename... Components>
        ecs::ArchetypePool<Components...>& registerArchetype()
        {
            const size_t readGeneration = read().generation();
            const size_t writeGeneration = write().generation();

            auto& readPool = read().template registerArchetype<Components...>();
            (void)write().template registerArchetype<Components...>();

            if (readGeneration != read().generation() || writeGeneration != write().generation())
                ++m_generation;

            return readPool;
        }

        template <typename... Components>
        ecs::ArchetypePool<Components...>* writePoolIfExists()
        {
            return write().template poolIfExists<Components...>();
        }

        template <typename... Components>
        ecs::ArchetypePool<Components...>* poolIfExists()
        {
            return read().template poolIfExists<Components...>();
        }

        template <typename Component>
        bool containsComponent() const
        {
            return read().template containsComponent<Component>();
        }

        ecs::IArchetypePool* poolForComponent(const ecs::ComponentTypeId componentTypeId)
        {
            return read().poolForComponent(componentTypeId);
        }

        const ecs::IArchetypePool* poolForComponent(const ecs::ComponentTypeId componentTypeId) const
        {
            return read().poolForComponent(componentTypeId);
        }

        template <typename Component>
        ecs::IArchetypePool* poolForComponent()
        {
            return read().template poolForComponent<Component>();
        }

        template <typename Component>
        const ecs::IArchetypePool* poolForComponent() const
        {
            return read().template poolForComponent<Component>();
        }

        ecs::IArchetypePool* writePoolForComponent(const ecs::ComponentTypeId componentTypeId)
        {
            return write().poolForComponent(componentTypeId);
        }

        void markPendingFullSync(const ecs::ComponentTypeId componentTypeId)
        {
            PendingSync& pending = m_pendingSyncs[componentTypeId];
            pending.mode = RenderPoolSyncMode::Full;
            pending.entities.clear();
            m_shouldSwap = true;
        }

        void markPendingEntitySync(const ecs::ComponentTypeId componentTypeId, const ArrayList<size_t>& entityIndices)
        {
            if (entityIndices.empty())
                return;

            PendingSync& pending = m_pendingSyncs[componentTypeId];
            if (RenderPoolSyncMode::Full == pending.mode)
            {
                m_shouldSwap = true;
                return;
            }

            for (const size_t entityIndex : entityIndices)
            {
                if (!pending.entities.contains(entityIndex))
                    pending.entities.append(entityIndex);
            }

            pending.mode = RenderPoolSyncMode::Entities;
            m_shouldSwap = true;
        }

        void markPendingEntitySync(const ecs::ComponentTypeId componentTypeId, const size_t entityIndex)
        {
            PendingSync& pending = m_pendingSyncs[componentTypeId];
            if (RenderPoolSyncMode::Full == pending.mode)
            {
                m_shouldSwap = true;
                return;
            }

            if (!pending.entities.contains(entityIndex))
                pending.entities.append(entityIndex);

            pending.mode = RenderPoolSyncMode::Entities;
            m_shouldSwap = true;
        }

        void eraseEntityFromAll(const size_t entityIndex)
        {
            for (size_t poolIndex = 0; poolIndex < write().poolCount(); ++poolIndex)
            {
                ecs::IArchetypePool* writePool = write().poolAt(poolIndex);
                const std::vector<ecs::ComponentTypeId> componentTypeIds = writePool->componentTypes();

                for (const ecs::ComponentTypeId componentTypeId : componentTypeIds)
                {
                    const ecs::IArchetypePool* readPool = read().poolForComponent(componentTypeId);
                    const bool presentInRead = nullptr != readPool && readPool->hasComponent(entityIndex, componentTypeId);
                    const bool removedFromWrite = writePool->removeComponent(entityIndex, componentTypeId);

                    if (presentInRead || removedFromWrite)
                        markPendingEntitySync(componentTypeId, entityIndex);
                }
            }
        }

        void clearPools()
        {
            read().clearPools();
            write().clearPools();
            m_pendingSyncs.clear();
            m_shouldSwap = false;
            ++m_generation;
        }

        bool swapBuffers()
        {
            if (!m_shouldSwap)
                return false;

            std::swap(m_roleToBuffer[READ_INDEX], m_roleToBuffer[WRITE_INDEX]);
            m_shouldSwap = false;
            ++m_generation;
            synchronizeInactiveBuffer();
            return true;
        }
    };

    PoolMap m_componentPools;
    PoolMap m_renderComponentPools;
    ecs::ArchetypeStorageRegistry m_archetypes;
    RenderArchetypeBuffers m_renderArchetypes;
    size_t m_componentStorageGeneration = 0;
    size_t m_renderStorageGeneration = 0;

    template <typename Component, typename ArchetypePoolT>
    bool migrateStandaloneComponentIntoArchetype(ArchetypePoolT& archetypePool)
    {
        using ComponentValue = component_value_t<Component>;
        const ecs::ComponentTypeId key = ecs::component_type_id<component_key_t<ComponentValue>>();
        const auto it = m_componentPools.find(key);
        if (it == m_componentPools.end())
            return false;

        auto* pool = static_cast<pool_t<ComponentValue>*>(it->second.get());
        for (size_t denseIndex = 0; denseIndex < pool->size(); ++denseIndex)
        {
            archetypePool.template assignComponent<ComponentValue>(
                pool->entity_at(denseIndex),
                pool->dense_at(denseIndex)
            );
        }

        m_componentPools.erase(it);
        return true;
    }

    template <typename ArchetypePoolT, typename... Components>
    bool migrateStandaloneComponentsIntoArchetype(ArchetypePoolT& archetypePool)
    {
        bool migrated = false;
        ((migrated = migrateStandaloneComponentIntoArchetype<Components>(archetypePool) || migrated), ...);
        return migrated;
    }

    template <typename Component, typename ArchetypePoolT>
    bool migrateRenderComponentIntoArchetype(ArchetypePoolT& archetypePool)
    {
        return migrateRenderComponentIntoArchetype<Component>(archetypePool, archetypePool);
    }

    template <typename Component, typename ReadArchetypePoolT, typename WriteArchetypePoolT>
    bool migrateRenderComponentIntoArchetype(
        ReadArchetypePoolT& readArchetypePool,
        WriteArchetypePoolT& writeArchetypePool
    ) {
        using ComponentValue = component_value_t<Component>;
        const ecs::ComponentTypeId key = ecs::component_type_id<component_key_t<ComponentValue>>();
        const auto it = m_renderComponentPools.find(key);
        if (it == m_renderComponentPools.end())
            return false;

        auto* pool = static_cast<render_pool_t<ComponentValue>*>(it->second.get());
        for (size_t denseIndex = 0; denseIndex < pool->readSet().size(); ++denseIndex)
        {
            readArchetypePool.template assignComponent<ComponentValue>(
                pool->readSet().key_at(denseIndex),
                pool->readSet().dense_at(denseIndex)
            );
        }

        for (size_t denseIndex = 0; denseIndex < pool->writeSet().size(); ++denseIndex)
        {
            writeArchetypePool.template assignComponent<ComponentValue>(
                pool->writeSet().key_at(denseIndex),
                pool->writeSet().dense_at(denseIndex)
            );
        }

        if (pool->hasPendingPublish())
            m_renderArchetypes.markPendingFullSync(key);

        m_renderComponentPools.erase(it);
        return true;
    }

    template <typename ArchetypePoolT, typename... Components>
    bool migrateRenderComponentsIntoArchetype(ArchetypePoolT& archetypePool)
    {
        bool migrated = false;
        ((migrated = migrateRenderComponentIntoArchetype<Components>(archetypePool) || migrated), ...);
        return migrated;
    }

    template <typename ReadArchetypePoolT, typename WriteArchetypePoolT, typename... Components>
    bool migrateRenderComponentsIntoArchetype(
        ReadArchetypePoolT& readArchetypePool,
        WriteArchetypePoolT& writeArchetypePool
    ) {
        bool migrated = false;
        ((migrated = migrateRenderComponentIntoArchetype<Components>(
            readArchetypePool,
            writeArchetypePool
        ) || migrated), ...);
        return migrated;
    }

public:
    size_t componentStorageGeneration() const
    { return m_componentStorageGeneration; }

    size_t renderStorageGeneration() const
    { return m_renderStorageGeneration; }

    PoolMap& componentPools()
    { return m_componentPools; }

    const PoolMap& componentPools() const
    { return m_componentPools; }

    PoolMap& renderComponentPools()
    { return m_renderComponentPools; }

    const PoolMap& renderComponentPools() const
    { return m_renderComponentPools; }

    ecs::ArchetypeStorageRegistry& archetypes()
    { return m_archetypes; }

    const ecs::ArchetypeStorageRegistry& archetypes() const
    { return m_archetypes; }

    ecs::ArchetypeStorageRegistry& renderArchetypes()
    { return m_renderArchetypes.readStorage(); }

    const ecs::ArchetypeStorageRegistry& renderArchetypes() const
    { return m_renderArchetypes.readStorage(); }

    ecs::ArchetypeStorageRegistry& writableRenderArchetypes()
    { return m_renderArchetypes.writeStorage(); }

    const ecs::ArchetypeStorageRegistry& writableRenderArchetypes() const
    { return m_renderArchetypes.writeStorage(); }

    template <typename... Components>
    ecs::ArchetypePool<component_value_t<Components>...>& registerArchetype()
    {
        const size_t archetypeGeneration = m_archetypes.generation();
        auto& pool = m_archetypes.template registerArchetype<component_value_t<Components>...>();
        const bool migrated = migrateStandaloneComponentsIntoArchetype<
            decltype(pool),
            component_value_t<Components>...
        >(pool);

        if (archetypeGeneration != m_archetypes.generation() || migrated)
            ++m_componentStorageGeneration;

        return pool;
    }

    template <typename Component>
    bool isArchetypedComponent() const
    { return m_archetypes.template containsComponent<component_value_t<Component>>(); }

    template <typename... Components>
    ecs::ArchetypePool<component_value_t<Components>...>& registerRenderArchetype()
    {
        const size_t archetypeGeneration = m_renderArchetypes.generation();
        auto& pool = m_renderArchetypes.template registerArchetype<component_value_t<Components>...>();
        auto* writePool = m_renderArchetypes.template writePoolIfExists<component_value_t<Components>...>();
        if (nullptr == writePool)
            throw std::runtime_error("Render archetype write buffer was not registered");

        const bool migrated = migrateRenderComponentsIntoArchetype<
            decltype(pool),
            std::remove_pointer_t<decltype(writePool)>,
            component_value_t<Components>...
        >(pool, *writePool);

        if (archetypeGeneration != m_renderArchetypes.generation() || migrated)
            ++m_renderStorageGeneration;

        return pool;
    }

    template <typename Component>
    bool isRenderArchetypedComponent() const
    { return m_renderArchetypes.template containsComponent<component_value_t<Component>>(); }

    template <typename Component>
    ecs::IArchetypePool* archetypePoolForComponent()
    { return m_archetypes.template poolForComponent<component_value_t<Component>>(); }

    template <typename Component>
    const ecs::IArchetypePool* archetypePoolForComponent() const
    { return m_archetypes.template poolForComponent<component_value_t<Component>>(); }

    template <typename Component>
    ecs::IArchetypePool* renderArchetypePoolForComponent()
    { return m_renderArchetypes.template poolForComponent<component_value_t<Component>>(); }

    template <typename Component>
    const ecs::IArchetypePool* renderArchetypePoolForComponent() const
    { return m_renderArchetypes.template poolForComponent<component_value_t<Component>>(); }

    ecs::IArchetypePool* archetypePoolForComponent(const ecs::ComponentTypeId componentTypeId)
    { return m_archetypes.poolForComponent(componentTypeId); }

    const ecs::IArchetypePool* archetypePoolForComponent(const ecs::ComponentTypeId componentTypeId) const
    { return m_archetypes.poolForComponent(componentTypeId); }

    ecs::IArchetypePool* renderArchetypePoolForComponent(const ecs::ComponentTypeId componentTypeId)
    { return m_renderArchetypes.poolForComponent(componentTypeId); }

    const ecs::IArchetypePool* renderArchetypePoolForComponent(const ecs::ComponentTypeId componentTypeId) const
    { return m_renderArchetypes.poolForComponent(componentTypeId); }

    template <typename... Components>
    ecs::ArchetypePool<component_value_t<Components>...>* archetypePoolIfExists()
    {
        return m_archetypes.template poolIfExists<component_value_t<Components>...>();
    }

    template <typename... Components>
    ecs::ArchetypePool<component_value_t<Components>...>* renderArchetypePoolIfExists()
    {
        return m_renderArchetypes.template poolIfExists<component_value_t<Components>...>();
    }

    ecs::IArchetypePool* writableRenderArchetypePoolForComponent(const ecs::ComponentTypeId componentTypeId)
    { return m_renderArchetypes.writePoolForComponent(componentTypeId); }

    void markRenderArchetypeComponentFullSync(const ecs::ComponentTypeId componentTypeId)
    { m_renderArchetypes.markPendingFullSync(componentTypeId); }

    void markRenderArchetypeComponentEntitySync(
        const ecs::ComponentTypeId componentTypeId,
        const ArrayList<size_t>& entityIndices
    ) {
        m_renderArchetypes.markPendingEntitySync(componentTypeId, entityIndices);
    }

    template <typename T>
    pool_t<T>* storageIfExists()
    {
        const auto it = m_componentPools.find(ecs::component_type_id<component_key_t<T>>());
        if (it == m_componentPools.end())
            return nullptr;

        return static_cast<pool_t<T>*>(it->second.get());
    }

    template <typename T>
    const pool_t<T>* storageIfExists() const
    {
        const auto it = m_componentPools.find(ecs::component_type_id<component_key_t<T>>());
        if (it == m_componentPools.end())
            return nullptr;

        return static_cast<const pool_t<T>*>(it->second.get());
    }

    template <typename T>
    pool_t<T>& storage()
    {
        const ecs::ComponentTypeId key = ecs::component_type_id<component_key_t<T>>();
        auto it = m_componentPools.find(key);
        if (it == m_componentPools.end())
        {
            auto inserted = m_componentPools.emplace(key, std::make_unique<pool_t<T>>());
            it = inserted.first;
            ++m_componentStorageGeneration;
        }

        return *static_cast<pool_t<T>*>(it->second.get());
    }

    template <typename T>
    render_pool_t<T>* renderStorageIfExists()
    {
        const auto it = m_renderComponentPools.find(ecs::component_type_id<component_key_t<T>>());
        if (it == m_renderComponentPools.end())
            return nullptr;

        return static_cast<render_pool_t<T>*>(it->second.get());
    }

    template <typename T>
    const render_pool_t<T>* renderStorageIfExists() const
    {
        const auto it = m_renderComponentPools.find(ecs::component_type_id<component_key_t<T>>());
        if (it == m_renderComponentPools.end())
            return nullptr;

        return static_cast<const render_pool_t<T>*>(it->second.get());
    }

    template <typename T>
    render_pool_t<T>& renderStorage()
    {
        const ecs::ComponentTypeId key = ecs::component_type_id<component_key_t<T>>();
        auto it = m_renderComponentPools.find(key);
        if (it == m_renderComponentPools.end())
        {
            auto inserted = m_renderComponentPools.emplace(key, std::make_unique<render_pool_t<T>>());
            it = inserted.first;
            ++m_renderStorageGeneration;
        }

        return *static_cast<render_pool_t<T>*>(it->second.get());
    }

    void eraseEntityFromAll(const size_t entityIndex)
    {
        for (const auto& pool : m_componentPools | std::views::values)
            pool->erase(entityIndex);

        m_archetypes.eraseEntityFromAll(entityIndex);

        for (const auto& pool : m_renderComponentPools | std::views::values)
            pool->erase(entityIndex);

        m_renderArchetypes.eraseEntityFromAll(entityIndex);
    }

    void clearPools()
    {
        for (auto& pool : m_componentPools | std::views::values)
            pool->clear();

        m_archetypes.clearPools();

        for (auto& pool : m_renderComponentPools | std::views::values)
            pool->clear();

        m_renderArchetypes.clearPools();
    }

    void swapSimulationBuffers()
    {
        for (auto& pool : m_componentPools | std::views::values)
            pool->swapBuffers();
    }

    void swapRenderBuffers()
    {
        for (auto& pool : m_renderComponentPools | std::views::values)
            pool->swapBuffers();

        if (m_renderArchetypes.swapBuffers())
            ++m_renderStorageGeneration;
    }

    void markDirty(const ecs::ComponentTypeId componentTypeId)
    {
        const auto it = m_componentPools.find(componentTypeId);
        if (it != m_componentPools.end())
        {
            it->second->markDirty();
            return;
        }

        if (ecs::IArchetypePool* archetypePool = m_archetypes.poolForComponent(componentTypeId))
            archetypePool->markComponentDirty(componentTypeId);
    }

    void markEntityDirty(const ecs::ComponentTypeId componentTypeId, const size_t entityIndex)
    {
        const auto it = m_componentPools.find(componentTypeId);
        if (it != m_componentPools.end())
        {
            it->second->markEntityDirty(entityIndex);
            return;
        }

        if (ecs::IArchetypePool* archetypePool = m_archetypes.poolForComponent(componentTypeId))
            archetypePool->markComponentEntityDirty(componentTypeId, entityIndex);
    }

    bool markDirtyIfEntityCountReachesThreshold(
        const ecs::ComponentTypeId componentTypeId,
        const size_t entityCount
    ) {
        const auto it = m_componentPools.find(componentTypeId);
        if (it != m_componentPools.end())
            return it->second->markFullIfEntityDirtyCountReachesThreshold(entityCount);

        if (ecs::IArchetypePool* archetypePool = m_archetypes.poolForComponent(componentTypeId))
            return archetypePool->markComponentDirtyIfEntityCountReachesThreshold(componentTypeId, entityCount);

        return false;
    }
};
