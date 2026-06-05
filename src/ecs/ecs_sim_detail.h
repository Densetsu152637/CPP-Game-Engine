//
// Created by Nicholas on 07/05/26.
//

#pragma once

#include <exception>
#include <functional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>

#include "ecs.h"
#include "../async/threadpool.h"

namespace ecs_sim
{
    using TypeId = size_t;

    struct AccessSpec
    {
        ArrayList<TypeId> reads{4};
        ArrayList<TypeId> writes{4};
    };

    struct Job
    {
        std::function<void(ECS&)> run;
        AccessSpec access;
    };

    using SimulationJob = Job;
    using RenderJob = Job;

    template <typename T>
    struct is_entity_arg : std::bool_constant<std::is_same_v<std::remove_cvref_t<T>, Entity>>
    {};

    template <typename T>
    inline constexpr bool is_entity_arg_v = is_entity_arg<T>::value;

    template <typename T>
    struct is_read_wrapper : std::false_type
    {};

    template <typename T>
    struct is_read_wrapper<Read<T>> : std::true_type
    {};

    template <typename T>
    inline constexpr bool is_read_wrapper_v = is_read_wrapper<std::remove_cvref_t<T>>::value;

    template <typename T>
    struct is_write_wrapper : std::false_type
    {};

    template <typename T>
    struct is_write_wrapper<Write<T>> : std::true_type
    {};

    template <typename T>
    inline constexpr bool is_write_wrapper_v = is_write_wrapper<std::remove_cvref_t<T>>::value;

    template <typename T>
    struct is_readwrite_wrapper : std::false_type
    {};

    template <typename T>
    struct is_readwrite_wrapper<ReadWrite<T>> : std::true_type
    {};

    template <typename T>
    inline constexpr bool is_readwrite_wrapper_v = is_readwrite_wrapper<std::remove_cvref_t<T>>::value;

    template <typename T>
    inline constexpr bool is_access_wrapper_v =
        is_read_wrapper_v<T> || is_write_wrapper_v<T> || is_readwrite_wrapper_v<T>;

    template <typename T>
    inline constexpr bool is_supported_submit_arg_v =
        is_access_wrapper_v<T> || is_entity_arg_v<T>;

    template <typename T>
    struct component_for_arg
    {
        using type = void;
    };

    template <typename T>
    struct component_for_arg<Read<T>>
    {
        using type = std::remove_cvref_t<T>;
    };

    template <typename T>
    struct component_for_arg<Write<T>>
    {
        using type = std::remove_cvref_t<T>;
    };

    template <typename T>
    struct component_for_arg<ReadWrite<T>>
    {
        using type = std::remove_cvref_t<T>;
    };

