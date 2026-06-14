//
// Created by Nicholas on 06/05/26.
//

#pragma once

#include <algorithm>
#include <array>
#include <exception>
#include <iterator>
#include <functional>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include "../core/archetype_storage_registry.h"
#include "../pools/component_pool.h"
#include "view_cache.h"
#include "view_execution.h"
#include "view_matcher.h"

namespace ecs_view_detail
{
    struct NoSingleViewValue {};

    template <typename... Components>
    struct single_view_value
    {
        using type = NoSingleViewValue;
    };

    template <typename Component>
    struct single_view_value<Component>
    {
        using type = ecs::component_value_t<Component>;
    };

    template <typename... Components>
    using single_view_value_t = typename single_view_value<Components...>::type;
}

template <typename... Components>
class View
{
    template <typename Component>
    using sim_pool_t = SimulationComponentPoolFor<ecs::component_value_t<Component>>;

    using sim_pool_tuple = std::tuple<sim_pool_t<Components>*...>;
    using render_pool_tuple = std::tuple<RenderComponentPool<ecs::component_value_t<Components>>*...>;

    ECS* m_ecs = nullptr;
    sim_pool_tuple m_pools {};
    render_pool_tuple m_renderPools {};
    std::array<ecs::IArchetypePool*, sizeof...(Components)> m_archetypePools {};
    std::array<ecs::ComponentTypeId, sizeof...(Components)> m_componentTypeIds {};
    bool m_usesArchetypeSources = false;
    bool m_singleArchetypeSource = false;
    size_t m_primaryIndex = Entity::N_POS;
    bool m_cacheResolved = false;
    bool m_trackDirtyWrites = true;
    ViewStorage m_storage = ViewStorage::Simulation;

    template <typename>
    static constexpr bool always_false_v = false;

    using component_tuple = std::tuple<Components...>;
    using single_iteration_value_t = ecs_view_detail::single_view_value_t<Components...>;

    template <size_t I>
    using component_at_t = std::tuple_element_t<I, component_tuple>;

    template <size_t I>
    using value_at_t = ecs::component_value_t<component_at_t<I>>;

    using dense_range = ecs_view_detail::DenseRange;
    using view_match = ecs_view_detail::ViewMatch<sizeof...(Components)>;

public:
    class const_iterator
    {
        const View* m_view = nullptr;
        size_t m_index = 0;

    public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = single_iteration_value_t;
        using reference = const value_type&;
        using pointer = const value_type*;

        const_iterator() = default;

        const_iterator(const View* view, const size_t index)
            : m_view(view),
              m_index(index)
        {}

        reference operator*() const
        {
            const pointer component = m_view->single_component_at_iteration(m_index);
            if (nullptr == component)
                throw std::out_of_range("View iterator dereferenced an invalid component index");

            return *component;
        }

        pointer operator->() const
        { return &operator*(); }

        const_iterator& operator++()
        {
            ++m_index;
            return *this;
        }

        const_iterator operator++(int)
        {
            const_iterator previous = *this;
            ++(*this);
            return previous;
        }

        bool operator==(const const_iterator& rhs) const
        { return m_view == rhs.m_view && m_index == rhs.m_index; }

        bool operator!=(const const_iterator& rhs) const
        { return !(*this == rhs); }
    };

private:
    ArrayList<view_match> m_matches;
    bool m_matchesResolved = false;
    static constexpr size_t UNOBSERVED_GENERATION = static_cast<size_t>(-1);
    std::array<size_t, sizeof...(Components)> m_observedPoolGenerations {};
    size_t m_observedEntityGeneration = UNOBSERVED_GENERATION;
    size_t m_observedStorageGeneration = UNOBSERVED_GENERATION;

    bool is_rendering_storage() const
    { return ViewStorage::Rendering == m_storage; }

    size_t current_storage_generation() const
    {
        return is_rendering_storage()
            ? m_ecs->render_storage_generation()
            : m_ecs->component_storage_generation();
    }

    template <typename PoolTuple, size_t... Is>
    std::array<size_t, sizeof...(Components)> current_pool_generations(
        const PoolTuple& pools,
        std::index_sequence<Is...>
    ) const {
        return ecs_view_detail::pool_generations(pools, std::index_sequence<Is...>{});
    }

    template <size_t I>
    size_t current_source_generation() const
    {
        if (nullptr != m_archetypePools[I])
            return m_archetypePools[I]->generation();

        if (is_rendering_storage())
        {
            auto* pool = std::get<I>(m_renderPools);
            return nullptr == pool ? 0 : pool->generation();
        }

        auto* pool = std::get<I>(m_pools);
        return nullptr == pool ? 0 : pool->generation();
    }

    template <size_t... Is>
    std::array<size_t, sizeof...(Components)> current_source_generations(std::index_sequence<Is...>) const
    {
        return { current_source_generation<Is>()... };
    }

    std::array<size_t, sizeof...(Components)> current_pool_generations() const
    {
        if constexpr (0 == sizeof...(Components))
            return {};
        else if (m_usesArchetypeSources)
            return current_source_generations(std::make_index_sequence<sizeof...(Components)>{});
        else if (is_rendering_storage())
            return current_pool_generations(m_renderPools, std::make_index_sequence<sizeof...(Components)>{});
        else
            return current_pool_generations(m_pools, std::make_index_sequence<sizeof...(Components)>{});
    }

    bool cache_current() const
    {
        if (!m_cacheResolved || nullptr == m_ecs)
            return false;

        return m_observedEntityGeneration == m_ecs->entity_generation()
            && m_observedStorageGeneration == current_storage_generation()
            && m_observedPoolGenerations == current_pool_generations();
    }

    void capture_cache_generations()
    {
        m_observedEntityGeneration = m_ecs->entity_generation();
        m_observedStorageGeneration = current_storage_generation();
        m_observedPoolGenerations = current_pool_generations();
    }

    template <typename PoolTuple>
    size_t select_primary_storage(const PoolTuple& pools) const
    {
        return ecs_view_detail::select_primary_storage<sizeof...(Components)>(pools);
    }

    template <size_t I>
    bool source_available() const
    {
        if (nullptr != m_archetypePools[I])
            return true;

        return is_rendering_storage()
            ? nullptr != std::get<I>(m_renderPools)
            : nullptr != std::get<I>(m_pools);
    }

