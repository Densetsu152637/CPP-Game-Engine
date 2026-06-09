//
// ECS job argument materialisation and job creation.
//

#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

#include "ecs_sim_access.h"

namespace ecs_sim
{
    template <typename Arg>
    decltype(auto) make_global_argument(ECS& ecs)
    {
        using Decayed = std::remove_cvref_t<Arg>;

        if constexpr (is_array_for_v<Decayed>)
        {
            using Component = std::remove_cvref_t<typename Decayed::component_type>;
            return std::as_const(ecs).template denseComponents<Component>();
        }
        else
        {
            static_assert(sizeof(Arg) == 0, "Global ECSProcessor jobs only support ArrayFor<T> arguments");
        }
    }

    template <typename Component, typename DirtyComponents>
    inline constexpr bool is_guaranteed_dirty_component_v =
        contains_type<DirtyComponents, std::remove_cvref_t<Component>>::value;

    template <typename Component>
    void mark_guaranteed_dirty_component(ECS& ecs)
    {
        ecs.template markComponentDirty<Component>();
    }

    template <typename... Components>
    void mark_guaranteed_dirty_components(ECS& ecs, type_list<Components...>)
    {
        (mark_guaranteed_dirty_component<Components>(ecs), ...);
    }

    template <typename Param, typename DirtyComponents>
    void mark_callable_param_entity_dirty(ECS& ecs, const size_t entityIndex)
    {
        using Arg = std::remove_cvref_t<Param>;

        if constexpr (
            !is_entity_arg_v<Arg> &&
            !is_array_list_param_v<Param> &&
            is_plain_component_arg_v<Arg> &&
            !std::is_const_v<std::remove_reference_t<Param>> &&
            !is_guaranteed_dirty_component_v<Arg, DirtyComponents>
        ) {
            ecs.template markComponentEntityDirty<Arg>(entityIndex);
        }
    }

    template <typename DirtyComponents, typename... Params>
    void mark_entity_dirty_for_writes(ECS& ecs, const size_t entityIndex, type_list<Params...>)
    {
        (mark_callable_param_entity_dirty<Params, DirtyComponents>(ecs, entityIndex), ...);
    }

    template <typename Callable, typename DirtyComponents>
    void mark_entity_dirty_for_writes(ECS& ecs, const size_t entityIndex)
    {
        mark_entity_dirty_for_writes<DirtyComponents>(ecs, entityIndex, callable_arg_list_t<Callable>{});
    }

    template <typename Arg>
    decltype(auto) make_argument(ECS& ecs, const Entity& entity)
    {
        using Decayed = std::remove_cvref_t<Arg>;

        if constexpr (is_entity_arg_v<Decayed>)
        {
            return entity;
        }
        else if constexpr (is_array_for_v<Decayed>)
        {
            return make_global_argument<Arg>(ecs);
        }
        else if constexpr (is_component_submit_arg_v<Arg>)
        {
            using Component = component_for_arg_t<Arg>;
            auto* component = ecs.try_get<Component>(entity);
            if (nullptr == component)
                throw std::out_of_range("Missing component for plain component argument");

            if constexpr (is_const_component_arg_v<Arg>)
                return static_cast<const ecs::component_value_t<Component>&>(*component);
            else
                return *component;
        }
        else
        {
            static_assert(sizeof(Arg) == 0, "Unsupported ECSProcessor argument");
        }
    }

    template <typename Arg, typename... Components, typename ComponentTuple>
    decltype(auto) make_argument_from_components(ECS& ecs, const Entity& entity, ComponentTuple& components)
    {
        using Decayed = std::remove_cvref_t<Arg>;

        if constexpr (is_entity_arg_v<Decayed>)
        {
            return entity;
        }
        else if constexpr (is_array_for_v<Decayed>)
        {
            return make_global_argument<Arg>(ecs);
        }
        else
        {
            using Component = component_for_arg_t<Decayed>;
            constexpr size_t componentIndex = type_index<Component, Components...>::value;
            auto& component = std::get<componentIndex>(components);

            if constexpr (is_const_component_arg_v<Arg>)
                return static_cast<const ecs::component_value_t<Component>&>(component);
            else if constexpr (is_component_submit_arg_v<Arg>)
                return component;
            else
                static_assert(sizeof(Arg) == 0, "Unsupported ECSProcessor argument");
        }
    }

