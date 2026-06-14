//
// Compatibility aggregate for ECS component-pool types.
//

#pragma once

#include <type_traits>

#include "component_pool_base.h"
#include "component_pool_simulation.h"
#include "component_pool_shared.h"
#include "component_pool_render.h"

template <typename T>
using SimulationComponentPoolFor = std::conditional_t<
    ecs::is_shared_component_alias_v<T>,
    SharedComponentPool<T>,
    std::conditional_t<
        ecs::is_buffered_component_v<T>,
        BufferedComponentPool<T>,
        ComponentPool<T>
    >
>;