    template <size_t I>
    size_t source_size() const
    {
        if (nullptr != m_archetypePools[I])
            return m_archetypePools[I]->componentSize(m_componentTypeIds[I]);

        if (is_rendering_storage())
        {
            auto* pool = std::get<I>(m_renderPools);
            return nullptr == pool ? 0 : pool->size();
        }

        auto* pool = std::get<I>(m_pools);
        return nullptr == pool ? 0 : pool->size();
    }

    template <size_t... Is>
    size_t select_primary_source(std::index_sequence<Is...>) const
    {
        std::array<size_t, sizeof...(Components)> sizes { source_size<Is>()... };
        if (std::any_of(sizes.begin(), sizes.end(), [](const size_t size) { return 0 == size; }))
            return Entity::N_POS;

        const auto smallest = std::min_element(sizes.begin(), sizes.end());
        return static_cast<size_t>(std::distance(sizes.begin(), smallest));
    }

    bool all_components_in_single_archetype_source() const
    {
        if constexpr (sizeof...(Components) <= 1)
        {
            return false;
        }
        else
        {
            ecs::IArchetypePool* first = nullptr;
            for (ecs::IArchetypePool* pool : m_archetypePools)
            {
                if (nullptr == pool)
                    return false;

                if (nullptr == first)
                {
                    first = pool;
                    continue;
                }

                if (first != pool)
                    return false;
            }

            return nullptr != first;
        }
    }

    size_t primary_archetype_component_size() const
    {
        if (Entity::N_POS == m_primaryIndex)
            return 0;

        ecs::IArchetypePool* primary = m_archetypePools[m_primaryIndex];
        return nullptr == primary ? 0 : primary->componentSize(m_componentTypeIds[m_primaryIndex]);
    }

    template <size_t... Is>
    bool single_archetype_entity_matches(
        ecs::IArchetypePool& pool,
        const size_t entityIndex,
        std::index_sequence<Is...>
    ) const {
        return (pool.hasComponent(entityIndex, m_componentTypeIds[Is]) && ...);
    }

    size_t single_archetype_match_count() const
    {
        if (!m_singleArchetypeSource || Entity::N_POS == m_primaryIndex)
            return 0;

        ecs::IArchetypePool* primary = m_archetypePools[m_primaryIndex];
        if (nullptr == primary)
            return 0;

        size_t count = 0;
        const ecs::ComponentTypeId primaryTypeId = m_componentTypeIds[m_primaryIndex];
        const size_t primarySize = primary->componentSize(primaryTypeId);
        for (size_t componentDenseIndex = 0; componentDenseIndex < primarySize; ++componentDenseIndex)
        {
            const size_t entityIndex = primary->componentEntityAt(primaryTypeId, componentDenseIndex);
            if (!m_ecs->is_alive_index(entityIndex))
                continue;

            if (single_archetype_entity_matches(
                *primary,
                entityIndex,
                std::make_index_sequence<sizeof...(Components)>{}
            )) {
                ++count;
            }
        }

        return count;
    }

    template <size_t I, size_t PrimaryI>
    bool same_sim_source_as_primary() const
    {
        if (nullptr != m_archetypePools[I] || nullptr != m_archetypePools[PrimaryI])
            return nullptr != m_archetypePools[I] && m_archetypePools[I] == m_archetypePools[PrimaryI];

        return I == PrimaryI;
    }

    template <size_t I, size_t PrimaryI, typename PoolTuple>
    bool fill_match_dense_index(
        PoolTuple& pools,
        const size_t entityIndex,
        const size_t primaryDenseIndex,
        view_match& match
    ) const
    {
        if (m_usesArchetypeSources)
        {
            if (same_sim_source_as_primary<I, PrimaryI>())
            {
                if (nullptr != m_archetypePools[I]
                    && !m_archetypePools[I]->hasComponent(entityIndex, m_componentTypeIds[I]))
                {
                    return false;
                }

                match.denseIndices[I] = primaryDenseIndex;
                return true;
            }

            if (nullptr != m_archetypePools[I])
            {
                if (!m_archetypePools[I]->hasComponent(entityIndex, m_componentTypeIds[I]))
                    return false;

                const size_t denseIndex = m_archetypePools[I]->denseIndexOf(entityIndex);
                if (SparseSet<value_at_t<I>>::N_POS == denseIndex)
                    return false;

                match.denseIndices[I] = denseIndex;
                return true;
            }
        }

        if constexpr (I == PrimaryI)
        {
            match.denseIndices[I] = primaryDenseIndex;
            return true;
        }
        else
        {
            auto* pool = std::get<I>(pools);
            if (nullptr == pool)
                return false;

            const size_t denseIndex = pool->dense_index_of(entityIndex);
            if (SparseSet<value_at_t<I>>::N_POS == denseIndex)
                return false;

            match.denseIndices[I] = denseIndex;
            return true;
        }
    }

    template <size_t PrimaryI, typename PoolTuple, size_t... Is>
    bool fill_match(
        PoolTuple& pools,
        const size_t entityIndex,
        const size_t primaryDenseIndex,
        view_match& match,
        std::index_sequence<Is...>
    ) const
    {
        return (fill_match_dense_index<Is, PrimaryI>(
            pools,
            entityIndex,
            primaryDenseIndex,
            match
        ) && ...);
    }

    template <size_t PrimaryI, typename PoolTuple, typename PoolT>
    void append_matches_from_primary(PoolTuple& pools, PoolT* primary)
    {
        if (nullptr == primary)
            return;

        for (size_t denseIndex = 0; denseIndex < primary->size(); ++denseIndex)
        {
            const size_t entityIndex = primary->entity_at(denseIndex);
            if (!m_ecs->is_alive_index(entityIndex))
                continue;

            view_match match;
            match.entityIndex = entityIndex;
            if (fill_match<PrimaryI>(
                pools,
                entityIndex,
                denseIndex,
                match,
                std::make_index_sequence<sizeof...(Components)>{}
            )) {
                m_matches.append(std::move(match));
            }
        }
    }

    template <typename PoolTuple, size_t... Is>
    void resolve_matches_from_pools(PoolTuple& pools, std::index_sequence<Is...>)
    {
        bool handled = false;
        size_t currentIndex = 0;

        auto dispatch = [&](auto index, auto* primary)
        {
            constexpr size_t I = decltype(index)::value;

            if (handled || currentIndex != m_primaryIndex)
            {
                ++currentIndex;
                return;
            }

            handled = true;
            append_matches_from_primary<I>(pools, primary);
            ++currentIndex;
        };

        (dispatch(std::integral_constant<size_t, Is>{}, std::get<Is>(pools)), ...);
    }

