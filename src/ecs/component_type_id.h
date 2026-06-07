//
// Dense runtime IDs for ECS component types.
//

#pragma once

#include <atomic>
#include <cstdint>
#include <typeinfo>

#include "component_alias_traits.h"

namespace ecs
{
    using ComponentTypeId = uint32_t;

    namespace detail
    {
        inline std::atomic<ComponentTypeId> nextComponentTypeId = 0;
    }

    template <typename T>
    ComponentTypeId component_type_id()
    {
        using Key = component_key_t<T>;
        static const ComponentTypeId id = detail::nextComponentTypeId.fetch_add(
            1,
            std::memory_order_relaxed
        );
        return id;
    }

    template <typename T>
    const char* component_type_name()
    {
        return typeid(component_key_t<T>).name();
    }
}
