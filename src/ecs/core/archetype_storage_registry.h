//
// User-declared archetype storage groups backed by sparse tuple rows.
//

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "../pools/component_pool_dirty.h"
#include "../pools/component_pool_storage.h"
#include "component_type_id.h"

namespace ecs
{
    namespace archetype_detail
    {
        inline bool contains_type_id(const std::vector<ComponentTypeId>& ids, const ComponentTypeId id)
        {
            return std::find(ids.begin(), ids.end(), id) != ids.end();
        }

        inline bool same_component_set(
            const std::vector<ComponentTypeId>& lhs,
            const std::vector<ComponentTypeId>& rhs
        ) {
            return lhs.size() == rhs.size()
                && std::all_of(lhs.begin(), lhs.end(), [&](const ComponentTypeId id)
                {
                    return contains_type_id(rhs, id);
                });
        }

        inline bool is_superset(
            const std::vector<ComponentTypeId>& candidate,
            const std::vector<ComponentTypeId>& existing
        ) {
            return std::all_of(existing.begin(), existing.end(), [&](const ComponentTypeId id)
            {
                return contains_type_id(candidate, id);
            });
        }

        template <typename... Components>
        std::vector<ComponentTypeId> component_ids()
        {
            std::vector<ComponentTypeId> ids { component_type_id<component_key_t<Components>>()... };
            std::ranges::sort(ids);
            const auto uniqueEnd = std::ranges::unique(ids).begin();
            ids.erase(uniqueEnd, ids.end());
            return ids;
        }

        template <typename... Components>
        std::vector<ComponentTypeId> component_ids_in_tuple_order()
        {
            return { component_type_id<component_key_t<Components>>()... };
        }
    }

    class IArchetypePool
    {
    public:
        virtual ~IArchetypePool() = default;
        virtual const std::vector<ComponentTypeId>& componentTypes() const = 0;
        virtual size_t size() const = 0;
        virtual size_t componentSize(ComponentTypeId componentTypeId) const = 0;
        virtual size_t componentEntityAt(ComponentTypeId componentTypeId, size_t componentDenseIndex) const = 0;
        virtual size_t generation() const = 0;
        virtual bool containsEntity(size_t entityIndex) const = 0;
        virtual bool hasComponent(size_t entityIndex, ComponentTypeId componentTypeId) const = 0;
        virtual size_t denseIndexOf(size_t entityIndex) const = 0;
        virtual size_t entityAt(size_t denseIndex) const = 0;
        virtual void ensureEntity(size_t entityIndex) = 0;
        virtual void* ensureComponentPointer(size_t entityIndex, ComponentTypeId componentTypeId) = 0;
        virtual bool removeComponent(size_t entityIndex, ComponentTypeId componentTypeId) = 0;
        virtual void clearComponentStorage(ComponentTypeId componentTypeId) = 0;
        virtual void eraseEntity(size_t entityIndex) = 0;
        virtual void clear() = 0;
        virtual void* componentPointer(size_t entityIndex, ComponentTypeId componentTypeId) = 0;
        virtual const void* componentPointer(size_t entityIndex, ComponentTypeId componentTypeId) const = 0;
        virtual void* denseComponentPointer(size_t denseIndex, ComponentTypeId componentTypeId) = 0;
        virtual const void* componentPointerAt(ComponentTypeId componentTypeId, size_t componentDenseIndex) const = 0;
        virtual bool componentPointers(
            size_t entityIndex,
            const ComponentTypeId* componentTypeIds,
            void** destinations,
            size_t count
        ) = 0;
        virtual bool denseComponentPointers(
            size_t denseIndex,
            const ComponentTypeId* componentTypeIds,
            void** destinations,
            size_t count
        ) = 0;
        virtual bool copyComponentTo(size_t entityIndex, ComponentTypeId componentTypeId, void* destination) const = 0;
        virtual void copyRowsInto(IArchetypePool& destination) const = 0;
        virtual bool isComponentDirty(ComponentTypeId componentTypeId) const = 0;
        virtual bool isComponentFullyDirty(ComponentTypeId componentTypeId) const = 0;
        virtual const ArrayList<size_t>& componentDirtyEntities(ComponentTypeId componentTypeId) const = 0;
        virtual void markComponentDirty(ComponentTypeId componentTypeId) = 0;
        virtual void markComponentEntityDirty(ComponentTypeId componentTypeId, size_t entityIndex) = 0;
        virtual bool markComponentDirtyIfEntityCountReachesThreshold(
            ComponentTypeId componentTypeId,
            size_t entityCount
        ) = 0;
        virtual void clearComponentDirty(ComponentTypeId componentTypeId) = 0;