    template <size_t PrimaryI, typename PoolTuple>
    void append_matches_from_archetype_primary(PoolTuple& pools)
    {
        ecs::IArchetypePool* primary = m_archetypePools[PrimaryI];
        if (nullptr == primary)
            return;

        const ecs::ComponentTypeId primaryTypeId = m_componentTypeIds[PrimaryI];
        const size_t primarySize = primary->componentSize(primaryTypeId);
        for (size_t componentDenseIndex = 0; componentDenseIndex < primarySize; ++componentDenseIndex)
        {
            const size_t entityIndex = primary->componentEntityAt(primaryTypeId, componentDenseIndex);
            if (!m_ecs->is_alive_index(entityIndex))
                continue;

            const size_t rowDenseIndex = primary->denseIndexOf(entityIndex);
            if (SparseSet<value_at_t<PrimaryI>>::N_POS == rowDenseIndex)
                continue;

            view_match match;
            match.entityIndex = entityIndex;
            if (fill_match<PrimaryI>(
                pools,
                entityIndex,
                rowDenseIndex,
                match,
                std::make_index_sequence<sizeof...(Components)>{}
            )) {
                m_matches.append(std::move(match));
            }
        }
    }

    template <typename PoolTuple, size_t... Is>
    void resolve_matches_from_sources(PoolTuple& pools, std::index_sequence<Is...>)
    {
        bool handled = false;
        size_t currentIndex = 0;

        auto dispatch = [&](auto index)
        {
            constexpr size_t I = decltype(index)::value;

            if (handled || currentIndex != m_primaryIndex)
            {
                ++currentIndex;
                return;
            }

            handled = true;
            if (nullptr != m_archetypePools[I])
                append_matches_from_archetype_primary<I>(pools);
            else
                append_matches_from_primary<I>(pools, std::get<I>(pools));

            ++currentIndex;
        };

        (dispatch(std::integral_constant<size_t, Is>{}), ...);
    }

    void resolve_matches()
    {
        m_matches.clear();

        if constexpr (sizeof...(Components) > 0)
        {
            if (m_usesArchetypeSources && Entity::N_POS != m_primaryIndex)
            {
                if (is_rendering_storage())
                    resolve_matches_from_sources(m_renderPools, std::make_index_sequence<sizeof...(Components)>{});
                else
                    resolve_matches_from_sources(m_pools, std::make_index_sequence<sizeof...(Components)>{});
            }
            else if constexpr (sizeof...(Components) > 1)
            {
                if (Entity::N_POS != m_primaryIndex)
                {
                    if (is_rendering_storage())
                    {
                        resolve_matches_from_pools(m_renderPools, std::make_index_sequence<sizeof...(Components)>{});
                    }
                    else
                    {
                        resolve_matches_from_pools(m_pools, std::make_index_sequence<sizeof...(Components)>{});
                    }
                }
            }
        }

        m_matchesResolved = true;
    }

    void resolve_cache()
    {
        m_matchesResolved = false;
        m_usesArchetypeSources = false;
        m_singleArchetypeSource = false;
        m_archetypePools = {};

        if constexpr (0 == sizeof...(Components))
        {
            m_primaryIndex = 0;
        }
        else
        {
            if (is_rendering_storage())
            {
                m_componentTypeIds = {
                    ecs::component_type_id<ecs::component_key_t<Components>>()...
                };
                m_renderPools = render_pool_tuple{
                    m_ecs->render_storage_if_exists<Components>()...
                };
                m_archetypePools = {
                    m_ecs->template mutable_render_archetype_pool_for_component<Components>()...
                };
                m_usesArchetypeSources = std::any_of(
                    m_archetypePools.begin(),
                    m_archetypePools.end(),
                    [](const ecs::IArchetypePool* pool) { return nullptr != pool; }
                );
                m_singleArchetypeSource = all_components_in_single_archetype_source();
                m_primaryIndex = m_usesArchetypeSources
                    ? select_primary_source(std::make_index_sequence<sizeof...(Components)>{})
                    : select_primary_storage(m_renderPools);
            }
            else
            {
                m_componentTypeIds = {
                    ecs::component_type_id<ecs::component_key_t<Components>>()...
                };
                m_pools = sim_pool_tuple{
                    m_ecs->storage_if_exists<Components>()...
                };
                m_archetypePools = {
                    m_ecs->template archetypePoolForComponent<Components>()...
                };
                m_usesArchetypeSources = std::any_of(
                    m_archetypePools.begin(),
                    m_archetypePools.end(),
                    [](const ecs::IArchetypePool* pool) { return nullptr != pool; }
                );
                m_singleArchetypeSource = all_components_in_single_archetype_source();
                m_primaryIndex = m_usesArchetypeSources
                    ? select_primary_source(std::make_index_sequence<sizeof...(Components)>{})
                    : select_primary_storage(m_pools);
            }
        }

        m_cacheResolved = true;
        capture_cache_generations();
    }

    bool ensure_cache()
    {
        if (nullptr == m_ecs)
            return false;

        if (!m_cacheResolved || !cache_current())
            resolve_cache();

        if constexpr (0 == sizeof...(Components))
        {
            return true;
        }
        else
        {
            return m_primaryIndex != Entity::N_POS;
        }
    }

    bool ensure_matches()
    {
        if (!ensure_cache())
            return false;

        if (m_singleArchetypeSource)
            return true;

        if (!m_matchesResolved)
            resolve_matches();

        return true;
    }

    size_t alive_entity_count() const
    { return m_ecs->alive_entity_count(); }

    template <typename Func>
    void invoke_entity_only_callback(const Entity& entity, Func& func)
    {
        if constexpr (std::is_invocable_v<Func&, Entity>)
        {
            func(entity);
        }
        else if constexpr (std::is_invocable_v<Func&>)
        {
            func();
        }
        else
        {
            static_assert(
                always_false_v<Func>,
                "Entity-only view callback must accept `(Entity)` or `()`."
            );
        }
    }

    template <typename Func>
    void iterate_all_entities(Func& func)
    {
        const ArrayList<EntityRecord>& records = m_ecs->entity_records();
        for (size_t i = 0; i < records.length(); ++i)
        {
            const EntityRecord& record = records[i];
            if (!record.alive)
                continue;

            invoke_entity_only_callback(m_ecs->make_handle(record, i), func);
        }
    }

