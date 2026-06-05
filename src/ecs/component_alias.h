//
// Created by Nicholas on 01/06/26.
//

#pragma once

#include <type_traits>

namespace ecs
{
    template <typename Value, typename Tag>
    struct ComponentAlias
    {
        using ecs_component_value_type = std::remove_cvref_t<Value>;
    };

    template <typename Value, typename Tag>
    using Alias = ComponentAlias<Value, Tag>;

    namespace detail
    {
        template <typename T, typename = void>
        struct component_value
        {
            using type = std::remove_cvref_t<T>;
        };

        template <typename T>
        struct component_value<
            T,
            std::void_t<typename std::remove_cvref_t<T>::ecs_component_value_type>
        >
        {
            using type = std::remove_cvref_t<typename std::remove_cvref_t<T>::ecs_component_value_type>;
        };
    }

    template <typename T>
    using component_key_t = std::remove_cvref_t<T>;

    template <typename T>
    using component_value_t = typename detail::component_value<T>::type;
}
