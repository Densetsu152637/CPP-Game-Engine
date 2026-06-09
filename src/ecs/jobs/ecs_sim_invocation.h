//
// ECS job argument materialisation and job creation.
//

#pragma once

#include <array>
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
    struct ViewOfArgumentFactory;

    template <typename... Components>
    struct ViewOfArgumentFactory<ecs::ViewOf<Components...>>
    {
        static auto make(ECS& ecs)
        {
            return ecs.template view<ecs::component_value_t<Components>...>();
        }
    };

    template <typename Arg>
    decltype(auto) make_global_argument(ECS& ecs)
    {
        using Decayed = std::remove_cvref_t<Arg>;

        if constexpr (is_view_of_v<Decayed>)
        {
            return ViewOfArgumentFactory<Decayed>::make(ecs);
        }
        else
        {
            static_assert(sizeof(Arg) == 0, "Global ECSProcessor jobs only support ecs::ViewOf<T> arguments");
        }
    }

    template <typename Component, typename DirtyComponents>
    inline constexpr bool is_guaranteed_dirty_component_v =
        contains_type<DirtyComponents, std::remove_cvref_t<Component>>::value;

    template <typename DirtyComponents>
    struct DirtyTrackingState;

    template <typename... Components>
    struct DirtyTrackingState<type_list<Components...>>
    {
        std::array<bool, sizeof...(Components)> preMarkedFull {};

        template <typename Component>
        bool component_pre_marked_full() const
        {
            using CleanComponent = std::remove_cvref_t<Component>;

            if constexpr (contains_type<type_list<Components...>, CleanComponent>::value)
                return preMarkedFull[type_index<CleanComponent, Components...>::value];
            else
                return false;
        }
    };

    template <typename Component>
    bool begin_guaranteed_dirty_component(ECS& ecs, const size_t touchedEntityCount)
    {
        return ecs.template markComponentDirtyIfEntityCountReachesThreshold<Component>(touchedEntityCount);
    }

    template <typename... Components>
    DirtyTrackingState<type_list<Components...>> begin_guaranteed_dirty_components(
        ECS& ecs,
        const size_t touchedEntityCount,
        type_list<Components...>
    ) {
        DirtyTrackingState<type_list<Components...>> state;
        state.preMarkedFull = {
            begin_guaranteed_dirty_component<Components>(ecs, touchedEntityCount)...
        };
        return state;
    }

    template <typename Component>
    void mark_guaranteed_dirty_entity_component(ECS& ecs, const size_t entityIndex, const auto& dirtyState)
    {
        if (!dirtyState.template component_pre_marked_full<Component>())
            ecs.template markComponentEntityDirty<Component>(entityIndex);
    }

    template <typename... Components>
    void mark_guaranteed_dirty_entity_components(
        ECS& ecs,
        const size_t entityIndex,
        type_list<Components...>,
        const auto& dirtyState
    ) {
        (mark_guaranteed_dirty_entity_component<Components>(ecs, entityIndex, dirtyState), ...);
    }

    template <typename Param, typename DirtyComponents>
    void mark_callable_param_entity_dirty(ECS& ecs, const size_t entityIndex, const auto& dirtyState)
    {
        using Arg = std::remove_cvref_t<Param>;

        if constexpr (
            !is_entity_arg_v<Arg> &&
            !is_view_param_v<Param> &&
            is_plain_component_arg_v<Arg> &&
            !std::is_const_v<std::remove_reference_t<Param>> &&
            !is_guaranteed_dirty_component_v<Arg, DirtyComponents>
        ) {
            if (!dirtyState.template component_pre_marked_full<Arg>())
                ecs.template markComponentEntityDirty<Arg>(entityIndex);
        }
    }

    template <typename DirtyComponents, typename... Params>
    void mark_entity_dirty_for_writes(
        ECS& ecs,
        const size_t entityIndex,
        type_list<Params...>,
        const auto& dirtyState
    ) {
        mark_guaranteed_dirty_entity_components(ecs, entityIndex, DirtyComponents{}, dirtyState);
        (mark_callable_param_entity_dirty<Params, DirtyComponents>(ecs, entityIndex, dirtyState), ...);
    }

    template <typename Callable, typename DirtyComponents>
    void mark_entity_dirty_for_writes(ECS& ecs, const size_t entityIndex, const auto& dirtyState)
    {
        mark_entity_dirty_for_writes<DirtyComponents>(
            ecs,
            entityIndex,
            callable_arg_list_t<Callable>{},
            dirtyState
        );
    }

    template <typename Arg>
    decltype(auto) make_argument(ECS& ecs, const Entity& entity)
    {
        using Decayed = std::remove_cvref_t<Arg>;

        if constexpr (is_entity_arg_v<Decayed>)
        {
            return entity;
        }
        else if constexpr (is_view_of_v<Decayed>)
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
        else if constexpr (is_view_of_v<Decayed>)
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
    void invoke_for_entity_impl(
        Callable& callable,
        ECS& ecs,
        const Entity& entity,
        const auto& dirtyState,
        std::index_sequence<Is...>
    )
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

        mark_entity_dirty_for_writes<Callable, DirtyComponents>(ecs, entity.index, dirtyState);
    }

    template <typename Callable, typename DirtyComponents, typename... Args>
    void invoke_for_entity(Callable& callable, ECS& ecs, const Entity& entity, const auto& dirtyState)
    {
        invoke_for_entity_impl<Callable, DirtyComponents, Args...>(
            callable,
            ecs,
            entity,
            dirtyState,
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
        ComponentTuple& components,
        const auto& dirtyState
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

        mark_entity_dirty_for_writes<Callable, DirtyComponents>(ecs, entity.index, dirtyState);
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
            const size_t touchedEntityCount = view.size();
            auto dirtyState = begin_guaranteed_dirty_components(ecs, touchedEntityCount, DirtyComponents{});
            view.each_mt([&](const Entity& entity)
            {
                invoke_for_entity<Callable, DirtyComponents, Args...>(callable, ecs, entity, dirtyState);
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
        const size_t touchedEntityCount = view.size();
        auto dirtyState = begin_guaranteed_dirty_components(ecs, touchedEntityCount, DirtyComponents{});
        view.each_mt([&](const Entity& entity, ecs::component_value_t<Components>&... components)
        {
            auto componentTuple = std::forward_as_tuple(components...);
            invoke_for_entity_with_components<Callable, DirtyComponents>(
                ecs,
                callable,
                type_list<Args...>{},
                type_list<Components...>{},
                entity,
                componentTuple,
                dirtyState
            );
        }, pool);
    }

    template <typename Callable, typename DirtyComponents, typename... Args>
    void run_job(ECS& ecs, Threadpool& pool, Callable& callable)
    {
        using Components = component_list_t<Args...>;
        run_component_job<Callable, DirtyComponents, Args...>(ecs, pool, callable, Components{});
    }

}