    template <typename PoolTuple>
    size_t primary_size_runtime(const PoolTuple& pools) const
    {
        if constexpr (0 == sizeof...(Components))
            return alive_entity_count();

        size_t currentIndex = 0;
        size_t primarySize = 0;
        bool hit = false;

        std::apply(
            [&](auto*... pools)
            {
                auto select = [&](auto* pool)
                {
                    if (hit || currentIndex != m_primaryIndex)
                    {
                        ++currentIndex;
                        return;
                    }

                    hit = true;
                    primarySize = nullptr == pool ? 0 : pool->size();
                    ++currentIndex;
                };

                (select(pools), ...);
            },
            pools
        );

        return primarySize;
    }

    size_t primary_size_runtime() const
    {
        if constexpr (0 == sizeof...(Components))
            return alive_entity_count();
        else if (is_rendering_storage())
            return primary_size_runtime(m_renderPools);
        else
            return primary_size_runtime(m_pools);
    }

    size_t iteration_size_runtime()
    {
        if (m_usesArchetypeSources)
        {
            if (m_singleArchetypeSource)
                return primary_archetype_component_size();

            ensure_matches();
            return m_matches.length();
        }

        if constexpr (sizeof...(Components) <= 1)
            return primary_size_runtime();
        else
        {
            ensure_matches();
            return m_matches.length();
        }
    }

    const single_iteration_value_t* single_component_at_iteration(const size_t iterationIndex) const
    {
        if constexpr (sizeof...(Components) != 1)
        {
            (void)iterationIndex;
            return nullptr;
        }
        else
        {

            auto* self = const_cast<View*>(this);
            if (!self->ensure_matches())
                return nullptr;

            if (m_usesArchetypeSources)
            {
                if (iterationIndex >= m_matches.length())
                    return nullptr;

                const view_match& match = m_matches[iterationIndex];
                if (nullptr != m_archetypePools[0])
                {
                    void* component = m_archetypePools[0]->denseComponentPointer(
                        match.denseIndices[0],
                        m_componentTypeIds[0]
                    );
                    return static_cast<const value_at_t<0>*>(component);
                }

                if (is_rendering_storage())
                    return &std::get<0>(m_renderPools)->dense_at(match.denseIndices[0]);

                return &std::get<0>(m_pools)->dense_at(match.denseIndices[0]);
            }

            if (is_rendering_storage())
            {
                auto* renderPool = std::get<0>(m_renderPools);
                if (nullptr == renderPool || iterationIndex >= renderPool->size())
                    return nullptr;

                return &renderPool->dense_at(iterationIndex);
            }

            auto* pool = std::get<0>(m_pools);
            if (nullptr == pool || iterationIndex >= pool->size())
                return nullptr;

            return &pool->dense_at(iterationIndex);
        }
    }

    template <typename Func, size_t... Is>
    static constexpr bool accepts_sim_refs(std::index_sequence<Is...>)
    {
        return std::is_invocable_v<Func&, Entity, value_at_t<Is>&...>
            || std::is_invocable_v<Func&, value_at_t<Is>&...>;
    }

    template <typename Func, size_t... Is>
    static constexpr bool accepts_render_refs(std::index_sequence<Is...>)
    {
        return std::is_invocable_v<Func&, Entity, const value_at_t<Is>&...>
            || std::is_invocable_v<Func&, const value_at_t<Is>&...>;
    }

    template <typename Func>
    static constexpr bool accepts_sim_refs()
    {
        return accepts_sim_refs<Func>(std::make_index_sequence<sizeof...(Components)>{});
    }

    template <typename Func>
    static constexpr bool accepts_render_refs()
    {
        return accepts_render_refs<Func>(std::make_index_sequence<sizeof...(Components)>{});
    }

    template <typename Func, size_t... Is>
    static constexpr bool accepts_const_sim_refs(std::index_sequence<Is...>)
    {
        return std::is_invocable_v<Func&, Entity, const value_at_t<Is>&...>
            || std::is_invocable_v<Func&, const value_at_t<Is>&...>;
    }

    template <typename Func>
    static constexpr bool callback_requires_mutable_sim_refs()
    {
        if constexpr (0 == sizeof...(Components))
            return false;
        else
            return accepts_sim_refs<Func>()
                && !accepts_const_sim_refs<Func>(std::make_index_sequence<sizeof...(Components)>{});
    }

    template <typename Func, size_t... Is>
    void each_const_impl(Func&& func, std::index_sequence<Is...>) const
    {
        auto* self = const_cast<View*>(this);

        if constexpr (0 == sizeof...(Components))
        {
            self->each(std::forward<Func>(func));
        }
        else if constexpr (std::is_invocable_v<Func&, Entity, const value_at_t<Is>&...>)
        {
            auto wrapper = [&func](const Entity& entity, const value_at_t<Is>&... components)
            {
                std::invoke(func, entity, components...);
            };
            self->each(wrapper);
        }
        else if constexpr (std::is_invocable_v<Func&, const value_at_t<Is>&...>)
        {
            auto wrapper = [&func](const value_at_t<Is>&... components)
            {
                std::invoke(func, components...);
            };
            self->each(wrapper);
        }
        else
        {
            static_assert(
                always_false_v<Func>,
                "Const View::each callback must accept `(Entity, const Component&...)` or `(const Component&...)`."
            );
        }
    }

    template <typename Func, size_t... Is>
    void mark_sim_components_dirty_after_callback(
        const size_t entityIndex,
        std::index_sequence<Is...>
    ) {
        if (!m_trackDirtyWrites || is_rendering_storage())
            return;

        if constexpr (callback_requires_mutable_sim_refs<Func>())
            (m_ecs->template markComponentEntityDirty<value_at_t<Is>>(entityIndex), ...);
    }

    template <typename Func>
    void mark_sim_components_dirty_after_callback(const size_t entityIndex)
    {
        mark_sim_components_dirty_after_callback<Func>(
            entityIndex,
            std::make_index_sequence<sizeof...(Components)>{}
        );
    }

    template <typename Func, typename Tuple, size_t... Is>
    void invoke_component_callback(const size_t entityIndex, Func& func, Tuple& components, std::index_sequence<Is...>)
    {
        if constexpr (std::is_invocable_v<Func&, Entity, decltype(*std::get<Is>(components))...>)
        {
            func(m_ecs->make_handle(entityIndex), *std::get<Is>(components)...);
        }
        else if constexpr (std::is_invocable_v<Func&, decltype(*std::get<Is>(components))...>)
        {
            func(*std::get<Is>(components)...);
        }
        else
        {
            static_assert(
                always_false_v<Func>,
                "View::each callback must accept `(Entity, Component&...)`, `(Component&...)`, or const equivalents for render storage."
            );
        }
    }

