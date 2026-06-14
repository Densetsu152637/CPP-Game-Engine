//
// Compile-time argument classification for ECS simulation/render jobs.
//

#pragma once

#include <functional>
#include <type_traits>

#include "../ecs.h"
#include "ecs_sim_types.h"

namespace ecs_sim
{
    template <typename... Ts>
    struct type_list
    {};

    template <typename T>
    struct is_entity_arg : std::bool_constant<std::is_same_v<std::remove_cvref_t<T>, Entity>>
    {};

    template <typename T>
    inline constexpr bool is_entity_arg_v = is_entity_arg<T>::value;

    template <typename T>
    struct is_view_of : std::false_type
    {};

    template <typename... Components>
    struct is_view_of<ecs::ViewOf<Components...>> : std::true_type
    {};

    template <typename T>
    inline constexpr bool is_view_of_v = is_view_of<std::remove_cvref_t<T>>::value;

    template <typename T>
    struct is_dirty_component_arg : std::false_type
    {};

    template <typename T>
    struct is_dirty_component_arg<ecs::Dirty<T>> : std::true_type
    {};

    template <typename T>
    inline constexpr bool is_dirty_component_arg_v =
        is_dirty_component_arg<std::remove_cvref_t<T>>::value;

    template <typename T>
    struct is_tag_arg : std::false_type
    {};

    template <typename T>
    struct is_tag_arg<ecs::Tag<T>> : std::true_type
    {};

    template <typename T>
    inline constexpr bool is_tag_arg_v = is_tag_arg<std::remove_cvref_t<T>>::value;

    template <typename T>
    struct is_exclude_arg : std::false_type
    {};

    template <typename T>
    struct is_exclude_arg<ecs::Exclude<T>> : std::true_type
    {};

    template <typename T>
    inline constexpr bool is_exclude_arg_v = is_exclude_arg<std::remove_cvref_t<T>>::value;

    template <typename T>
    struct is_shared_arg : std::false_type
    {};

    template <typename T>
    struct is_shared_arg<ecs::Shared<T>> : std::true_type
    {};

    template <typename T>
    inline constexpr bool is_shared_arg_v = is_shared_arg<std::remove_cvref_t<T>>::value;

    template <typename T>
    inline constexpr bool is_wrapped_component_arg_v =
        is_dirty_component_arg_v<T>;

    template <typename T>
    inline constexpr bool is_plain_component_arg_v =
        !is_entity_arg_v<T> &&
        !is_view_of_v<T> &&
        !is_tag_arg_v<T> &&
        !is_exclude_arg_v<T> &&
        !is_shared_arg_v<T> &&
        !is_wrapped_component_arg_v<T>;

    template <typename T>
    inline constexpr bool is_component_submit_arg_v =
        is_plain_component_arg_v<T> ||
        is_wrapped_component_arg_v<T>;

    template <typename T>
    inline constexpr bool is_const_component_arg_v =
        is_plain_component_arg_v<T> &&
        std::is_const_v<std::remove_reference_t<T>>;

    template <typename T>
    inline constexpr bool is_supported_submit_arg_v =
        is_entity_arg_v<T> ||
        is_view_of_v<T> ||
        is_tag_arg_v<T> ||
        is_exclude_arg_v<T> ||
        is_shared_arg_v<T> ||
        is_component_submit_arg_v<T>;

    template <typename T>
    struct component_for_arg
    {
        using type = std::conditional_t<
            is_plain_component_arg_v<T>,
            std::remove_cvref_t<T>,
            void
        >;
    };

    template <typename... Components>
    struct component_for_arg<ecs::ViewOf<Components...>>
    {
        using type = void;
    };

    template <typename T>
    struct component_for_arg<ecs::Exclude<T>>
    {
        using type = void;
    };

    template <typename T>
    struct component_for_arg<ecs::Dirty<T>>
    {
        using type = std::remove_cvref_t<T>;
    };

    template <typename T>
    using component_for_arg_t = typename component_for_arg<std::remove_cvref_t<T>>::type;

    template <typename T>
    struct shared_component_for_arg
    {
        using type = void;
    };

