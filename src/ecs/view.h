//
// Created by Nicholas on 06/05/26.
//

#pragma once

#include <algorithm>
#include <array>
#include <iterator>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

#include "component_pool.h"
#include "../async/threadpool.h"

template <typename... Components>
class View
{
    ECS* m_ecs = nullptr;
    std::tuple<ComponentPool<Components>*...> m_pools {};
    size_t m_primaryIndex = Entity::N_POS;
    bool m_cacheResolved = false;

    template <typename>
    static constexpr bool always_false_v = false;

    template <size_t... Is>
    std::array<size_t, sizeof...(Components)> storage_sizes(std::index_sequence<Is...>) const
    {
        return {
            (nullptr == std::get<Is>(m_pools) ? 0 : std::get<Is>(m_pools)->size())...
        };
    }

    size_t select_primary_storage() const
    {
        const auto sizes = storage_sizes(std::make_index_sequence<sizeof...(Components)>{});
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
            m_pools = std::tuple<ComponentPool<Components>*...>{ m_ecs->storage_if_exists<Components>()... };
            m_primaryIndex = select_primary_storage();
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

    size_t primary_size_runtime() const
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
            m_pools
        );

        return primarySize;
    }

    template <typename PoolT, typename Func>
    void iterate_primary_pool_range(PoolT* primary, const size_t beginDense, const size_t endDense, Func& func)
    {
        if (nullptr == primary)
            return;

        const size_t boundedEnd = std::min(endDense, primary->size());
        for (size_t denseIndex = beginDense; denseIndex < boundedEnd; ++denseIndex)
        {
            const size_t entityIndex = primary->entity_at(denseIndex);
            if (!m_ecs->is_alive_index(entityIndex))
                continue;

            this->visit_entity(entityIndex, func, std::make_index_sequence<sizeof...(Components)>{});
        }
    }

    template <typename Func, size_t... Is>
    void each_with_primary_range_impl(
        const size_t beginDense,
        const size_t endDense,
        Func& func,
        std::index_sequence<Is...>
    ) {
        bool handled = false;
        size_t currentIndex = 0;

        auto dispatch = [&](auto* primary)
        {
            if (handled || currentIndex != m_primaryIndex)
            {
                ++currentIndex;
                return;
            }

            handled = true;
            iterate_primary_pool_range(primary, beginDense, endDense, func);
            ++currentIndex;
        };

        (dispatch(std::get<Is>(m_pools)), ...);
    }

    template <typename Func>
    void each_range(const size_t beginDense, const size_t endDense, Func& func)
    {
        each_with_primary_range_impl(
            beginDense,
            endDense,
            func,
            std::make_index_sequence<sizeof...(Components)>{}
        );
    }

    template <typename Func, size_t... Is>
    void visit_entity(const size_t entityIndex, Func& func, std::index_sequence<Is...>)
    {
        auto components = std::tuple{ std::get<Is>(m_pools)->try_get(entityIndex)... };
        if (!(((std::get<Is>(components) != nullptr) && ...)))
            return;

        if constexpr (std::is_invocable_v<Func&, Entity, Pair<Components>&...>)
        {
            func(m_ecs->make_handle(entityIndex), *std::get<Is>(components)...);
        }
        else if constexpr (std::is_invocable_v<Func&, Pair<Components>&...>)
        {
            func(*std::get<Is>(components)...);
        }
        else
        {
            static_assert(
                always_false_v<Func>,
                "View::each callback must accept `(Entity, Pair<Components>&...)` or `(Pair<Components>&...)`."
            );
        }
    }

public:
    explicit View(ECS& ecs) : m_ecs(&ecs) {}

    bool empty()
    {
        if (!ensure_cache())
            return true;

        if constexpr (0 == sizeof...(Components))
            return 0 == alive_entity_count();

        return 0 == primary_size_runtime();
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

        return primary_size_runtime();
    }

    ArrayList<Entity> allEntities()
    {
        ArrayList<Entity> entities;
        this->each([&entities](const Entity& entity, Pair<Components>&...)
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

        auto&& callable = func;
        if constexpr (0 == sizeof...(Components))
        {
            iterate_all_entities(callable);
        }
        else
        {
            each_range(0, primary_size_runtime(), callable);
        }
    }

    template <typename Func>
    void each_mt(Func&& func, Threadpool& pool, const size_t minChunk = 0)
    {
        if (!ensure_cache())
            return;

        ArrayList<Entity> entities = allEntities();
        if (entities.empty())
            return;

        using Callable = std::decay_t<Func>;
        Callable callableSeed(std::forward<Func>(func));

        auto promise = pool.map<Entity, bool>(
            std::function<bool(const Entity&)>(
                [this, callable = std::move(callableSeed)](const Entity& entity) mutable
                {
                    if constexpr (0 == sizeof...(Components))
                    {
                        invoke_entity_only_callback(entity, callable);
                    }
                    else
                    {
                        this->visit_entity(
                            entity.index,
                            callable,
                            std::make_index_sequence<sizeof...(Components)>{}
                        );
                    }

                    return true;
                }
            ),
            &entities,
            minChunk
        );

        auto& result = promise.await();
        if (result.is_failure())
            std::rethrow_exception(result.exception());
    }

    template <typename Func>
    void each(Func&& func, Threadpool& pool)
    {
        each_mt(std::forward<Func>(func), pool);
    }
};