    template <size_t I, size_t PrimaryI, typename PoolTuple, typename PrimaryComponent>
    auto component_ptr_from_primary(
        PoolTuple& pools,
        const size_t entityIndex,
        PrimaryComponent& primaryComponent
    )
    {
        if constexpr (I == PrimaryI)
            return &primaryComponent;
        else
            return std::get<I>(pools)->try_get(entityIndex);
    }

    template <typename Tuple, size_t... Is>
    bool all_components_present(const Tuple& components, std::index_sequence<Is...>) const
    { return ((std::get<Is>(components) != nullptr) && ...); }

    template <size_t PrimaryI, typename PoolTuple, typename PrimaryComponent, typename Func, size_t... Is>
    void visit_entity_from_primary(
        PoolTuple& pools,
        const size_t entityIndex,
        PrimaryComponent& primaryComponent,
        Func& func,
        std::index_sequence<Is...>
    ) {
        auto components = std::tuple{
            component_ptr_from_primary<Is, PrimaryI>(pools, entityIndex, primaryComponent)...
        };
        if (!all_components_present(components, std::make_index_sequence<sizeof...(Components)>{}))
            return;

        invoke_component_callback(entityIndex, func, components, std::make_index_sequence<sizeof...(Components)>{});
        mark_sim_components_dirty_after_callback<Func>(entityIndex);
    }

    template <size_t PrimaryI, typename PoolTuple, typename PoolT, typename Func>
    void iterate_primary_pool_range(
        PoolTuple& pools,
        PoolT* primary,
        const size_t beginDense,
        const size_t endDense,
        Func& func
    )
    {
        if (nullptr == primary)
            return;

        const size_t boundedEnd = std::min(endDense, primary->size());
        for (size_t denseIndex = beginDense; denseIndex < boundedEnd; ++denseIndex)
        {
            const size_t entityIndex = primary->entity_at(denseIndex);
            if (!m_ecs->is_alive_index(entityIndex))
                continue;

            auto& primaryComponent = primary->dense_at(denseIndex);
            this->visit_entity_from_primary<PrimaryI>(
                pools,
                entityIndex,
                primaryComponent,
                func,
                std::make_index_sequence<sizeof...(Components)>{}
            );
        }
    }

    template <typename PoolTuple, typename Func, size_t... Is>
    void each_with_primary_range_impl(
        PoolTuple& pools,
        const size_t beginDense,
        const size_t endDense,
        Func& func,
        std::index_sequence<Is...>
    ) {
        bool handled = false;
        size_t currentIndex = 0;

        auto dispatch = [&](auto index, auto* primary)
        {
            constexpr size_t I = decltype(index)::value;

            if (handled || currentIndex != m_primaryIndex)
            {
                ++currentIndex;
                return;
            }

            handled = true;
            iterate_primary_pool_range<I>(pools, primary, beginDense, endDense, func);
            ++currentIndex;
        };

        (dispatch(std::integral_constant<size_t, Is>{}, std::get<Is>(pools)), ...);
    }

    template <typename PoolTuple, typename Func, size_t... Is>
    void visit_match(PoolTuple& pools, const view_match& match, Func& func, std::index_sequence<Is...>)
    {
        auto components = std::tuple{
            &std::get<Is>(pools)->dense_at(match.denseIndices[Is])...
        };
        invoke_component_callback(match.entityIndex, func, components, std::make_index_sequence<sizeof...(Components)>{});
        mark_sim_components_dirty_after_callback<Func>(match.entityIndex);
    }

    template <typename PoolTuple, typename Func>
    void each_match_range(PoolTuple& pools, const size_t beginMatch, const size_t endMatch, Func& func)
    {
        const size_t boundedEnd = std::min(endMatch, m_matches.length());
        for (size_t matchIndex = beginMatch; matchIndex < boundedEnd; ++matchIndex)
        {
            visit_match(
                pools,
                m_matches[matchIndex],
                func,
                std::make_index_sequence<sizeof...(Components)>{}
            );
        }
    }

    void fill_archetype_raw_components(
        const size_t entityIndex,
        std::array<void*, sizeof...(Components)>& rawComponents
    ) {
        std::array<bool, sizeof...(Components)> handled {};

        for (size_t i = 0; i < m_archetypePools.size(); ++i)
        {
            ecs::IArchetypePool* archetypePool = m_archetypePools[i];
            if (nullptr == archetypePool || handled[i])
                continue;

            std::array<ecs::ComponentTypeId, sizeof...(Components)> componentTypeIds {};
            std::array<void*, sizeof...(Components)> destinations {};
            std::array<size_t, sizeof...(Components)> componentIndices {};
            size_t count = 0;

            for (size_t j = i; j < m_archetypePools.size(); ++j)
            {
                if (m_archetypePools[j] != archetypePool)
                    continue;

                handled[j] = true;
                componentTypeIds[count] = m_componentTypeIds[j];
                componentIndices[count] = j;
                ++count;
            }

            if (!archetypePool->componentPointers(
                entityIndex,
                componentTypeIds.data(),
                destinations.data(),
                count
            )) {
                continue;
            }

            for (size_t componentIndex = 0; componentIndex < count; ++componentIndex)
                rawComponents[componentIndices[componentIndex]] = destinations[componentIndex];
        }
    }

    void fill_archetype_raw_components(
        const view_match& match,
        std::array<void*, sizeof...(Components)>& rawComponents
    ) {
        std::array<bool, sizeof...(Components)> handled {};

        for (size_t i = 0; i < m_archetypePools.size(); ++i)
        {
            ecs::IArchetypePool* archetypePool = m_archetypePools[i];
            if (nullptr == archetypePool || handled[i])
                continue;

            std::array<ecs::ComponentTypeId, sizeof...(Components)> componentTypeIds {};
            std::array<void*, sizeof...(Components)> destinations {};
            std::array<size_t, sizeof...(Components)> componentIndices {};
            size_t count = 0;

            for (size_t j = i; j < m_archetypePools.size(); ++j)
            {
                if (m_archetypePools[j] != archetypePool)
                    continue;

                handled[j] = true;
                componentTypeIds[count] = m_componentTypeIds[j];
                componentIndices[count] = j;
                ++count;
            }

            if (!archetypePool->denseComponentPointers(
                match.denseIndices[i],
                componentTypeIds.data(),
                destinations.data(),
                count
            )) {
                continue;
            }

            for (size_t componentIndex = 0; componentIndex < count; ++componentIndex)
                rawComponents[componentIndices[componentIndex]] = destinations[componentIndex];
        }
    }

