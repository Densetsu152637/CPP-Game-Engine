//
// ECS job access-set construction and conflict checks.
//

#pragma once

#include <string>
#include <type_traits>
#include <utility>

#include "ecs_sim_arg_traits.h"

namespace ecs_sim
{
    enum class AccessConflict
    {
        None,
        MutableMutable,
        NonBufferedMutableRead
    };

    inline bool contains_any(const ArrayList<TypeId>& haystack, const ArrayList<TypeId>& needles)
    {
        for (size_t i = 0; i < needles.length(); ++i)
        {
            if (haystack.contains(needles[i]))
                return true;
        }

        return false;
    }

    inline AccessConflict conflict_between(const AccessSpec& lhs, const AccessSpec& rhs)
    {
        if (contains_any(lhs.writes, rhs.writes))
            return AccessConflict::MutableMutable;

        if (contains_any(lhs.exclusiveWrites, rhs.reads)
            || contains_any(rhs.exclusiveWrites, lhs.reads))
            return AccessConflict::NonBufferedMutableRead;

        return AccessConflict::None;
    }

    inline bool conflicts_with(const AccessSpec& lhs, const AccessSpec& rhs)
    {
        return AccessConflict::None != conflict_between(lhs, rhs);
    }

    template <typename Component>
    TypeId component_type_id()
    {
        return ecs::component_type_id<Component>();
    }

    template <typename Component>
    std::string component_type_name()
    {
        return ecs::component_type_name<Component>();
    }

    inline void append_unique_access(
        ArrayList<TypeId>& access,
        ArrayList<std::string>& names,
        const TypeId id,
        std::string name
    ) {
        if (!access.contains(id))
        {
            access.append(id);
            names.append(std::move(name));
        }
    }

    template <typename Component>
    void append_read(AccessSpec& spec)
    {
        append_unique_access(
            spec.reads,
            spec.readNames,
            component_type_id<Component>(),
            component_type_name<Component>()
        );
    }

    template <typename Component>
    void append_write(AccessSpec& spec)
    {
        append_unique_access(
            spec.writes,
            spec.writeNames,
            component_type_id<Component>(),
            component_type_name<Component>()
        );
    }

    template <typename Component>
    void append_exclusive_write(AccessSpec& spec)
    {
        append_unique_access(
            spec.exclusiveWrites,
            spec.exclusiveWriteNames,
            component_type_id<Component>(),
            component_type_name<Component>()
        );
    }

    template <typename Component>
    void append_guaranteed_dirty_access(AccessSpec& spec)
    {
        append_read<Component>(spec);
        append_write<Component>(spec);

        if constexpr (!ecs::is_buffered_component_v<Component>)
            append_exclusive_write<Component>(spec);
    }

    template <typename... Components>
    void append_guaranteed_dirty_access(AccessSpec& spec, type_list<Components...>)
    {
        (append_guaranteed_dirty_access<Components>(spec), ...);
    }

    template <typename T>
    void append_access(AccessSpec& spec)
    {
        using Arg = std::remove_cvref_t<T>;

        if constexpr (is_view_of_v<Arg>)
        {
            using Component = std::remove_cvref_t<typename Arg::component_type>;
            append_read<Component>(spec);
        }
        else if constexpr (is_plain_component_arg_v<T>)
        {
            using Component = std::remove_cvref_t<T>;

            if constexpr (is_const_component_arg_v<T>)
            {
                append_read<Component>(spec);
            }
            else
            {
                append_read<Component>(spec);
                append_write<Component>(spec);

                if constexpr (!ecs::is_buffered_component_v<Component>)
                    append_exclusive_write<Component>(spec);
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
        else if constexpr (is_view_of_param_v<Param>)
        {
            using Component = view_of_param_component_t<Param>;
            append_read<Component>(spec);
        }
        else
        {
            using Component = Arg;

            if constexpr (std::is_const_v<std::remove_reference_t<Param>>)
            {
                append_read<Component>(spec);
            }
            else
            {
                append_read<Component>(spec);
                append_write<Component>(spec);

                if constexpr (!ecs::is_buffered_component_v<Component>)
                    append_exclusive_write<Component>(spec);
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

    template <typename Callable, typename DirtyComponents>
    AccessSpec build_callable_access_spec()
    {
        AccessSpec spec = build_callable_access_spec<Callable>();
        append_guaranteed_dirty_access(spec, DirtyComponents{});
        return spec;
    }

    template <typename... Args>
    AccessSpec build_access_spec()
    {
        AccessSpec spec;
        (append_access<Args>(spec), ...);
        return spec;
    }
}