    template <typename T>
    struct shared_component_for_arg<ecs::Shared<T>>
    {
        static_assert(
            ecs::is_shared_component_alias_v<std::remove_cvref_t<T>>,
            "ecs::Shared<T> can only wrap ecs::SharedAlias component types"
        );

        using type = std::remove_cvref_t<T>;
    };

    template <typename T>
    using shared_component_for_arg_t =
        typename shared_component_for_arg<std::remove_cvref_t<T>>::type;

    template <typename T>
    struct render_component_for_arg
    {
        using shared_component = shared_component_for_arg_t<T>;
        using type = std::conditional_t<
            std::is_void_v<shared_component>,
            component_for_arg_t<T>,
            shared_component
        >;
    };

    template <typename T>
    using render_component_for_arg_t =
        typename render_component_for_arg<std::remove_cvref_t<T>>::type;

    template <typename T>
    struct simulation_match_component_for_arg
    {
        using shared_component = shared_component_for_arg_t<T>;
        using type = std::conditional_t<
            std::is_void_v<shared_component>,
            component_for_arg_t<T>,
            shared_component
        >;
    };

    template <typename T>
    using simulation_match_component_for_arg_t =
        typename simulation_match_component_for_arg<std::remove_cvref_t<T>>::type;

    template <typename T>
    using simulation_component_for_arg_t = simulation_match_component_for_arg_t<T>;

    template <typename T>
    struct view_component_list_for_arg
    {
        using type = type_list<>;
    };

    template <typename... Components>
    struct view_component_list_for_arg<ecs::ViewOf<Components...>>
    {
        using type = type_list<std::remove_cvref_t<Components>...>;
    };

    template <typename T>
    using view_component_list_for_arg_t = typename view_component_list_for_arg<std::remove_cvref_t<T>>::type;

    template <typename T>
    struct tag_for_arg
    {
        using type = void;
    };

    template <typename T>
    struct tag_for_arg<ecs::Tag<T>>
    {
        using type = ecs::tag_name_t<T>;
    };

    template <typename T>
    using tag_for_arg_t = typename tag_for_arg<std::remove_cvref_t<T>>::type;

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

    template <typename Arg, typename... Components>
    struct call_arg<Arg, ecs::ViewOf<Components...>>
    {
        using type = View<ecs::component_value_t<Components>...>;
    };

    template <typename Arg, typename T>
    struct call_arg<Arg, ecs::Dirty<T>>
    {
        using type = ecs::component_value_t<T>&;
    };

    template <typename Arg>
    using call_arg_t = typename call_arg<Arg>::type;

    template <typename T>
    struct callable_traits
    {
        static constexpr bool is_inspectable = false;
    };

    template <typename R, typename... Args>
    struct callable_traits<R(*)(Args...)>
    {
        static constexpr bool is_inspectable = true;
        using arg_types = type_list<Args...>;
        static constexpr size_t arity = sizeof...(Args);
    };

    template <typename R, typename... Args>
    struct callable_traits<R(&)(Args...)> : callable_traits<R(*)(Args...)>
    {};

    template <typename R, typename... Args>
    struct callable_traits<std::function<R(Args...)>> : callable_traits<R(*)(Args...)>
    {};

    template <typename Class, typename R, typename... Args>
    struct callable_traits<R(Class::*)(Args...)> : callable_traits<R(*)(Args...)>
    {};

    template <typename Class, typename R, typename... Args>
    struct callable_traits<R(Class::*)(Args...) const> : callable_traits<R(*)(Args...)>
    {};

    template <typename T, bool Inspectable = callable_traits<std::remove_cvref_t<T>>::is_inspectable>
    struct callable_non_object_traits
    {
        static constexpr bool is_inspectable = false;
        using arg_types = type_list<>;
        static constexpr size_t arity = 0;
    };

    template <typename T>
    struct callable_non_object_traits<T, true> : callable_traits<std::remove_cvref_t<T>>
    {};

    template <typename T, typename = void>
    struct callable_object_traits : callable_non_object_traits<T>
    {
    };