    template <size_t... Is>
    void fill_standalone_raw_components(
        const view_match& match,
        std::array<void*, sizeof...(Components)>& rawComponents,
        std::index_sequence<Is...>
    ) {
        auto fill = [&]<size_t I>()
        {
            if (nullptr == m_archetypePools[I])
                rawComponents[I] = &std::get<I>(m_pools)->dense_at(match.denseIndices[I]);
        };

        (fill.template operator()<Is>(), ...);
    }

    template <size_t... Is>
    void fill_render_standalone_raw_components(
        const view_match& match,
        std::array<void*, sizeof...(Components)>& rawComponents,
        std::index_sequence<Is...>
    ) {
        auto fill = [&]<size_t I>()
        {
            if (nullptr == m_archetypePools[I])
                rawComponents[I] = const_cast<value_at_t<I>*>(&std::get<I>(m_renderPools)->dense_at(match.denseIndices[I]));
        };

        (fill.template operator()<Is>(), ...);
    }

    template <size_t... Is>
    bool raw_components_present(
        const std::array<void*, sizeof...(Components)>& rawComponents,
        std::index_sequence<Is...>
    ) const {
        return ((nullptr != rawComponents[Is]) && ...);
    }

    template <typename Func, size_t... Is>
    void invoke_sim_raw_component_callback(
        const size_t entityIndex,
        Func& func,
        std::array<void*, sizeof...(Components)>& rawComponents,
        std::index_sequence<Is...>
    ) {
        if constexpr (std::is_invocable_v<Func&, Entity, value_at_t<Is>&...>)
        {
            func(
                m_ecs->make_handle(entityIndex),
                *static_cast<value_at_t<Is>*>(rawComponents[Is])...
            );
        }
        else if constexpr (std::is_invocable_v<Func&, value_at_t<Is>&...>)
        {
            func(*static_cast<value_at_t<Is>*>(rawComponents[Is])...);
        }
        else
        {
            static_assert(
                always_false_v<Func>,
                "View::each callback must accept `(Entity, Component&...)` or `(Component&...)`."
            );
        }
    }

    template <typename Func, size_t... Is>
    void invoke_render_raw_component_callback(
        const size_t entityIndex,
        Func& func,
        std::array<void*, sizeof...(Components)>& rawComponents,
        std::index_sequence<Is...>
    ) {
        if constexpr (std::is_invocable_v<Func&, Entity, const value_at_t<Is>&...>)
        {
            func(
                m_ecs->make_handle(entityIndex),
                *static_cast<const value_at_t<Is>*>(rawComponents[Is])...
            );
        }
        else if constexpr (std::is_invocable_v<Func&, const value_at_t<Is>&...>)
        {
            func(*static_cast<const value_at_t<Is>*>(rawComponents[Is])...);
        }
        else
        {
            static_assert(
                always_false_v<Func>,
                "View render storage callback must accept const component references"
            );
        }
    }

    template <typename Func>
    void visit_sim_match(const view_match& match, Func& func)
    {
        std::array<void*, sizeof...(Components)> rawComponents {};
        fill_archetype_raw_components(match, rawComponents);
        fill_standalone_raw_components(
            match,
            rawComponents,
            std::make_index_sequence<sizeof...(Components)>{}
        );

        if (!raw_components_present(rawComponents, std::make_index_sequence<sizeof...(Components)>{}))
            return;

        invoke_sim_raw_component_callback(
            match.entityIndex,
            func,
            rawComponents,
            std::make_index_sequence<sizeof...(Components)>{}
        );
        mark_sim_components_dirty_after_callback<Func>(match.entityIndex);
    }

    template <typename Func>
    void visit_render_match_from_sources(const view_match& match, Func& func)
    {
        std::array<void*, sizeof...(Components)> rawComponents {};
        fill_archetype_raw_components(match, rawComponents);
        fill_render_standalone_raw_components(
            match,
            rawComponents,
            std::make_index_sequence<sizeof...(Components)>{}
        );

        if (!raw_components_present(rawComponents, std::make_index_sequence<sizeof...(Components)>{}))
            return;

        invoke_render_raw_component_callback(
            match.entityIndex,
            func,
            rawComponents,
            std::make_index_sequence<sizeof...(Components)>{}
        );
    }

    template <typename Func>
    void each_sim_match_range(const size_t beginMatch, const size_t endMatch, Func& func)
    {
        const size_t boundedEnd = std::min(endMatch, m_matches.length());
        for (size_t matchIndex = beginMatch; matchIndex < boundedEnd; ++matchIndex)
            visit_sim_match(m_matches[matchIndex], func);
    }

    template <typename Func>
    void each_render_match_range(const size_t beginMatch, const size_t endMatch, Func& func)
    {
        const size_t boundedEnd = std::min(endMatch, m_matches.length());
        for (size_t matchIndex = beginMatch; matchIndex < boundedEnd; ++matchIndex)
            visit_render_match_from_sources(m_matches[matchIndex], func);
    }

    template <bool Rendering, typename Func>
    void each_single_archetype_source_range(const size_t beginDense, const size_t endDense, Func& func)
    {
        if (!m_singleArchetypeSource || Entity::N_POS == m_primaryIndex)
            return;

        ecs::IArchetypePool* archetypePool = m_archetypePools[m_primaryIndex];
        if (nullptr == archetypePool)
            return;

        const ecs::ComponentTypeId primaryTypeId = m_componentTypeIds[m_primaryIndex];
        const size_t boundedEnd = std::min(endDense, archetypePool->componentSize(primaryTypeId));
        for (size_t componentDenseIndex = beginDense; componentDenseIndex < boundedEnd; ++componentDenseIndex)
        {
            const size_t entityIndex = archetypePool->componentEntityAt(primaryTypeId, componentDenseIndex);
            if (!m_ecs->is_alive_index(entityIndex))
                continue;

            std::array<void*, sizeof...(Components)> rawComponents {};
            if (!archetypePool->componentPointers(
                entityIndex,
                m_componentTypeIds.data(),
                rawComponents.data(),
                sizeof...(Components)
            )) {
                continue;
            }

            if constexpr (Rendering)
            {
                invoke_render_raw_component_callback(
                    entityIndex,
                    func,
                    rawComponents,
                    std::make_index_sequence<sizeof...(Components)>{}
                );
            }
            else
            {
                invoke_sim_raw_component_callback(
                    entityIndex,
                    func,
                    rawComponents,
                    std::make_index_sequence<sizeof...(Components)>{}
                );
                mark_sim_components_dirty_after_callback<Func>(entityIndex);
            }
        }
    }

