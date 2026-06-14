//
// Created by Nicholas on 01/06/26.
//

#pragma once

#include <type_traits>

namespace ecs
{
    namespace detail
    {
        template <typename T, typename = void>
        struct alias_value
        {
            using type = std::remove_cvref_t<T>;
        };

        template <typename T>
        struct alias_value<
            T,
            std::void_t<typename std::remove_cvref_t<T>::ecs_component_value_type>
        >
        {
            using type = std::remove_cvref_t<typename std::remove_cvref_t<T>::ecs_component_value_type>;
        };

        template <typename T, typename = void>
        struct is_component_alias : std::false_type
        {};

        template <typename T>
        struct is_component_alias<
            T,
            std::void_t<typename std::remove_cvref_t<T>::ecs_component_alias_tag>
        > : std::true_type
        {};

        template <typename T, typename = void>
        struct is_buffered_component : std::false_type
        {};

        template <typename T>
        struct is_buffered_component<
            T,
            std::void_t<typename std::remove_cvref_t<T>::ecs_buffered_component_tag>
        > : std::true_type
        {};

        template <typename T, typename = void>
        struct is_shared_component_alias : std::false_type
        {};

        template <typename T>
        struct is_shared_component_alias<
            T,
            std::void_t<typename std::remove_cvref_t<T>::ecs_shared_component_alias_tag>
        > : std::true_type
        {};
    }

    template <typename T>
    using component_key_t = std::remove_cvref_t<T>;

    template <typename T>
    using component_value_t = std::remove_cvref_t<T>;

    template <typename T>
    using alias_value_t = typename detail::alias_value<T>::type;

    template <typename T>
    inline constexpr bool is_component_alias_v = detail::is_component_alias<T>::value;

    template <typename T>
    inline constexpr bool is_buffered_component_v = detail::is_buffered_component<T>::value;

    template <typename T>
    inline constexpr bool is_shared_component_alias_v = detail::is_shared_component_alias<T>::value;
}
