//
// ECSProcessor component-pool registration helpers.
//

#pragma once

#include <type_traits>

#include "../ecs.h"
#include "ecs_iteration_traits.h"

namespace ecs_processor_detail
{
    template <typename... Components>
    void guarantee_view_components(ECS& ecs, ecs_sim::type_list<Components...>)
    {
        (ecs.template guarantee_component_pool<Components>(), ...);
    }

    template <typename Arg>
    void guarantee_sim_pool(ECS& ecs)
    {
        using Component = ecs_sim::component_for_arg_t<Arg>;
        using SharedComponent = ecs_sim::shared_component_for_arg_t<Arg>;
        using ViewComponents = ecs_sim::view_component_list_for_arg_t<Arg>;

        if constexpr (
            !ecs_sim::is_tag_arg_v<Arg> &&
            !ecs_sim::is_exclude_arg_v<Arg> &&
            !std::is_void_v<Component>
        )
        {
            ecs.template guarantee_component_pool<Component>();
        }

        if constexpr (!std::is_void_v<SharedComponent>)
            ecs.template guarantee_component_pool<SharedComponent>();

        guarantee_view_components(ecs, ViewComponents{});
    }

    template <typename... Args>
    void guarantee_sim_pools(ECS& ecs)
    {
        (guarantee_sim_pool<Args>(ecs), ...);
    }

    template <typename Arg>
    void guarantee_render_pool(ECS& ecs)
    {
        using Component = ecs_sim::render_component_for_arg_t<Arg>;

        if constexpr (!std::is_void_v<Component>)
        {
            ecs.template guarantee_component_pool<Component>();
            ecs.template guarantee_render_component_pool<Component>();
        }
    }

    template <typename... Components>
    void guarantee_render_pools(ECS& ecs)
    {
        (guarantee_render_pool<Components>(ecs), ...);
    }
}