    template <typename Func>
    void each_range(const size_t beginDense, const size_t endDense, Func& func)
    {
        if (is_rendering_storage())
        {
            if constexpr (accepts_render_refs<Func>())
            {
                if (m_singleArchetypeSource)
                {
                    each_single_archetype_source_range<true>(beginDense, endDense, func);
                }
                else if (m_usesArchetypeSources)
                {
                    each_render_match_range(beginDense, endDense, func);
                }
                else if constexpr (sizeof...(Components) > 1)
                {
                    each_match_range(m_renderPools, beginDense, endDense, func);
                }
                else
                {
                    each_with_primary_range_impl(
                        m_renderPools,
                        beginDense,
                        endDense,
                        func,
                        std::make_index_sequence<sizeof...(Components)>{}
                    );
                }
            }
            else
            {
                throw std::invalid_argument("View render storage callback must accept const component references");
            }
        }
        else
        {
            if constexpr (accepts_sim_refs<Func>())
            {
                if (m_singleArchetypeSource)
                {
                    each_single_archetype_source_range<false>(beginDense, endDense, func);
                }
                else if (m_usesArchetypeSources)
                {
                    each_sim_match_range(beginDense, endDense, func);
                }
                else if constexpr (sizeof...(Components) > 1)
                {
                    each_match_range(m_pools, beginDense, endDense, func);
                }
                else
                {
                    each_with_primary_range_impl(
                        m_pools,
                        beginDense,
                        endDense,
                        func,
                        std::make_index_sequence<sizeof...(Components)>{}
                    );
                }
            }
            else
            {
                static_assert(
                    always_false_v<Func>,
                    "View simulation storage callback must accept mutable component references"
                );
            }
        }
    }

    template <typename Callable>
    bool execute_mt_entity_callback(const Entity& entity, Callable& callable)
    {
        if constexpr (0 == sizeof...(Components))
        {
            invoke_entity_only_callback(entity, callable);
        }
        else
        {
            this->visit_entity(
                entity.index,
                callable
            );
        }

        return true;
    }

    template <typename Callable>
    std::function<bool(const Entity&)> make_mt_executor(Callable&& callable)
    {
        using DecayedCallable = std::decay_t<Callable>;
        return std::function<bool(const Entity&)>(
            [this, callable = DecayedCallable(std::forward<Callable>(callable))](const Entity& entity) mutable
            {
                return this->execute_mt_entity_callback(entity, callable);
            }
        );
    }

    template <typename Func, size_t... Is>
    void visit_entity_from_sim_pools(const size_t entityIndex, Func& func, std::index_sequence<Is...>)
    {
        auto components = std::tuple{ std::get<Is>(m_pools)->try_get(entityIndex)... };
        if (!all_components_present(components, std::make_index_sequence<sizeof...(Components)>{}))
            return;

        invoke_component_callback(entityIndex, func, components, std::make_index_sequence<sizeof...(Components)>{});
        mark_sim_components_dirty_after_callback<Func>(entityIndex);
    }

    template <size_t... Is>
    void fill_standalone_raw_components_for_entity(
        const size_t entityIndex,
        std::array<void*, sizeof...(Components)>& rawComponents,
        std::index_sequence<Is...>
    ) {
        auto fill = [&]<size_t I>()
        {
            if (nullptr == m_archetypePools[I])
                rawComponents[I] = std::get<I>(m_pools)->try_get(entityIndex);
        };

        (fill.template operator()<Is>(), ...);
    }

    template <size_t... Is>
    void fill_render_standalone_raw_components_for_entity(
        const size_t entityIndex,
        std::array<void*, sizeof...(Components)>& rawComponents,
        std::index_sequence<Is...>
    ) {
        auto fill = [&]<size_t I>()
        {
            if (nullptr == m_archetypePools[I])
                rawComponents[I] = const_cast<value_at_t<I>*>(std::get<I>(m_renderPools)->try_get(entityIndex));
        };

        (fill.template operator()<Is>(), ...);
    }

    template <typename Func>
    void visit_entity_from_sim_sources(const size_t entityIndex, Func& func)
    {
        std::array<void*, sizeof...(Components)> rawComponents {};
        fill_archetype_raw_components(entityIndex, rawComponents);
        fill_standalone_raw_components_for_entity(
            entityIndex,
            rawComponents,
            std::make_index_sequence<sizeof...(Components)>{}
        );

        if (!raw_components_present(rawComponents, std::make_index_sequence<sizeof...(Components)>{}))
            return;

        invoke_sim_raw_component_callback(
            entityIndex,
            func,
            rawComponents,
            std::make_index_sequence<sizeof...(Components)>{}
        );
        mark_sim_components_dirty_after_callback<Func>(entityIndex);
    }

    template <typename Func>
    void visit_entity_from_render_sources(const size_t entityIndex, Func& func)
    {
        std::array<void*, sizeof...(Components)> rawComponents {};
        fill_archetype_raw_components(entityIndex, rawComponents);
        fill_render_standalone_raw_components_for_entity(
            entityIndex,
            rawComponents,
            std::make_index_sequence<sizeof...(Components)>{}
        );

        if (!raw_components_present(rawComponents, std::make_index_sequence<sizeof...(Components)>{}))
            return;

        invoke_render_raw_component_callback(
            entityIndex,
            func,
            rawComponents,
            std::make_index_sequence<sizeof...(Components)>{}
        );
    }

    template <typename Func, size_t... Is>
    void visit_entity_from_render_pools(const size_t entityIndex, Func& func, std::index_sequence<Is...>)
    {
        auto components = std::tuple{ std::get<Is>(m_renderPools)->try_get(entityIndex)... };
        if (!all_components_present(components, std::make_index_sequence<sizeof...(Components)>{}))
            return;

        invoke_component_callback(entityIndex, func, components, std::make_index_sequence<sizeof...(Components)>{});
    }

