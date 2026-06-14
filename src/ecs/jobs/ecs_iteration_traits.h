//
// Named ECSProcessor simulation/render argument groups.
//

#pragma once

#include "ecs_sim_arg_traits.h"

namespace ecs_sim
{
    template <typename... Args>
    struct SimulationIterationTypes
    {
        using submitted_args = type_list<Args...>;
        using callable_args = callable_submit_arg_list_t<Args...>;
        using match_components = simulation_match_component_list_t<Args...>;
        using iteration_components = match_components;
        using tags = tag_list_t<Args...>;
        using excludes = exclude_list_t<Args...>;
        using shared_components = shared_component_list_t<Args...>;
        using dirty_components = guaranteed_dirty_component_list_t<Args...>;
    };

    template <typename... Args>
    struct RenderIterationTypes
    {
        using submitted_args = type_list<Args...>;
        using iteration_components = component_list_t<Args...>;
        using match_components = render_match_component_list_t<Args...>;
        using tags = tag_list_t<Args...>;
        using excludes = exclude_list_t<Args...>;
        using shared_components = shared_component_list_t<Args...>;
    };
}