        bool containsComponent(const ComponentTypeId componentTypeId) const
        { return archetype_detail::contains_type_id(componentTypes(), componentTypeId); }

        size_t dense_index_of(const size_t entityIndex) const
        { return denseIndexOf(entityIndex); }

        size_t entity_at(const size_t denseIndex) const
        { return entityAt(denseIndex); }

        bool overlaps(const std::vector<ComponentTypeId>& componentTypeIds) const
        {
            return std::any_of(componentTypeIds.begin(), componentTypeIds.end(), [&](const ComponentTypeId id)
            {
                return containsComponent(id);
            });
        }
    };

    template <typename... Components>
    class ArchetypePool final : public IArchetypePool
    {
        static constexpr size_t COMPONENT_COUNT = sizeof...(Components);
        static constexpr size_t N_POS = static_cast<size_t>(-1);

        using tuple_type = std::tuple<component_value_t<Components>...>;

        struct Row
        {
            tuple_type components {};
            std::array<bool, COMPONENT_COUNT> present {};

            bool anyPresent() const
            {
                return std::any_of(present.begin(), present.end(), [](const bool value) { return value; });
            }
        };

        using storage_type = SparseComponentStorage<Row>;

        storage_type m_storage;
        std::vector<ComponentTypeId> m_componentTypes =
            archetype_detail::component_ids_in_tuple_order<Components...>();
        std::array<size_t, COMPONENT_COUNT> m_presentCounts {};
        std::array<ArrayList<size_t>, COMPONENT_COUNT> m_presentEntities {};
        std::array<ComponentPoolDirtyTracker, COMPONENT_COUNT> m_dirty {};
        mutable std::mutex m_dirtyMutex;
        size_t m_generation = 0;

        template <typename Component>
        static ComponentTypeId type_id()
        { return component_type_id<component_key_t<Component>>(); }

        size_t componentIndex(const ComponentTypeId componentTypeId) const
        {
            for (size_t i = 0; i < m_componentTypes.size(); ++i)
            {
                if (m_componentTypes[i] == componentTypeId)
                    return i;
            }

            return N_POS;
        }

        void markComponentDirtyByIndex(const size_t componentIndex)
        {
            std::lock_guard lock(m_dirtyMutex);
            m_dirty[componentIndex].markFull();
        }

        void markComponentEntityDirtyByIndex(const size_t componentIndex, const size_t entityIndex)
        {
            std::lock_guard lock(m_dirtyMutex);
            m_dirty[componentIndex].markEntity(entityIndex, m_presentCounts[componentIndex]);
        }

        Row& ensureRow(const size_t entityIndex)
        {
            Row* row = m_storage.try_get(entityIndex);
            if (nullptr != row)
                return *row;

            ++m_generation;
            return m_storage.emplace_record(entityIndex);
        }

        void eraseRowIfEmpty(const size_t entityIndex, Row& row)
        {
            if (row.anyPresent())
                return;

            m_storage.erase(entityIndex);
            ++m_generation;
        }

        template <typename Func>
        decltype(auto) visitComponentSlot(Row& row, const ComponentTypeId componentTypeId, Func&& func)
        {
            void* result = nullptr;
            size_t index = 0;

            auto select = [&]<typename Component>()
            {
                using Value = component_value_t<Component>;
                if (componentTypeId == type_id<Component>() && row.present[index])
                    result = &std::get<Value>(row.components);
                ++index;
            };

            (select.template operator()<Components>(), ...);
            return std::forward<Func>(func)(result);
        }

