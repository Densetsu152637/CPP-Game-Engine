//
// Component pool ownership and lookup for simulation and render storage.
//

#pragma once

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
    using pool_t = std::conditional_t<
        ecs::is_buffered_component_v<component_value_t<T>>,
        BufferedComponentPool<component_value_t<T>>,
        ComponentPool<component_value_t<T>>
    >;

    template <typename T>
    using render_pool_t = RenderComponentPool<component_value_t<T>>;

private:
    PoolMap m_componentPools;
    PoolMap m_renderComponentPools;
    ecs::ArchetypeStorageRegistry m_archetypes;
    ecs::ArchetypeStorageRegistry m_renderArchetypes;
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
        using ComponentValue = component_value_t<Component>;
        const ecs::ComponentTypeId key = ecs::component_type_id<component_key_t<ComponentValue>>();
        const auto it = m_renderComponentPools.find(key);
        if (it == m_renderComponentPools.end())
            return false;

        auto* pool = static_cast<render_pool_t<ComponentValue>*>(it->second.get());
        for (size_t denseIndex = 0; denseIndex < pool->size(); ++denseIndex)
        {
            archetypePool.template assignComponent<ComponentValue>(
                pool->entity_at(denseIndex),
                pool->dense_at(denseIndex)
            );
        }

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
    { return m_renderArchetypes; }

    const ecs::ArchetypeStorageRegistry& renderArchetypes() const
    { return m_renderArchetypes; }

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
        const bool migrated = migrateRenderComponentsIntoArchetype<
            decltype(pool),
            component_value_t<Components>...
        >(pool);

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
