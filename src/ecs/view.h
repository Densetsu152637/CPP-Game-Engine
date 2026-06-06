//
// Created by Nicholas on 06/05/26.
//

#pragma once

#include <algorithm>
#include <array>
#include <exception>
#include <iterator>
#include <functional>
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

    static size_t chunk_count(const size_t total, const size_t chunkSize)
    { return (total + chunkSize - 1) / chunkSize; }

    static dense_range make_chunk_range(const size_t chunk, const size_t chunkSize, const size_t total)
    {
        const size_t begin = chunk * chunkSize;
        return { begin, std::min(total, begin + chunkSize) };
    }

    bool is_rendering_storage() const
    { return ViewStorage::Rendering == m_storage; }

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

    void resolve_cache()
    {
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
    }

    bool ensure_cache()
    {
        if (nullptr == m_ecs)
            return false;

        if (!m_cacheResolved)
            resolve_cache();

        if constexpr (0 == sizeof...(Components))
            return true;

        return m_primaryIndex != Entity::N_POS;
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

    template <typename Func>
    void each_range(const size_t beginDense, const size_t endDense, Func& func)
    {
        if (is_rendering_storage())
        {
            if constexpr (accepts_render_refs<Func>())
            {
                each_with_primary_range_impl(
                    m_renderPools,
                    beginDense,
                    endDense,
                    func,
                    std::make_index_sequence<sizeof...(Components)>{}
                );
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
                each_with_primary_range_impl(
                    m_pools,
                    beginDense,
                    endDense,
                    func,
                    std::make_index_sequence<sizeof...(Components)>{}
                );
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
            promises.append(pool.submit([this, range, callable]() mutable
            {
                each_range(range.begin, range.end, callable);
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
        const size_t total = primary_size_runtime();
        if (0 == total)
            return;

        const size_t chunkSize = dense_chunk_size(total, pool, minChunk);
        const size_t chunks = chunk_count(total, chunkSize);

        ArrayList<Promise<bool>> promises;
        append_dense_range_jobs(pool, total, chunkSize, chunks, callable, promises);
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
        ensure_cache();
    }

    size_t size()
    {
        if (!ensure_cache())
            return 0;

        if constexpr (0 == sizeof...(Components))
            return alive_entity_count();
        else if constexpr (1 == sizeof...(Components))
            return primary_size_runtime();

        size_t matches = 0;
        each([&](auto&&...) { ++matches; });
        return matches;
    }

    ArrayList<Entity> allEntities()
    {
        ArrayList<Entity> entities;
        entities.reserve(primary_size_runtime());
        this->each([&entities](const Entity& entity, auto&...)
        {
            entities.append(entity);
        });
        return entities;
    }

    template <typename Func>
    void each(Func&& func)
    {
        if (!ensure_cache())
            return;

        if constexpr (0 == sizeof...(Components))
        {
            iterate_all_entities(func);
        }
        else
        {
            each_range(0, primary_size_runtime(), func);
        }
    }

    template <typename Func>
    void each_mt(Func&& func, Threadpool& pool, const size_t minChunk = 256)
    {
        if (!ensure_cache())
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
