//
// Compile-time argument classification for ECS simulation/render jobs.
//

#pragma once

#include <type_traits>

#include "ecs.h"
#include "ecs_sim_types.h"

namespace ecs_sim
{
    template <typename T>
    struct is_entity_arg : std::bool_constant<std::is_same_v<std::remove_cvref_t<T>, Entity>>
    {};

    template <typename T>
    inline constexpr bool is_entity_arg_v = is_entity_arg<T>::value;

    template <typename T>
    struct is_array_for : std::false_type
    {};

    template <typename T>
    struct is_array_for<ArrayFor<T>> : std::true_type
    {};

    template <typename T>
    inline constexpr bool is_array_for_v = is_array_for<std::remove_cvref_t<T>>::value;

    template <typename T>
    inline constexpr bool is_plain_component_arg_v =
        !is_entity_arg_v<T> &&
        !is_array_for_v<T>;

    template <typename T>
    inline constexpr bool is_const_component_arg_v =
        is_plain_component_arg_v<T> &&
        std::is_const_v<std::remove_reference_t<T>>;

    template <typename T>
    inline constexpr bool is_supported_submit_arg_v =
        is_entity_arg_v<T> ||
        is_array_for_v<T> ||
        is_plain_component_arg_v<T>;

    template <typename T>
    struct component_for_arg
    {
        using type = std::conditional_t<
            is_plain_component_arg_v<T>,
            std::remove_cvref_t<T>,
            void
        >;
    };

    template <typename T>
    struct component_for_arg<ArrayFor<T>>
    {
        using type = void;
    };

    template <typename T>
    using component_for_arg_t = typename component_for_arg<T>::type;

    template <typename T>
    struct array_component_for_arg
    {
        using type = void;
    };

    template <typename T>
    struct array_component_for_arg<ArrayFor<T>>
    {
        using type = std::remove_cvref_t<T>;
    };

    template <typename T>
    using array_component_for_arg_t = typename array_component_for_arg<std::remove_cvref_t<T>>::type;

    template <typename Arg, typename Decayed = std::remove_cvref_t<Arg>>
    struct call_arg
    {
        using type = std::conditional_t<
            is_const_component_arg_v<Arg>,
            const ecs::component_value_t<Decayed>&,
            ecs::component_value_t<Decayed>&
        >;
    };

    template <typename Arg>
    struct call_arg<Arg, Entity>
    {
        using type = Entity;
    };

    template <typename Arg, typename T>
    struct call_arg<Arg, ArrayFor<T>>
    {
        using type = const ArrayList<ecs::component_value_t<T>>&;
    };

    template <typename Arg>
    using call_arg_t = typename call_arg<Arg>::type;

    template <typename... Ts>
    struct type_list
    {};

    template <typename List, typename T>
    struct push_type;

    template <typename... Ts, typename T>
    struct push_type<type_list<Ts...>, T>
    {
        using type = type_list<Ts..., T>;
    };

    template <typename List, typename T>
    struct contains_type;

    template <typename T>
    struct contains_type<type_list<>, T> : std::false_type
    {};

    template <typename Head, typename... Tail, typename T>
    struct contains_type<type_list<Head, Tail...>, T>
        : std::bool_constant<std::is_same_v<Head, T> || contains_type<type_list<Tail...>, T>::value>
    {};

    template <typename List, typename T>
    struct push_unique_type
    {
        using type = std::conditional_t<
            contains_type<List, T>::value,
            List,
            typename push_type<List, T>::type
        >;
    };

    template <typename List, typename... Args>
    struct collect_components;

    template <typename List>
    struct collect_components<List>
    {
        using type = List;
    };

    template <typename List, typename Arg, typename... Rest>
    struct collect_components<List, Arg, Rest...>
    {
        using component = component_for_arg_t<Arg>;
        using next = std::conditional_t<
            std::is_void_v<component>,
            List,
            typename push_unique_type<List, component>::type
        >;
        using type = typename collect_components<next, Rest...>::type;
    };

    template <typename... Args>
    using component_list_t = typename collect_components<type_list<>, Args...>::type;

    template <typename T, typename... Ts>
    struct type_index;

    template <typename T, typename... Rest>
    struct type_index<T, T, Rest...> : std::integral_constant<size_t, 0>
    {};

    template <typename T, typename Head, typename... Rest>
    struct type_index<T, Head, Rest...>
        : std::integral_constant<size_t, 1 + type_index<T, Rest...>::value>
    {};

    template <typename... Args>
    inline constexpr bool has_entity_arg_pack_v = (... || is_entity_arg_v<Args>);
}
