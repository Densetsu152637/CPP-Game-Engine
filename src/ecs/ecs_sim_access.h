//
// ECS job access-set construction and conflict checks.
//

#pragma once

#include <typeinfo>
#include <type_traits>

#include "ecs_sim_arg_traits.h"

namespace ecs_sim
{
    inline bool contains_any(const ArrayList<TypeId>& haystack, const ArrayList<TypeId>& needles)
    {
        for (size_t i = 0; i < needles.length(); ++i)
        {
            if (haystack.contains(needles[i]))
                return true;
        }

        return false;
    }

    inline bool conflicts_with(const AccessSpec& lhs, const AccessSpec& rhs)
    {
        return contains_any(lhs.exclusiveWrites, rhs.reads)
            || contains_any(lhs.exclusiveWrites, rhs.writes)
            || contains_any(rhs.exclusiveWrites, lhs.reads)
            || contains_any(rhs.exclusiveWrites, lhs.writes);
    }

    template <typename Component>
    TypeId component_type_id()
    {
        return typeid(ecs::component_key_t<Component>).hash_code();
    }

    inline void append_unique_access(ArrayList<TypeId>& access, const TypeId id)
    {
        if (!access.contains(id))
            access.append(id);
    }

    template <typename T>
    void append_access(AccessSpec& spec)
    {
        using Arg = std::remove_cvref_t<T>;

        if constexpr (is_array_for_v<Arg>)
        {
            using Component = std::remove_cvref_t<typename Arg::component_type>;
            append_unique_access(spec.reads, component_type_id<Component>());
        }
        else if constexpr (is_plain_component_arg_v<T>)
        {
            using Component = std::remove_cvref_t<T>;
            const TypeId id = component_type_id<Component>();

            if constexpr (is_const_component_arg_v<T>)
            {
                append_unique_access(spec.reads, id);
            }
            else
            {
                append_unique_access(spec.reads, id);
                append_unique_access(spec.writes, id);

                if constexpr (!ecs::is_buffered_component_v<Component>)
                    append_unique_access(spec.exclusiveWrites, id);
            }
        }
    }

    template <typename Param>
    void append_callable_param_access(AccessSpec& spec)
    {
        using Arg = std::remove_cvref_t<Param>;

        if constexpr (is_entity_arg_v<Arg>)
        {
            return;
        }
        else if constexpr (is_array_list_param_v<Param>)
        {
            using Component = array_list_param_component_t<Param>;
            append_unique_access(spec.reads, component_type_id<Component>());
        }
        else
        {
            using Component = Arg;
            const TypeId id = component_type_id<Component>();

            if constexpr (std::is_const_v<std::remove_reference_t<Param>>)
            {
                append_unique_access(spec.reads, id);
            }
            else
            {
                append_unique_access(spec.reads, id);
                append_unique_access(spec.writes, id);

                if constexpr (!ecs::is_buffered_component_v<Component>)
                    append_unique_access(spec.exclusiveWrites, id);
            }
        }
    }

    template <typename... Params>
    AccessSpec build_callable_access_spec(type_list<Params...>)
    {
        AccessSpec spec;
        (append_callable_param_access<Params>(spec), ...);
        return spec;
    }

    template <typename Callable>
    AccessSpec build_callable_access_spec()
    {
        return build_callable_access_spec(callable_arg_list_t<Callable>{});
    }

    template <typename... Args>
    AccessSpec build_access_spec()
    {
        AccessSpec spec;
        (append_access<Args>(spec), ...);
        return spec;
    }
}