        template <typename Func>
        decltype(auto) visitComponentSlot(const Row& row, const ComponentTypeId componentTypeId, Func&& func) const
        {
            const void* result = nullptr;
            size_t index = 0;

            auto select = [&]<typename Component>()
            {
                using Value = component_value_t<Component>;
                if (componentTypeId == type_id<Component>() && row.present[index])
                    result = &std::get<Value>(row.components);
                ++index;
            };

            (select.template operator()<Components>(), ...);
            return std::forward<Func>(func)(result);
        }

        void* componentPointerFromRow(Row& row, const ComponentTypeId componentTypeId)
        {
            return visitComponentSlot(row, componentTypeId, [](void* component) { return component; });
        }

        const void* componentPointerFromRow(const Row& row, const ComponentTypeId componentTypeId) const
        {
            return visitComponentSlot(row, componentTypeId, [](const void* component) { return component; });
        }

    public:
        using value_type = tuple_type;

        static_assert(
            (... && std::is_default_constructible_v<component_value_t<Components>>),
            "ArchetypePool components must be default constructible so tuple rows can be created incrementally"
        );

        static_assert(
            (... && !ecs::is_buffered_component_v<component_value_t<Components>>),
            "ArchetypePool currently supports direct components only; buffered archetype pools need a dedicated buffering policy"
        );

        const std::vector<ComponentTypeId>& componentTypes() const override
        { return m_componentTypes; }

        size_t size() const override
        { return m_storage.size(); }

        size_t componentSize(const ComponentTypeId componentTypeId) const override
        {
            const size_t index = componentIndex(componentTypeId);
            return N_POS == index ? 0 : m_presentCounts[index];
        }

        size_t componentEntityAt(
            const ComponentTypeId componentTypeId,
            const size_t componentDenseIndex
        ) const override {
            const size_t index = componentIndex(componentTypeId);
            if (N_POS == index || componentDenseIndex >= m_presentEntities[index].length())
                throw std::out_of_range("Archetype component dense index out of range");

            return m_presentEntities[index][componentDenseIndex];
        }

        size_t generation() const override
        { return m_generation; }

        bool containsEntity(const size_t entityIndex) const override
        { return nullptr != m_storage.try_get(entityIndex); }

        bool hasComponent(const size_t entityIndex, const ComponentTypeId componentTypeId) const override
        {
            const Row* row = m_storage.try_get(entityIndex);
            if (nullptr == row)
                return false;

            const size_t index = componentIndex(componentTypeId);
            return N_POS != index && row->present[index];
        }

        size_t denseIndexOf(const size_t entityIndex) const override
        { return m_storage.dense_index_of(entityIndex); }

        size_t entityAt(const size_t denseIndex) const override
        { return m_storage.entity_at(denseIndex); }

        void ensureEntity(const size_t entityIndex) override
        { (void)ensureRow(entityIndex); }

        void* ensureComponentPointer(const size_t entityIndex, const ComponentTypeId componentTypeId) override
        {
            const size_t index = componentIndex(componentTypeId);
            if (N_POS == index)
                return nullptr;

            Row& row = ensureRow(entityIndex);
            if (!row.present[index])
            {
                row.present[index] = true;
                ++m_presentCounts[index];
                m_presentEntities[index].append(entityIndex);
                ++m_generation;
            }

            return componentPointerFromRow(row, componentTypeId);
        }

        template <typename Component, typename Value>
        component_value_t<Component>& assignComponent(const size_t entityIndex, Value&& value)
        {
            using ComponentValue = component_value_t<Component>;
            void* component = ensureComponentPointer(entityIndex, type_id<ComponentValue>());
            if (nullptr == component)
                throw std::runtime_error("Archetype pool does not contain requested component");

            ComponentValue& stored = *static_cast<ComponentValue*>(component);
            stored = std::forward<Value>(value);
            markComponentEntityDirty(type_id<ComponentValue>(), entityIndex);
            return stored;
        }

        template <typename Component>
        Component* try_get_component(const size_t entityIndex)
        {
            void* component = componentPointer(entityIndex, type_id<Component>());
            return static_cast<Component*>(component);
        }