    template <typename Callable, typename DirtyComponents, typename... Args, size_t... Is>
    void invoke_for_entity_impl(Callable& callable, ECS& ecs, const Entity& entity, std::index_sequence<Is...>)
    {
        using ArgTuple = std::tuple<Args...>;
        std::tuple<call_arg_t<std::tuple_element_t<Is, ArgTuple>>...> args(
            make_argument<std::tuple_element_t<Is, ArgTuple>>(ecs, entity)...
        );

        std::apply(
            [&](auto&... unpacked)
            {
                std::invoke(callable, unpacked...);
            },
            args
        );

        mark_entity_dirty_for_writes<Callable, DirtyComponents>(ecs, entity.index);
    }

    template <typename Callable, typename DirtyComponents, typename... Args>
    void invoke_for_entity(Callable& callable, ECS& ecs, const Entity& entity)
    {
        invoke_for_entity_impl<Callable, DirtyComponents, Args...>(
            callable,
            ecs,
            entity,
            std::make_index_sequence<sizeof...(Args)>{}
        );
    }

    template <typename Callable, typename... Args, size_t... Is>
    void invoke_global_job_impl(Callable& callable, ECS& ecs, std::index_sequence<Is...>)
    {
        using ArgTuple = std::tuple<Args...>;
        std::tuple<call_arg_t<std::tuple_element_t<Is, ArgTuple>>...> args(
            make_global_argument<std::tuple_element_t<Is, ArgTuple>>(ecs)...
        );

        std::apply(
            [&](auto&... unpacked)
            {
                std::invoke(callable, unpacked...);
            },
            args
        );
    }

    template <typename Callable, typename... Args>
    void invoke_global_job(Callable& callable, ECS& ecs)
    {
        invoke_global_job_impl<Callable, Args...>(
            callable,
            ecs,
            std::make_index_sequence<sizeof...(Args)>{}
        );
    }

    template <typename Callable, typename DirtyComponents, typename... Args, typename... Components, typename ComponentTuple>
    void invoke_for_entity_with_components(
        ECS& ecs,
        Callable& callable,
        type_list<Args...>,
        type_list<Components...>,
        const Entity& entity,
        ComponentTuple& components
    ) {
        std::tuple<call_arg_t<Args>...> args(
            make_argument_from_components<Args, Components...>(ecs, entity, components)...
        );

        std::apply(
            [&](auto&... unpacked)
            {
                std::invoke(callable, unpacked...);
            },
            args
        );

        mark_entity_dirty_for_writes<Callable, DirtyComponents>(ecs, entity.index);
    }

    template <typename Callable, typename DirtyComponents, typename... Args>
    void run_no_component_job(ECS& ecs, Threadpool& pool, Callable& callable)
    {
        if constexpr (sizeof...(Args) == 0)
        {
            std::invoke(callable);
        }
        else if constexpr (has_entity_arg_pack_v<Args...>)
        {
            auto view = ecs.view<>();
            view.each_mt([&](const Entity& entity)
            {
                invoke_for_entity<Callable, DirtyComponents, Args...>(callable, ecs, entity);
            }, pool);
        }
        else
        {
            invoke_global_job<Callable, Args...>(callable, ecs);
        }
    }

    template <typename Callable, typename DirtyComponents, typename... Args>
    void run_component_job(ECS& ecs, Threadpool& pool, Callable& callable, type_list<>)
    {
        run_no_component_job<Callable, DirtyComponents, Args...>(ecs, pool, callable);
    }

    template <typename Callable, typename DirtyComponents, typename... Args, typename... Components>
    void run_component_job(ECS& ecs, Threadpool& pool, Callable& callable, type_list<Components...>)
    {
        auto view = ecs.view<Components...>();
        view.each_mt([&](const Entity& entity, ecs::component_value_t<Components>&... components)
        {
            auto componentTuple = std::forward_as_tuple(components...);
            invoke_for_entity_with_components<Callable, DirtyComponents>(
                ecs,
                callable,
                type_list<Args...>{},
                type_list<Components...>{},
                entity,
                componentTuple
            );
        }, pool);
    }

    template <typename Callable, typename DirtyComponents, typename... Args>
    void run_job(ECS& ecs, Threadpool& pool, Callable& callable)
    {
        using Components = component_list_t<Args...>;
        run_component_job<Callable, DirtyComponents, Args...>(ecs, pool, callable, Components{});
        mark_guaranteed_dirty_components(ecs, DirtyComponents{});
    }

}
