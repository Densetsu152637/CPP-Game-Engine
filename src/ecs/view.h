//
// Created by Nicholas on 06/05/26.
//

#pragma once

#include <algorithm>
#include <array>
#include <exception>
#include <iterator>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include "component_pool.h"
#include "../async/threadpool.h"

enum class ViewStorage
{
    Simulation,
    Rendering
};

template <typename... Components>
class View
{
    template <typename Component>
    using sim_pool_t = std::conditional_t<
        ecs::is_buffered_component_v<ecs::component_value_t<Component>>,
        BufferedComponentPool<ecs::component_value_t<Component>>,
        ComponentPool<ecs::component_value_t<Component>>
    >;

    using sim_pool_tuple = std::tuple<sim_pool_t<Components>*...>;
    using render_pool_tuple = std::tuple<RenderComponentPool<ecs::component_value_t<Components>>*...>;

    ECS* m_ecs = nullptr;
    sim_pool_tuple m_pools {};
    render_pool_tuple m_renderPools {};
    size_t m_primaryIndex = Entity::N_POS;
    bool m_cacheResolved = false;
    ViewStorage m_storage = ViewStorage::Simulation;

    template <typename>
    static constexpr bool always_false_v = false;

    using component_tuple = std::tuple<Components...>;

    template <size_t I>
    using component_at_t = std::tuple_element_t<I, component_tuple>;

    template <size_t I>
    using value_at_t = ecs::component_value_t<component_at_t<I>>;

    struct dense_range
    {
        size_t begin = 0;
        size_t end = 0;
    };

    using dense_index_array = std::array<size_t, sizeof...(Components)>;

    struct view_match
    {
        size_t entityIndex = Entity::N_POS;
        dense_index_array denseIndices {};
    };

    ArrayList<view_match> m_matches;
    bool m_matchesResolved = false;
    static constexpr size_t UNOBSERVED_GENERATION = static_cast<size_t>(-1);
    std::array<size_t, sizeof...(Components)> m_observedPoolGenerations {};
    size_t m_observedEntityGeneration = UNOBSERVED_GENERATION;
    size_t m_observedStorageGeneration = UNOBSERVED_GENERATION;

    static size_t chunk_count(const size_t total, const size_t chunkSize)
    { return (total + chunkSize - 1) / chunkSize; }

    static dense_range make_chunk_range(const size_t chunk, const size_t chunkSize, const size_t total)
    {
        const size_t begin = chunk * chunkSize;
        return { begin, std::min(total, begin + chunkSize) };
    }

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
        return {
            (nullptr == std::get<Is>(pools) ? 0 : std::get<Is>(pools)->generation())...
        };
    }

    std::array<size_t, sizeof...(Components)> current_pool_generations() const
    {
        if constexpr (0 == sizeof...(Components))
            return {};
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

    template <typename PoolTuple, size_t... Is>
    std::array<size_t, sizeof...(Components)> storage_sizes(const PoolTuple& pools, std::index_sequence<Is...>) const
    {
        return {
            (nullptr == std::get<Is>(pools) ? 0 : std::get<Is>(pools)->size())...
        };
    }

    template <typename PoolTuple>
    size_t select_primary_storage(const PoolTuple& pools) const
    {
        const auto sizes = storage_sizes(pools, std::make_index_sequence<sizeof...(Components)>{});
        if (std::any_of(sizes.begin(), sizes.end(), [](const size_t size) { return 0 == size; }))
            return Entity::N_POS;

        const auto smallest = std::min_element(sizes.begin(), sizes.end());
        return static_cast<size_t>(std::distance(sizes.begin(), smallest));
    }

    template <size_t I, size_t PrimaryI, typename PoolTuple>
    bool fill_match_dense_index(
        PoolTuple& pools,
        const size_t entityIndex,
        const size_t primaryDenseIndex,
        view_match& match
    ) const
    {
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

    void resolve_matches()
    {
        m_matches.clear();

        if constexpr (sizeof...(Components) > 1)
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

        m_matchesResolved = true;
    }

    void resolve_cache()
    {
        m_matchesResolved = false;

        if constexpr (0 == sizeof...(Components))
        {
            m_primaryIndex = 0;
        }
        else
        {
            if (is_rendering_storage())
            {
                m_renderPools = render_pool_tuple{
                    m_ecs->render_storage_if_exists<Components>()...
                };
                m_primaryIndex = select_primary_storage(m_renderPools);
            }
            else
            {
                m_pools = sim_pool_tuple{
                    m_ecs->storage_if_exists<Components>()...
                };
                m_primaryIndex = select_primary_storage(m_pools);
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
            return true;

        return m_primaryIndex != Entity::N_POS;
    }

    bool ensure_matches()
    {
        if (!ensure_cache())
            return false;

        if (!m_matchesResolved)
            resolve_matches();

        return true;
    }

    size_t alive_entity_count() const
    {
        size_t alive = 0;
        for (const auto& record : m_ecs->m_entities)
        {
            if (record.alive)
                ++alive;
        }
        return alive;
    }

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
        for (size_t i = 0; i < m_ecs->m_entities.length(); ++i)
        {
            const EntityRecord& record = m_ecs->m_entities[i];
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
        if constexpr (sizeof...(Components) <= 1)
            return primary_size_runtime();
        else
        {
            ensure_matches();
            return m_matches.length();
        }
    }

    size_t dense_chunk_size(const size_t total, Threadpool& pool, const size_t minChunk) const
    {
        const size_t numThreads = std::max<size_t>(1, pool.size());
        const size_t defaultChunk = std::max<size_t>(1, total / numThreads);
        return std::max<size_t>(
            1,
            minChunk == 0 ? defaultChunk : minChunk
        );
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

    template <typename Func>
    void each_range(const size_t beginDense, const size_t endDense, Func& func)
    {
        if (is_rendering_storage())
        {
            if constexpr (accepts_render_refs<Func>())
            {
                if constexpr (sizeof...(Components) > 1)
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
                if constexpr (sizeof...(Components) > 1)
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
            const dense_range range = make_chunk_range(chunk, chunkSize, total);
            auto chunkCallable = std::make_shared<Callable>(callable);
            promises.append(pool.submit([this, range, chunkCallable]() mutable
            {
                each_range(range.begin, range.end, *chunkCallable);
                return true;
            }));
        }
    }

    void await_all(ArrayList<Promise<bool>>& promises)
    {
        for (auto& promise : promises)
        {
            auto& result = promise.await();
            if (result.is_failure())
                std::rethrow_exception(result.exception());
        }
    }

    template <typename Callable>
    void each_component_mt(Threadpool& pool, const size_t minChunk, Callable&& callable)
    {
        const size_t total = iteration_size_runtime();
        if (0 == total)
            return;

        const size_t chunkSize = dense_chunk_size(total, pool, minChunk);
        const size_t chunks = chunk_count(total, chunkSize);
        using DecayedCallable = std::decay_t<Callable>;
        DecayedCallable callableSeed(std::forward<Callable>(callable));

        ArrayList<Promise<bool>> promises;
        append_dense_range_jobs(pool, total, chunkSize, chunks, callableSeed, promises);
        await_all(promises);
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

public:
    explicit View(ECS& ecs, const ViewStorage storage = ViewStorage::Simulation)
        : m_ecs(&ecs),
          m_storage(storage)
    {}

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
    void each(Func&& func, Threadpool& pool)
    {
        each_mt(std::forward<Func>(func), pool);
    }

};