        template <typename Component>
        const Component* try_get_component(const size_t entityIndex) const
        {
            const void* component = componentPointer(entityIndex, type_id<Component>());
            return static_cast<const Component*>(component);
        }

        bool removeComponent(const size_t entityIndex, const ComponentTypeId componentTypeId) override
        {
            Row* row = m_storage.try_get(entityIndex);
            if (nullptr == row)
                return false;

            const size_t index = componentIndex(componentTypeId);
            if (N_POS == index || !row->present[index])
                return false;

            row->present[index] = false;
            --m_presentCounts[index];
            m_presentEntities[index].remove(entityIndex);
            ++m_generation;
            markComponentEntityDirtyByIndex(index, entityIndex);
            eraseRowIfEmpty(entityIndex, *row);
            return true;
        }

        void clearComponentStorage(const ComponentTypeId componentTypeId) override
        {
            const size_t index = componentIndex(componentTypeId);
            if (N_POS == index || 0 == m_presentCounts[index])
                return;

            ArrayList<size_t> entities = m_presentEntities[index];
            for (const size_t entityIndex : entities)
                removeComponent(entityIndex, componentTypeId);
        }

        void eraseEntity(const size_t entityIndex) override
        {
            Row* row = m_storage.try_get(entityIndex);
            if (nullptr == row)
                return;

            for (size_t i = 0; i < COMPONENT_COUNT; ++i)
            {
                if (!row->present[i])
                    continue;

                row->present[i] = false;
                --m_presentCounts[i];
                m_presentEntities[i].remove(entityIndex);
                markComponentEntityDirtyByIndex(i, entityIndex);
            }

            m_storage.erase(entityIndex);
            ++m_generation;
        }

        void clear() override
        {
            if (m_storage.empty())
                return;

            {
                std::lock_guard lock(m_dirtyMutex);
                for (size_t i = 0; i < COMPONENT_COUNT; ++i)
                {
                    if (m_presentCounts[i] > 0)
                        m_dirty[i].markFull();
                    m_presentCounts[i] = 0;
                    m_presentEntities[i].clear();
                }
            }

            m_storage.clear();
            ++m_generation;
        }

        void* componentPointer(const size_t entityIndex, const ComponentTypeId componentTypeId) override
        {
            Row* row = m_storage.try_get(entityIndex);
            return nullptr == row ? nullptr : componentPointerFromRow(*row, componentTypeId);
        }

        const void* componentPointer(const size_t entityIndex, const ComponentTypeId componentTypeId) const override
        {
            const Row* row = m_storage.try_get(entityIndex);
            return nullptr == row ? nullptr : componentPointerFromRow(*row, componentTypeId);
        }

        void* denseComponentPointer(const size_t denseIndex, const ComponentTypeId componentTypeId) override
        {
            if (denseIndex >= m_storage.size())
                return nullptr;

            return componentPointerFromRow(m_storage.dense_at(denseIndex), componentTypeId);
        }

        const void* componentPointerAt(
            const ComponentTypeId componentTypeId,
            const size_t componentDenseIndex
        ) const override {
            const size_t index = componentIndex(componentTypeId);
            if (N_POS == index || componentDenseIndex >= m_presentEntities[index].length())
                return nullptr;

            const Row* row = m_storage.try_get(m_presentEntities[index][componentDenseIndex]);
            if (nullptr == row || !row->present[index])
                return nullptr;

            return componentPointerFromRow(*row, componentTypeId);
        }

        bool componentPointers(
            const size_t entityIndex,
            const ComponentTypeId* componentTypeIds,
            void** destinations,
            const size_t count
        ) override {
            Row* row = m_storage.try_get(entityIndex);
            if (nullptr == row)
                return false;

            return denseComponentPointers(m_storage.dense_index_of(entityIndex), componentTypeIds, destinations, count);
        }

        bool denseComponentPointers(
            const size_t denseIndex,
            const ComponentTypeId* componentTypeIds,
            void** destinations,
            const size_t count
        ) override {
            if (denseIndex >= m_storage.size())
                return false;

            Row& row = m_storage.dense_at(denseIndex);
            for (size_t i = 0; i < count; ++i)
            {
                destinations[i] = componentPointerFromRow(row, componentTypeIds[i]);
                if (nullptr == destinations[i])
                    return false;
            }

            return true;
        }