    template <typename T>
    using component_for_arg_t = typename component_for_arg<std::remove_cvref_t<T>>::type;

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
        return contains_any(lhs.writes, rhs.reads)
            || contains_any(lhs.writes, rhs.writes)
            || contains_any(lhs.reads, rhs.writes);
    }

    template <typename T>
    void append_access(AccessSpec& spec)
    {
        using Arg = std::remove_cvref_t<T>;

        if constexpr (is_read_wrapper_v<Arg>)
        {
            using Component = std::remove_cvref_t<typename Arg::component_type>;
            spec.reads.append(typeid(Component).hash_code());
        }
        else if constexpr (is_write_wrapper_v<Arg>)
        {
            using Component = std::remove_cvref_t<typename Arg::component_type>;
            spec.writes.append(typeid(Component).hash_code());
        }
        else if constexpr (is_readwrite_wrapper_v<Arg>)
        {
            using Component = std::remove_cvref_t<typename Arg::component_type>;
            const TypeId id = typeid(Component).hash_code();
            spec.reads.append(id);
            spec.writes.append(id);
        }
    }

    template <typename... Args>
    AccessSpec build_access_spec()
    {
        AccessSpec spec;
        (append_access<Args>(spec), ...);
        return spec;
    }

    template <typename Wrapper, typename Component>
    Wrapper make_component_argument(ECS& ecs, const Entity& entity, const char* missingMessage)
    {
        const size_t* roleLookup = nullptr;
        auto* pair = ecs.try_get<Component>(entity, roleLookup);
        if (nullptr == pair)
            throw std::out_of_range(missingMessage);

        return Wrapper(*pair, roleLookup);
    }

    template <typename Arg>
    decltype(auto) make_argument(ECS& ecs, const Entity& entity)
    {
        using Decayed = std::remove_cvref_t<Arg>;

        if constexpr (is_entity_arg_v<Decayed>)
        {
            return entity;
        }
        else if constexpr (is_read_wrapper_v<Decayed>)
        {
            using Component = std::remove_cvref_t<typename Decayed::component_type>;
            return make_component_argument<Read<Component>, Component>(
                ecs,
                entity,
                "Missing component for Read argument"
            );
        }
        else if constexpr (is_write_wrapper_v<Decayed>)
        {
            using Component = std::remove_cvref_t<typename Decayed::component_type>;
            return make_component_argument<Write<Component>, Component>(
                ecs,
                entity,
                "Missing component for Write argument"
            );
        }
        else if constexpr (is_readwrite_wrapper_v<Decayed>)
        {
            using Component = std::remove_cvref_t<typename Decayed::component_type>;
            return make_component_argument<ReadWrite<Component>, Component>(
                ecs,
                entity,
                "Missing component for ReadWrite argument"
            );
        }
        else
        {
            static_assert(sizeof(Arg) == 0, "Unsupported ECSSimulator argument");
        }
    }

    template <typename Arg, typename... Components, typename ComponentTuple, typename RoleTuple>
    decltype(auto) make_argument_from_components(
        const Entity& entity,
        ComponentTuple& components,
        const RoleTuple& roleLookups
    ) {
        using Decayed = std::remove_cvref_t<Arg>;

        if constexpr (is_entity_arg_v<Decayed>)
        {
            return entity;
        }
        else
        {
            using Component = component_for_arg_t<Decayed>;
            constexpr size_t componentIndex = type_index<Component, Components...>::value;
            auto& pair = std::get<componentIndex>(components);
            const size_t* roleLookup = std::get<componentIndex>(roleLookups);

            if constexpr (is_read_wrapper_v<Decayed>)
            {
                return Read<Component>(pair, roleLookup);
            }
            else if constexpr (is_write_wrapper_v<Decayed>)
            {
                return Write<Component>(pair, roleLookup);
            }
            else if constexpr (is_readwrite_wrapper_v<Decayed>)
            {
                return ReadWrite<Component>(pair, roleLookup);
            }
            else
            {
                static_assert(sizeof(Arg) == 0, "Unsupported ECSSimulator argument");
            }
        }
    }

    template <typename Callable, typename... Args, size_t... Is>
    void invoke_for_entity_impl(Callable& callable, ECS& ecs, const Entity& entity, std::index_sequence<Is...>)
    {
        auto args = std::tuple{
            make_argument<std::tuple_element_t<Is, std::tuple<Args...>>>(ecs, entity)...
        };
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

    template <typename Callable, typename... Args, typename... Components, typename ComponentTuple, typename RoleTuple>
    void invoke_for_entity_with_components(
        Callable& callable,
        type_list<Args...>,
        type_list<Components...>,
        const Entity& entity,
        ComponentTuple& components,
        const RoleTuple& roleLookups
    ) {
        auto args = std::tuple{
            make_argument_from_components<Args, Components...>(entity, components, roleLookups)...
        };
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
        else
        {
            auto view = ecs.view<>();
            view.each([&](const Entity& entity)
            {
                invoke_for_entity<Callable, Args...>(callable, ecs, entity);
            });
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
        auto roleLookups = view.role_lookups();
        view.each([&](const Entity& entity, Pair<ecs::component_value_t<Components>>&... components)
        {
            auto componentTuple = std::forward_as_tuple(components...);
            invoke_for_entity_with_components(
                callable,
                type_list<Args...>{},
                type_list<Components...>{},
                entity,
                componentTuple,
                roleLookups
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
            "ECSSimulator submit arguments must be Read<T>, Write<T>, ReadWrite<T>, or Entity"
        );

        using DecayedCallable = std::decay_t<Callable>;
        static_assert(
            std::is_invocable_v<
                DecayedCallable&,
                std::add_lvalue_reference_t<std::remove_reference_t<Args>>...
            >,
            "ECSSimulator submit callable is not invocable with the provided wrapper parameters"
        );

        Job job;
        job.access = build_access_spec<Args...>();
        job.run = [
            callable = DecayedCallable(std::forward<Callable>(callable))
        ](ECS& ecs) mutable
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
            (... && !is_access_wrapper_v<Components>),
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
        job.run = [
            callable = DecayedCallable(std::forward<Callable>(callable))
        ](ECS& ecs) mutable
        {
            auto view = ecs.view<Components...>();
            view.each([&](Pair<ecs::component_value_t<Components>>&... components)
            {
                std::invoke(callable, std::as_const(components.at(READ_INDEX))...);
            });
        };
        return job;
    }

    template <typename T>
    void await_promises(ArrayList<Promise<T>>& promises)
    {
        for (auto& promise : promises)
        {
            auto& result = promise.await();
            if (result.is_failure())
                std::rethrow_exception(result.exception());
        }
    }

    inline void execute_wall(Threadpool& pool, ECS& ecs, ArrayList<Job>& jobs)
    {
        if (jobs.empty())
            return;

        ArrayList<Promise<bool>> promises;
        promises.reserve(jobs.length());

        for (Job& job : jobs)
        {
            promises.append(pool.submit([&ecs, &job]()
            {
                job.run(ecs);
                return true;
            }));
        }

        await_promises(promises);
        ecs.swapSimBuffers();
    }

    inline void execute_readonly_wall(Threadpool& pool, ECS& ecs, ArrayList<Job>& jobs)
    {
        if (jobs.empty())
            return;

        ArrayList<Promise<bool>> promises;
        promises.reserve(jobs.length());

        for (Job& job : jobs)
        {
            promises.append(pool.submit([&ecs, &job]()
            {
                job.run(ecs);
                return true;
            }));
        }

        await_promises(promises);
        ecs.swapRenderBuffers();
    }
}