    template <typename Func>
    void visit_entity(const size_t entityIndex, Func& func)
    {
        if (is_rendering_storage())
        {
            if constexpr (accepts_render_refs<Func>())
            {
                if (m_usesArchetypeSources)
                    visit_entity_from_render_sources(entityIndex, func);
                else
                    visit_entity_from_render_pools(entityIndex, func, std::make_index_sequence<sizeof...(Components)>{});
            }
            else
            {
                throw std::invalid_argument("View render storage callback must accept const component references");
            }
        }
        else
        {
            if constexpr (accepts_sim_refs<Func>())
            {
                if (m_usesArchetypeSources)
                    visit_entity_from_sim_sources(entityIndex, func);
                else
                    visit_entity_from_sim_pools(entityIndex, func, std::make_index_sequence<sizeof...(Components)>{});
            }
            else
            {
                static_assert(
                    always_false_v<Func>,
                    "View simulation storage callback must accept mutable component references"
                );
            }
        }
    }

    template <typename Callable>
    void append_dense_range_jobs(
        Threadpool& pool,
        const size_t total,
        const size_t chunkSize,
        const size_t chunks,
        Callable& callable,
        ArrayList<Promise<bool>>& promises
    ) {
        promises.reserve(chunks);

        for (size_t chunk = 0; chunk < chunks; ++chunk)
        {
            const dense_range range = ecs_view_detail::make_chunk_range(chunk, chunkSize, total);
            promises.append(pool.submit([this, range, callable]() mutable
            {
                each_range(range.begin, range.end, callable);
                return true;
            }));
        }
    }

    template <typename Callable>
    void each_component_mt(Threadpool& pool, const size_t minChunk, Callable&& callable)
    {
        const size_t total = iteration_size_runtime();
        if (0 == total)
            return;

        const size_t chunkSize = ecs_view_detail::dense_chunk_size(total, pool, minChunk);
        const size_t chunks = ecs_view_detail::chunk_count(total, chunkSize);
        using DecayedCallable = std::decay_t<Callable>;
        DecayedCallable callableSeed(std::forward<Callable>(callable));

        ArrayList<Promise<bool>> promises;
        append_dense_range_jobs(pool, total, chunkSize, chunks, callableSeed, promises);
        ecs_view_detail::await_all(promises);
    }

    template <typename Callable>
    void each_entity_mt(Threadpool& pool, const size_t minChunk, Callable&& callable)
    {
        const ArrayList<Entity> entities = allEntities();
        if (entities.empty())
            return;

        auto promise = pool.map<Entity, bool>(
            make_mt_executor(std::forward<Callable>(callable)),
            &entities,
            minChunk
        );

        auto& result = promise.await();
        if (result.is_failure())
            std::rethrow_exception(result.exception());
    }

    template <typename Callable>
    void each_entity_list_mt(
        const ArrayList<Entity>& entities,
        Threadpool& pool,
        const size_t minChunk,
        Callable&& callable
    ) {
        if (entities.empty())
            return;

        auto promise = pool.map<Entity, bool>(
            make_mt_executor(std::forward<Callable>(callable)),
            &entities,
            minChunk
        );

        auto& result = promise.await();
        if (result.is_failure())
            std::rethrow_exception(result.exception());
    }

public:
    explicit View(ECS& ecs, const ViewStorage storage = ViewStorage::Simulation)
        : m_ecs(&ecs),
          m_storage(storage)
    {}

    void set_dirty_tracking(const bool enabled)
    { m_trackDirtyWrites = enabled; }

    bool empty()
    {
        if (!ensure_cache())
            return true;

        return 0 == size();
    }

    void refresh()
    {
        m_cacheResolved = false;
        m_matchesResolved = false;
        ensure_cache();
    }

    size_t size()
    {
        if (!ensure_matches())
            return 0;

        if constexpr (0 == sizeof...(Components))
            return alive_entity_count();
        else if (m_singleArchetypeSource)
            return single_archetype_match_count();
        else if (m_usesArchetypeSources)
            return m_matches.length();
        else if constexpr (1 == sizeof...(Components))
            return primary_size_runtime();
        else
            return m_matches.length();
    }

    ArrayList<Entity> allEntities()
    {
        ArrayList<Entity> entities;
        entities.reserve(iteration_size_runtime());
        this->each([&entities](const Entity& entity, auto&...)
        {
            entities.append(entity);
        });
        return entities;
    }

    const_iterator begin() const
        requires (sizeof...(Components) == 1)
    {
        auto* self = const_cast<View*>(this);
        self->ensure_matches();
        return const_iterator(this, 0);
    }

    const_iterator end() const
        requires (sizeof...(Components) == 1)
    {
        auto* self = const_cast<View*>(this);
        return const_iterator(this, self->size());
    }

    template <typename Func>
    void each(Func&& func)
    {
        if (!ensure_matches())
            return;

        if constexpr (0 == sizeof...(Components))
        {
            iterate_all_entities(func);
        }
        else
        {
            each_range(0, iteration_size_runtime(), func);
        }
    }

    template <typename Func>
    void each(Func&& func) const
    {
        each_const_impl(
            std::forward<Func>(func),
            std::make_index_sequence<sizeof...(Components)>{}
        );
    }

    template <typename Func>
    void each_mt(Func&& func, Threadpool& pool, const size_t minChunk = 256)
    {
        if (!ensure_matches())
            return;

        using Callable = std::decay_t<Func>;
        Callable callableSeed(std::forward<Func>(func));

        if constexpr (0 == sizeof...(Components))
            each_entity_mt(pool, minChunk, std::move(callableSeed));
        else
            each_component_mt(pool, minChunk, std::move(callableSeed));
    }

    template <typename Func>
    void each_entities_mt(
        const ArrayList<Entity>& entities,
        Func&& func,
        Threadpool& pool,
        const size_t minChunk = 256
    ) {
        if (entities.empty() || !ensure_cache())
            return;

        using Callable = std::decay_t<Func>;
        Callable callableSeed(std::forward<Func>(func));
        each_entity_list_mt(entities, pool, minChunk, std::move(callableSeed));
    }

    template <typename Func>
    void each_entities(const ArrayList<Entity>& entities, Func&& func)
    {
        if (entities.empty() || !ensure_cache())
            return;

        using Callable = std::decay_t<Func>;
        Callable callableSeed(std::forward<Func>(func));
        for (const Entity& entity : entities)
            execute_mt_entity_callback(entity, callableSeed);
    }

    template <typename Func>
    void each(Func&& func, Threadpool& pool)
    {
        each_mt(std::forward<Func>(func), pool);
    }

};
