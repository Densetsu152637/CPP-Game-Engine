//
// ECS job argument materialisation and job creation.
//

#pragma once

#include <functional>
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
            return std::as_const(ecs.template denseComponents<Component>());
        }
        else
        {
            static_assert(sizeof(Arg) == 0, "Global ECSProcessor jobs only support ArrayFor<T> arguments");
        }
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
        else if constexpr (is_plain_component_arg_v<Arg>)
        {
            using Component = std::remove_cvref_t<Arg>;
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
            else if constexpr (is_plain_component_arg_v<Arg>)
                return component;
            else
                static_assert(sizeof(Arg) == 0, "Unsupported ECSProcessor argument");
        }
    }

    template <typename Callable, typename... Args, size_t... Is>
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
    }

    template <typename Callable, typename... Args>
    void invoke_for_entity(Callable& callable, ECS& ecs, const Entity& entity)
    {
        invoke_for_entity_impl<Callable, Args...>(
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

    template <typename Callable, typename... Args, typename... Components, typename ComponentTuple>
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
    }

    template <typename Callable, typename... Args>
    void run_no_component_job(ECS& ecs, Callable& callable)
    {
        if constexpr (sizeof...(Args) == 0)
        {
            std::invoke(callable);
        }
        else if constexpr (has_entity_arg_pack_v<Args...>)
        {
            auto view = ecs.view<>();
            view.each([&](const Entity& entity)
            {
                invoke_for_entity<Callable, Args...>(callable, ecs, entity);
            });
        }
        else
        {
            invoke_global_job<Callable, Args...>(callable, ecs);
        }
    }

    template <typename Callable, typename... Args>
    void run_component_job(ECS& ecs, Callable& callable, type_list<>)
    {
        run_no_component_job<Callable, Args...>(ecs, callable);
    }

    template <typename Callable, typename... Args, typename... Components>
    void run_component_job(ECS& ecs, Callable& callable, type_list<Components...>)
    {
        auto view = ecs.view<Components...>();
        view.each([&](const Entity& entity, ecs::component_value_t<Components>&... components)
        {
            auto componentTuple = std::forward_as_tuple(components...);
            invoke_for_entity_with_components(
                ecs,
                callable,
                type_list<Args...>{},
                type_list<Components...>{},
                entity,
                componentTuple
            );
        });
    }

    template <typename Callable, typename... Args>
    void run_job(ECS& ecs, Callable& callable)
    {
        using Components = component_list_t<Args...>;
        run_component_job<Callable, Args...>(ecs, callable, Components{});
    }

    template <typename... Args, typename Callable>
    Job make_job(Callable&& callable)
    {
        static_assert(
            (... && is_supported_submit_arg_v<Args>),
            "ECSProcessor submit arguments must be component types, ArrayFor<T>, or Entity"
        );

        using DecayedCallable = std::decay_t<Callable>;
        using CallableArgs = callable_arg_list_t<DecayedCallable>;

        static_assert(
            is_callable_inspectable_v<DecayedCallable>,
            "ECSProcessor simulation callables must have a non-generic, inspectable operator() so access can be derived from parameter constness"
        );

        static_assert(
            callable_arity_v<DecayedCallable> == sizeof...(Args),
            "ECSProcessor simulation callable parameter count must match the submitted argument count"
        );

        static_assert(
            valid_callable_params_v<CallableArgs>,
            "ECSProcessor simulation callable parameters must be Entity, component lvalue references, or const ArrayList<T>& for ArrayFor<T>"
        );

        static_assert(
            std::is_invocable_v<
                DecayedCallable&,
                std::add_lvalue_reference_t<call_arg_t<Args>>...
            >,
            "ECSProcessor submit callable is not invocable with the provided component parameters"
        );

        Job job;
        job.access = build_callable_access_spec<DecayedCallable>();
        job.run = [callable = DecayedCallable(std::forward<Callable>(callable))](ECS& ecs) mutable
        {
            run_job<DecayedCallable, Args...>(ecs, callable);
        };
        return job;
    }

    template <typename... Args, typename Callable>
    SimulationJob make_sim_job(Callable&& callable)
    {
        return make_job<Args...>(std::forward<Callable>(callable));
    }

    template <typename... Components, typename Callable>
    RenderJob make_render_job(Callable&& callable)
    {
        static_assert(
            (... && is_plain_component_arg_v<Components>),
            "ECSProcessor render arguments must be plain component types"
        );

        using DecayedCallable = std::decay_t<Callable>;
        static_assert(
            std::is_invocable_v<
                DecayedCallable&,
                const ecs::component_value_t<Components>&...
            >,
            "ECSProcessor render callable is not invocable with const component references"
        );

        RenderJob job;
        job.run = [callable = DecayedCallable(std::forward<Callable>(callable))](ECS& ecs) mutable
        {
            auto view = ecs.render_view<Components...>();
            view.each([&](const ecs::component_value_t<Components>&... components)
            {
                std::invoke(callable, components...);
            });
        };
        return job;
    }
}