    template <typename T>
    struct callable_object_traits<T, std::void_t<decltype(&std::remove_cvref_t<T>::operator())>>
        : callable_traits<decltype(&std::remove_cvref_t<T>::operator())>
    {};

    template <typename T>
    inline constexpr bool is_callable_inspectable_v = callable_object_traits<T>::is_inspectable;

    template <typename T>
    using callable_arg_list_t = typename callable_object_traits<T>::arg_types;

    template <typename T>
    inline constexpr size_t callable_arity_v = callable_object_traits<T>::arity;

    template <typename T>
    struct view_param_component_list
    {
        using type = type_list<>;
    };

    template <typename... Components>
    struct view_param_component_list<View<Components...>>
    {
        using type = type_list<std::remove_cvref_t<Components>...>;
    };

    template <typename T>
    using view_param_component_list_t = typename view_param_component_list<std::remove_cvref_t<T>>::type;

    template <typename List>
    struct type_list_empty;

    template <typename... Components>
    struct type_list_empty<type_list<Components...>> : std::bool_constant<0 == sizeof...(Components)>
    {};

    template <typename T>
    inline constexpr bool is_view_param_v =
        !type_list_empty<view_param_component_list_t<T>>::value;

    template <typename T>
    inline constexpr bool is_const_lvalue_ref_v =
        std::is_lvalue_reference_v<T> &&
        std::is_const_v<std::remove_reference_t<T>>;

    template <typename T>
    inline constexpr bool is_valid_callable_param_v =
        is_entity_arg_v<T> ||
        (
            is_view_param_v<T> &&
            is_const_lvalue_ref_v<T>
        ) ||
        (
            !is_view_param_v<T> &&
            is_plain_component_arg_v<T> &&
            std::is_lvalue_reference_v<T>
        );

    template <typename List>
    struct valid_callable_params;

    template <typename... Args>
    struct valid_callable_params<type_list<Args...>>
        : std::bool_constant<(... && is_valid_callable_param_v<Args>)>
    {};

    template <typename List>
    inline constexpr bool valid_callable_params_v = valid_callable_params<List>::value;

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

    template <typename List, typename... Args>
    struct collect_simulation_match_components;

    template <typename List>
    struct collect_simulation_match_components<List>
    {
        using type = List;
    };

    template <typename List, typename Arg, typename... Rest>
    struct collect_simulation_match_components<List, Arg, Rest...>
    {
        using component = simulation_match_component_for_arg_t<Arg>;
        using next = std::conditional_t<
            std::is_void_v<component>,
            List,
            typename push_unique_type<List, component>::type
        >;
        using type = typename collect_simulation_match_components<next, Rest...>::type;
    };

    template <typename... Args>
    using simulation_match_component_list_t =
        typename collect_simulation_match_components<type_list<>, Args...>::type;

    template <typename List, typename... Args>
    struct collect_render_match_components;

    template <typename List>
    struct collect_render_match_components<List>
    {
        using type = List;
    };

    template <typename List, typename Arg, typename... Rest>
    struct collect_render_match_components<List, Arg, Rest...>
    {
        using component = render_component_for_arg_t<Arg>;
        using next = std::conditional_t<
            std::is_void_v<component>,
            List,
            typename push_unique_type<List, component>::type
        >;
        using type = typename collect_render_match_components<next, Rest...>::type;
    };

    template <typename... Args>
    using render_match_component_list_t =
        typename collect_render_match_components<type_list<>, Args...>::type;

    template <typename List, typename... Args>
    struct collect_callable_submit_args;

    template <typename List>
    struct collect_callable_submit_args<List>
    {
        using type = List;
    };

    template <typename List, typename Arg, typename... Rest>
    struct collect_callable_submit_args<List, Arg, Rest...>
    {
        using shared_component = shared_component_for_arg_t<Arg>;
        using callable_arg = std::conditional_t<
            std::is_void_v<shared_component>,
            Arg,
            shared_component
        >;
        using next = std::conditional_t<
            is_tag_arg_v<Arg> || is_exclude_arg_v<Arg>,
            List,
            typename push_type<List, callable_arg>::type
        >;
        using type = typename collect_callable_submit_args<next, Rest...>::type;
    };