        bool copyComponentTo(
            const size_t entityIndex,
            const ComponentTypeId componentTypeId,
            void* destination
        ) const override {
            if (nullptr == destination)
                return false;

            const Row* row = m_storage.try_get(entityIndex);
            if (nullptr == row)
                return false;

            bool copied = false;
            size_t index = 0;
            auto copy = [&]<typename Component>()
            {
                using Value = component_value_t<Component>;
                if (componentTypeId == type_id<Component>() && row->present[index])
                {
                    *static_cast<Value*>(destination) = std::get<Value>(row->components);
                    copied = true;
                }
                ++index;
            };

            (copy.template operator()<Components>(), ...);
            return copied;
        }

        void copyRowsInto(IArchetypePool& destination) const override
        {
            for (size_t denseIndex = 0; denseIndex < m_storage.size(); ++denseIndex)
            {
                const size_t entityIndex = m_storage.entity_at(denseIndex);
                const Row& row = m_storage.dense_at(denseIndex);

                for (size_t componentIndex = 0; componentIndex < COMPONENT_COUNT; ++componentIndex)
                {
                    if (!row.present[componentIndex])
                        continue;

                    const ComponentTypeId componentTypeId = m_componentTypes[componentIndex];
                    void* component = destination.ensureComponentPointer(entityIndex, componentTypeId);
                    if (nullptr == component || !copyComponentTo(entityIndex, componentTypeId, component))
                        throw std::runtime_error("Failed to merge archetype component storage");

                    destination.markComponentEntityDirty(componentTypeId, entityIndex);
                }
            }
        }

        bool isComponentDirty(const ComponentTypeId componentTypeId) const override
        {
            const size_t index = componentIndex(componentTypeId);
            return N_POS != index && m_dirty[index].dirty();
        }

        bool isComponentFullyDirty(const ComponentTypeId componentTypeId) const override
        {
            const size_t index = componentIndex(componentTypeId);
            return N_POS != index && m_dirty[index].fullyDirty();
        }

        const ArrayList<size_t>& componentDirtyEntities(const ComponentTypeId componentTypeId) const override
        {
            const size_t index = componentIndex(componentTypeId);
            if (N_POS == index)
                throw std::out_of_range("Archetype pool does not contain requested component");

            return m_dirty[index].entities();
        }

        void markComponentDirty(const ComponentTypeId componentTypeId) override
        {
            const size_t index = componentIndex(componentTypeId);
            if (N_POS != index)
                markComponentDirtyByIndex(index);
        }

        void markComponentEntityDirty(const ComponentTypeId componentTypeId, const size_t entityIndex) override
        {
            const size_t index = componentIndex(componentTypeId);
            if (N_POS != index)
                markComponentEntityDirtyByIndex(index, entityIndex);
        }

        bool markComponentDirtyIfEntityCountReachesThreshold(
            const ComponentTypeId componentTypeId,
            const size_t entityCount
        ) override {
            const size_t index = componentIndex(componentTypeId);
            if (N_POS == index)
                return false;

            std::lock_guard lock(m_dirtyMutex);
            if (!m_dirty[index].wouldMarkFullAfterAddingEntities(m_presentCounts[index], entityCount))
                return false;

            m_dirty[index].markFull();
            return true;
        }

        void clearComponentDirty(const ComponentTypeId componentTypeId) override
        {
            const size_t index = componentIndex(componentTypeId);
            if (N_POS == index)
                return;

            std::lock_guard lock(m_dirtyMutex);
            m_dirty[index].clear();
        }
    };

    class ArchetypeStorageRegistry
    {
        std::vector<std::unique_ptr<IArchetypePool>> m_pools;
        std::unordered_map<ComponentTypeId, size_t> m_componentToPool;
        size_t m_generation = 0;

        void rebuildComponentLookup()
        {
            m_componentToPool.clear();
            for (size_t poolIndex = 0; poolIndex < m_pools.size(); ++poolIndex)
            {
                for (const ComponentTypeId componentTypeId : m_pools[poolIndex]->componentTypes())
                    m_componentToPool[componentTypeId] = poolIndex;
            }
        }

