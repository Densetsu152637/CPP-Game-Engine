//
// View pool matching helpers.
//

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <tuple>

#include "../core/entity.h"

namespace ecs_view_detail
{
    template <typename PoolTuple, size_t... Is>
    std::array<size_t, sizeof...(Is)> pool_generations(const PoolTuple& pools, std::index_sequence<Is...>)
    {
        return {
            (nullptr == std::get<Is>(pools) ? 0 : std::get<Is>(pools)->generation())...
        };
    }

    template <typename PoolTuple, size_t... Is>
    std::array<size_t, sizeof...(Is)> storage_sizes(const PoolTuple& pools, std::index_sequence<Is...>)
    {
        return {
            (nullptr == std::get<Is>(pools) ? 0 : std::get<Is>(pools)->size())...
        };
    }

    template <size_t ComponentCount, typename PoolTuple>
    size_t select_primary_storage(const PoolTuple& pools)
    {
        const auto sizes = storage_sizes(pools, std::make_index_sequence<ComponentCount>{});
        if (std::any_of(sizes.begin(), sizes.end(), [](const size_t size) { return 0 == size; }))
            return Entity::N_POS;

        const auto smallest = std::min_element(sizes.begin(), sizes.end());
        return static_cast<size_t>(std::distance(sizes.begin(), smallest));
    }
}
