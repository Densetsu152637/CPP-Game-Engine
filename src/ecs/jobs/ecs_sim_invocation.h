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
    struct NoGlobalArgument {};

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

    template <typename Arg>
    using global_argument_storage_t = std::conditional_t<
        is_view_of_v<std::remove_cvref_t<Arg>>,
        call_arg_t<Arg>,
        NoGlobalArgument
    >;

    template <typename Arg>
    global_argument_storage_t<Arg> make_global_argument_storage(ECS& ecs)
    {
        if constexpr (is_view_of_v<std::remove_cvref_t<Arg>>)
            return make_global_argument<Arg>(ecs);
        else
            return {};
    }

    template <typename... Args, size_t... Is>
    auto make_global_arguments_impl(ECS& ecs, std::index_sequence<Is...>)
    {
        using ArgTuple = std::tuple<Args...>;
        return std::tuple<global_argument_storage_t<std::tuple_element_t<Is, ArgTuple>>...>(
            make_global_argument_storage<std::tuple_element_t<Is, ArgTuple>>(ecs)...
        );
    }

    template <typename... Args>
    auto make_global_arguments(ECS& ecs)
    {
        return make_global_arguments_impl<Args...>(ecs, std::make_index_sequence<sizeof...(Args)>{});
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

    template <size_t ArgIndex, typename Arg, typename GlobalArguments>
    decltype(auto) make_argument(ECS& ecs, const Entity& entity, GlobalArguments& globalArguments)
    {
        using Decayed = std::remove_cvref_t<Arg>;

        if constexpr (is_entity_arg_v<Decayed>)
        {
            return entity;
        }
        else if constexpr (is_view_of_v<Decayed>)
        {
            (void)ecs;
            return std::get<ArgIndex>(globalArguments);
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

    template <
        size_t ArgIndex,
        typename Arg,
        typename... Components,
        typename ComponentTuple,
        typename GlobalArguments
    >
    decltype(auto) make_argument_from_components(
        ECS& ecs,
        const Entity& entity,
        ComponentTuple& components,
        GlobalArguments& globalArguments
    )
    {
        using Decayed = std::remove_cvref_t<Arg>;

        if constexpr (is_entity_arg_v<Decayed>)
        {
            return entity;
        }
        else if constexpr (is_view_of_v<Decayed>)
        {
            (void)ecs;
            return std::get<ArgIndex>(globalArguments);
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

    template <typename Callable, typename DirtyComponents, typename... Args, typename GlobalArguments, size_t... Is>
    void invoke_for_entity_impl(
        Callable& callable,
        ECS& ecs,
        const Entity& entity,
        const auto& dirtyState,
        GlobalArguments& globalArguments,
        std::index_sequence<Is...>
    )
    {
        using ArgTuple = std::tuple<Args...>;
        std::tuple<call_arg_t<std::tuple_element_t<Is, ArgTuple>>...> args(
            make_argument<Is, std::tuple_element_t<Is, ArgTuple>>(ecs, entity, globalArguments)...
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

    template <typename Callable, typename DirtyComponents, typename... Args, typename GlobalArguments>
    void invoke_for_entity(
        Callable& callable,
        ECS& ecs,
        const Entity& entity,
        const auto& dirtyState,
        GlobalArguments& globalArguments
    )
    {
        invoke_for_entity_impl<Callable, DirtyComponents, Args...>(
            callable,
            ecs,
            entity,
            dirtyState,
            globalArguments,
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

    template <typename... Components, typename... Tags, typename... Excludes>
    ArrayList<Entity> simulation_entities_for(
        ECS& ecs,
        type_list<Components...>,
        type_list<Tags...>,
        type_list<Excludes...>
    )
    {
        return ecs.template matchingEntities<Components..., ecs::Tag<Tags>..., Excludes...>();
    }

    template <typename... Components, typename... Tags, typename... Excludes>
    ArrayList<Entity> rendering_entities_for(
        ECS& ecs,
        type_list<Components...>,
        type_list<Tags...>,
        type_list<Excludes...>
    )
    {
        return ecs.template renderMatchingEntities<Components..., ecs::Tag<Tags>..., Excludes...>();
    }

    template <
        typename Callable,
        typename DirtyComponents,
        typename... Args,
        typename... Components,
        typename ComponentTuple,
        typename GlobalArguments,
        size_t... Is
    >
    void invoke_for_entity_with_components_impl(
        ECS& ecs,
        Callable& callable,
        type_list<Args...>,
        type_list<Components...>,
        const Entity& entity,
        ComponentTuple& components,
        const auto& dirtyState,
        GlobalArguments& globalArguments,
        std::index_sequence<Is...>
    ) {
        using ArgTuple = std::tuple<Args...>;
        std::tuple<call_arg_t<std::tuple_element_t<Is, ArgTuple>>...> args(
            make_argument_from_components<Is, std::tuple_element_t<Is, ArgTuple>, Components...>(
                ecs,
                entity,
                components,
                globalArguments
            )...
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

    template <
        typename Callable,
        typename DirtyComponents,
        typename... Args,
        typename... Components,
        typename ComponentTuple,
        typename GlobalArguments
    >
    void invoke_for_entity_with_components(
        ECS& ecs,
        Callable& callable,
        type_list<Args...>,
        type_list<Components...>,
        const Entity& entity,
        ComponentTuple& components,
        const auto& dirtyState,
        GlobalArguments& globalArguments
    ) {
        invoke_for_entity_with_components_impl<Callable, DirtyComponents>(
            ecs,
            callable,
            type_list<Args...>{},
            type_list<Components...>{},
            entity,
            components,
            dirtyState,
            globalArguments,
            std::make_index_sequence<sizeof...(Args)>{}
        );
    }

    template <typename Callable, typename DirtyComponents, typename... Args, typename... Tags, typename... Excludes>
    void run_no_component_job(
        ECS& ecs,
        Threadpool& pool,
        Callable& callable,
        type_list<Args...>,
        type_list<Tags...> tags,
        type_list<Excludes...> excludes
    )
    {
        if constexpr (sizeof...(Args) == 0 && sizeof...(Tags) == 0 && sizeof...(Excludes) == 0)
        {
            std::invoke(callable);
        }
        else if constexpr (has_entity_arg_pack_v<Args...> || sizeof...(Tags) > 0 || sizeof...(Excludes) > 0)
        {
            ArrayList<Entity> entities = simulation_entities_for(ecs, type_list<>{}, tags, excludes);
            const size_t touchedEntityCount = entities.length();
            auto dirtyState = begin_guaranteed_dirty_components(ecs, touchedEntityCount, DirtyComponents{});
            auto globalArguments = make_global_arguments<Args...>(ecs);
            if (entities.empty())
                return;

            auto executor = [&](const Entity& entity)
            {
                invoke_for_entity<Callable, DirtyComponents, Args...>(
                    callable,
                    ecs,
                    entity,
                    dirtyState,
                    globalArguments
                );
            };

            auto promise = pool.map<Entity, bool>(
                std::function<bool(const Entity&)>([&](const Entity& entity)
                {
                    executor(entity);
                    return true;
                }),
                &entities,
                256
            );

            auto& result = promise.await();
            if (result.is_failure())
                std::rethrow_exception(result.exception());
        }
        else
        {
            invoke_global_job<Callable, Args...>(callable, ecs);
        }
    }

    template <typename Callable, typename DirtyComponents, typename... Args, typename... Tags, typename... Excludes>
    void run_component_job(
        ECS& ecs,
        Threadpool& pool,
        Callable& callable,
        type_list<Args...> callableArgs,
        type_list<>,
        type_list<Tags...> tags,
        type_list<Excludes...> excludes
    )
    {
        run_no_component_job<Callable, DirtyComponents>(
            ecs,
            pool,
            callable,
            callableArgs,
            tags,
            excludes
        );
    }

    template <
        typename Callable,
        typename DirtyComponents,
        typename... Args,
        typename... Components,
        typename... Tags,
        typename... Excludes
    >
    void run_component_job(
        ECS& ecs,
        Threadpool& pool,
        Callable& callable,
        type_list<Args...>,
        type_list<Components...>,
        type_list<Tags...> tags,
        type_list<Excludes...> excludes
    )
    {
        ArrayList<Entity> entities = simulation_entities_for(ecs, type_list<Components...>{}, tags, excludes);
        const size_t touchedEntityCount = entities.length();
        if (entities.empty())
            return;

        auto view = ecs.view<Components...>();
        view.set_dirty_tracking(false);
        auto dirtyState = begin_guaranteed_dirty_components(ecs, touchedEntityCount, DirtyComponents{});
        auto globalArguments = make_global_arguments<Args...>(ecs);
        view.each_entities_mt(entities, [&](const Entity& entity, ecs::component_value_t<Components>&... components)
        {
            auto componentTuple = std::forward_as_tuple(components...);
            invoke_for_entity_with_components<Callable, DirtyComponents>(
                ecs,
                callable,
                type_list<Args...>{},
                type_list<Components...>{},
                entity,
                componentTuple,
                dirtyState,
                globalArguments
            );
        }, pool);
    }

    template <typename Callable, typename DirtyComponents, typename... Args>
    void run_job(ECS& ecs, Threadpool& pool, Callable& callable)
    {
        using Components = component_list_t<Args...>;
        using CallableArgs = callable_submit_arg_list_t<Args...>;
        using Tags = tag_list_t<Args...>;
        using Excludes = exclude_list_t<Args...>;
        run_component_job<Callable, DirtyComponents>(
            ecs,
            pool,
            callable,
            CallableArgs{},
            Components{},
            Tags{},
            Excludes{}
        );
    }

}