    public:
        size_t generation() const
        { return m_generation; }

        size_t poolCount() const
        { return m_pools.size(); }

        IArchetypePool* poolAt(const size_t poolIndex)
        { return m_pools[poolIndex].get(); }

        const IArchetypePool* poolAt(const size_t poolIndex) const
        { return m_pools[poolIndex].get(); }

        template <typename Component>
        bool containsComponent() const
        {
            const ComponentTypeId componentTypeId = component_type_id<component_key_t<Component>>();
            return m_componentToPool.find(componentTypeId) != m_componentToPool.end();
        }

        IArchetypePool* poolForComponent(const ComponentTypeId componentTypeId)
        {
            const auto it = m_componentToPool.find(componentTypeId);
            if (it == m_componentToPool.end())
                return nullptr;

            return m_pools[it->second].get();
        }

        const IArchetypePool* poolForComponent(const ComponentTypeId componentTypeId) const
        {
            const auto it = m_componentToPool.find(componentTypeId);
            if (it == m_componentToPool.end())
                return nullptr;

            return m_pools[it->second].get();
        }

        template <typename Component>
        IArchetypePool* poolForComponent()
        { return poolForComponent(component_type_id<component_key_t<Component>>()); }

        template <typename Component>
        const IArchetypePool* poolForComponent() const
        { return poolForComponent(component_type_id<component_key_t<Component>>()); }

        void eraseEntityFromAll(const size_t entityIndex)
        {
            for (const auto& pool : m_pools)
                pool->eraseEntity(entityIndex);
        }

        void clearPools()
        {
            for (const auto& pool : m_pools)
                pool->clear();
        }

        template <typename... Components>
        ArchetypePool<Components...>& registerArchetype()
        {
            static_assert(sizeof...(Components) > 1, "Archetype groups must contain at least two components");

            using pool_type = ArchetypePool<Components...>;
            const std::vector<ComponentTypeId> componentTypeIds =
                archetype_detail::component_ids<Components...>();

            if (componentTypeIds.size() != sizeof...(Components))
                throw std::invalid_argument("Archetype groups cannot contain duplicate component types");

            std::vector<size_t> overlaps;
            for (size_t poolIndex = 0; poolIndex < m_pools.size(); ++poolIndex)
            {
                if (m_pools[poolIndex]->overlaps(componentTypeIds))
                    overlaps.push_back(poolIndex);
            }

            if (overlaps.size() == 1
                && archetype_detail::same_component_set(m_pools[overlaps[0]]->componentTypes(), componentTypeIds))
            {
                if (auto* existing = dynamic_cast<pool_type*>(m_pools[overlaps[0]].get()))
                    return *existing;
            }

            for (const size_t poolIndex : overlaps)
            {
                if (!archetype_detail::is_superset(componentTypeIds, m_pools[poolIndex]->componentTypes()))
                {
                    throw std::invalid_argument(
                        "Overlapping archetype registration must name the full merged component set"
                    );
                }
            }

            auto mergedPool = std::make_unique<pool_type>();
            for (const size_t poolIndex : overlaps)
                m_pools[poolIndex]->copyRowsInto(*mergedPool);

            std::ranges::sort(overlaps, std::greater<>());
            for (const size_t poolIndex : overlaps)
                m_pools.erase(m_pools.begin() + static_cast<std::ptrdiff_t>(poolIndex));

            m_pools.push_back(std::move(mergedPool));
            ++m_generation;
            rebuildComponentLookup();
            return *static_cast<pool_type*>(m_pools.back().get());
        }

        template <typename... Components>
        ArchetypePool<Components...>* poolIfExists()
        {
            const std::vector<ComponentTypeId> componentTypeIds =
                archetype_detail::component_ids<Components...>();

            for (const auto& pool : m_pools)
            {
                if (archetype_detail::same_component_set(pool->componentTypes(), componentTypeIds))
                    return dynamic_cast<ArchetypePool<Components...>*>(pool.get());
            }

            return nullptr;
        }
    };
}
