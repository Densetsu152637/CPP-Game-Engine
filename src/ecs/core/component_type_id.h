//
// Dense runtime IDs for ECS component types.
//

#pragma once

#include <entt/core/type_info.hpp>
#include <cstdint>
#include <typeinfo>

#include "../aliases/component_alias_traits.h"

namespace ecs
{
    using ComponentTypeId = uint32_t;

    template <typename T>
    ComponentTypeId component_type_id()
    {
        using Key = component_key_t<T>;
        return entt::type_id<Key>().index();
    }

    template <typename T>
    const char* component_type_name()
    {
        return typeid(component_key_t<T>).name();
    }
}
