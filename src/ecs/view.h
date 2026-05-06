//
// Created by Nicholas on 06/05/26.
//

#pragma once

#include <algorithm>
#include <array>
#include <iterator>
#include <type_traits>
#include <tuple>
#include <utility>

#include "../ecs/ecs.h"

template <typename... Components>
class View
{
    static_assert(sizeof...(Components) > 0, "A view needs at least one component type.");

    ECS* m_ecs = nullptr;
    std::tuple<ComponentPool<Components>*...> m_pools{};
    size_t m_primaryIndex = Entity::N_POS;
    bool m_resolved = false;

    template <typename T>
    size_t storage_size() const
    {
        const auto* pool = std::get<ComponentPool<T>*>(m_pools);
        return nullptr == pool ? 0 : pool->size();
    }

    // Chooses the smallest available storage among the requested components.
    // This becomes the primary dense iteration source for `each()`.
    size_t select_primary_storage() const
    {
        const std::array<size_t, sizeof...(Components)> sizes{ storage_size<Components>()... };
        if (
            std::any_of(
                sizes.begin(), sizes.end(),
                [](const size_t size) {return 0 == size; }
            )
        ) { return Entity::N_POS; }

        const auto smallest = std::min_element(sizes.begin(), sizes.end());
        return static_cast<size_t>(std::distance(sizes.begin(), smallest));
    }

    void resolve_cache()
    {
        m_pools = std::tuple{ m_ecs->storage_if_exists<Components>()... };
        m_primaryIndex = select_primary_storage();
        m_resolved = (m_primaryIndex != Entity::N_POS);
    }

    // Resolves cached pool pointers and the primary index the first time the view is used.
    bool ensure_cache()
    {
        if (nullptr == m_ecs)
            return false;

        if (!m_resolved)
            resolve_cache();

        return m_resolved;
    }

    template <size_t I = 0>
    // Returns the current size of the selected primary pool.
    size_t primary_size_runtime() const
    {
        if constexpr (I < sizeof...(Components))
        {
            if (m_primaryIndex == I)
            {
                using Primary = std::tuple_element_t<I, std::tuple<Components...>>;
                auto* primary = std::get<ComponentPool<Primary>*>(m_pools);
                return nullptr == primary ? 0 : primary->size();
            }

            return primary_size_runtime<I + 1>();
        }

        return 0;
    }

    // Dispatches over the cached pool tuple and runs the dense iteration path for the
    // selected primary pool.
    template <typename Func, size_t... Is>
    void each_with_primary_impl(Func& func, std::index_sequence<Is...>)
    {
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
            iterate_primary_pool(primary, func);
            ++currentIndex;
        };

        (dispatch(std::get<Is>(m_pools)), ...);
    }

    // Iterates the primary pool densely and checks the other requested components for each entity.
    template <typename PoolT, typename Func>
    void iterate_primary_pool(PoolT* primary, Func& func)
    {
        if (nullptr == primary)
            return;

        for (size_t denseIndex = 0; denseIndex < primary->size(); ++denseIndex)
        {
            const size_t entityIndex = primary->entity_at(denseIndex);
            if (!m_ecs->is_alive_index(entityIndex))
                continue;

            this->visit_entity(entityIndex, func, std::make_index_sequence<sizeof...(Components)>{});
        }
    }

    // Gathers component references for one entity and invokes the callback only if every
    // requested component exists.
    template <typename Func, size_t... Is>
    void visit_entity(const size_t entityIndex, Func& func, std::index_sequence<Is...>)
    {
        auto components = std::tuple{ std::get<Is>(m_pools)->try_get(entityIndex)... };
        if (!(((std::get<Is>(components) != nullptr) && ...)))
            return;

        if constexpr (std::is_invocable_v<Func&, Entity, Components&...>)
        {
            func(m_ecs->make_handle(entityIndex), *std::get<Is>(components)...);
        } else
        {
            func(*std::get<Is>(components)...);
        }
    }

public:
    // Creates a view bound to one ECS registry.
    explicit View(ECS& ecs) : m_ecs(&ecs) {}

    // Returns true when the view cannot produce any matching entities.
    bool empty()
    {
        if (!ensure_cache())
            return true;

        return m_primaryIndex == Entity::N_POS || primary_size_runtime() == 0;
    }

    // Resets cached primary selection so the next call re-evaluates the storages.
    void refresh()
    {
        m_resolved = false;
        resolve_cache();
    }

    // Iterates all entities that contain every requested component.
    // The callback may receive either `(Entity, Components&...)` or just `(Components&...)`.
    template <typename Func>
    void each(Func&& func)
    {
        if (!ensure_cache())
            return;

        auto&& callable = func;
        each_with_primary_impl(callable, std::make_index_sequence<sizeof...(Components)>{});
    }
};