    template <typename... Args>
    using callable_submit_arg_list_t = typename collect_callable_submit_args<type_list<>, Args...>::type;

    template <typename List, typename... Args>
    struct collect_tags;

    template <typename List>
    struct collect_tags<List>
    {
        using type = List;
    };

    template <typename List, typename Arg, typename... Rest>
    struct collect_tags<List, Arg, Rest...>
    {
        using tag = tag_for_arg_t<Arg>;
        using next = std::conditional_t<
            std::is_void_v<tag>,
            List,
            typename push_unique_type<List, tag>::type
        >;
        using type = typename collect_tags<next, Rest...>::type;
    };

    template <typename... Args>
    using tag_list_t = typename collect_tags<type_list<>, Args...>::type;

    template <typename List, typename... Args>
    struct collect_shared_components;

    template <typename List>
    struct collect_shared_components<List>
    {
        using type = List;
    };

    template <typename List, typename Arg, typename... Rest>
    struct collect_shared_components<List, Arg, Rest...>
    {
        using component = shared_component_for_arg_t<Arg>;
        using next = std::conditional_t<
            std::is_void_v<component>,
            List,
            typename push_unique_type<List, component>::type
        >;
        using type = typename collect_shared_components<next, Rest...>::type;
    };

    template <typename... Args>
    using shared_component_list_t =
        typename collect_shared_components<type_list<>, Args...>::type;

    template <typename List, typename... Args>
    struct collect_excludes;

    template <typename List>
    struct collect_excludes<List>
    {
        using type = List;
    };

    template <typename List, typename Arg, typename... Rest>
    struct collect_excludes<List, Arg, Rest...>
    {
        using CleanArg = std::remove_cvref_t<Arg>;
        using next = std::conditional_t<
            is_exclude_arg_v<CleanArg>,
            typename push_unique_type<List, CleanArg>::type,
            List
        >;
        using type = typename collect_excludes<next, Rest...>::type;
    };

    template <typename... Args>
    using exclude_list_t = typename collect_excludes<type_list<>, Args...>::type;

    template <typename List, typename Arg>
    struct append_guaranteed_dirty_component
    {
        using type = List;
    };

    template <typename List, typename Component>
    struct append_guaranteed_dirty_component<List, ecs::Dirty<Component>>
    {
        using type = typename push_unique_type<List, std::remove_cvref_t<Component>>::type;
    };

    template <typename List, typename... Args>
    struct collect_guaranteed_dirty_components;

    template <typename List>
    struct collect_guaranteed_dirty_components<List>
    {
        using type = List;
    };

    template <typename List, typename Arg, typename... Rest>
    struct collect_guaranteed_dirty_components<List, Arg, Rest...>
    {
        using next = typename append_guaranteed_dirty_component<List, std::remove_cvref_t<Arg>>::type;
        using type = typename collect_guaranteed_dirty_components<next, Rest...>::type;
    };

    template <typename... Args>
    using guaranteed_dirty_component_list_t =
        typename collect_guaranteed_dirty_components<type_list<>, Args...>::type;

    template <typename List>
    struct type_list_size;

    template <typename... Args>
    struct type_list_size<type_list<Args...>> : std::integral_constant<size_t, sizeof...(Args)>
    {};

    template <typename List>
    inline constexpr size_t type_list_size_v = type_list_size<List>::value;

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

    template <typename... Args>
    inline constexpr bool has_tag_arg_pack_v = (... || is_tag_arg_v<Args>);

    template <typename... Args>
    inline constexpr bool has_exclude_arg_pack_v = (... || is_exclude_arg_v<Args>);

    template <typename... Args>
    inline constexpr bool has_shared_arg_pack_v = (... || is_shared_arg_v<Args>);

    template <typename... Args>
    inline constexpr bool has_filter_arg_pack_v =
        has_tag_arg_pack_v<Args...> || has_exclude_arg_pack_v<Args...> || has_shared_arg_pack_v<Args...>;
}
