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
        ArrayList<TypeId> reads;
        ArrayList<TypeId> writes;
    };

    struct Job
    {
        std::function<void(ECS&)> run;
        AccessSpec access;
    };

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
            typename push_type<List, component>::type
        >;
        using type = typename collect_components<next, Rest...>::type;
    };

    template <typename... Args>
    using component_list_t = typename collect_components<type_list<>, Args...>::type;

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

    inline void append_access(AccessSpec& dst, const AccessSpec& src)
    {
        dst.reads.append(src.reads);
        dst.writes.append(src.writes);
    }

    template <typename T>
    void append_access(AccessSpec& spec)
    {
        using Arg = std::remove_cvref_t<T>;

        if constexpr (is_read_wrapper_v<Arg>)
        {
            using Component = std::remove_cvref_t<typename Arg::value_type>;
            spec.reads.append(typeid(Component).hash_code());
        }
        else if constexpr (is_write_wrapper_v<Arg>)
        {
            using Component = std::remove_cvref_t<typename Arg::value_type>;
            spec.writes.append(typeid(Component).hash_code());
        }
        else if constexpr (is_readwrite_wrapper_v<Arg>)
        {
            using Component = std::remove_cvref_t<typename Arg::value_type>;
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
            using Component = std::remove_cvref_t<typename Decayed::value_type>;
            Component* ptr = ecs.try_read<Component>(entity);
            if (nullptr == ptr)
                throw std::out_of_range("Missing component for Read argument");
            return Read<Component>(*ptr);
        }
        else if constexpr (is_write_wrapper_v<Decayed>)
        {
            using Component = std::remove_cvref_t<typename Decayed::value_type>;
            Component* ptr = ecs.try_write<Component>(entity);
            if (nullptr == ptr)
                throw std::out_of_range("Missing component for Write argument");
            return Write<Component>(*ptr);
        }
        else if constexpr (is_readwrite_wrapper_v<Decayed>)
        {
            using Component = std::remove_cvref_t<typename Decayed::value_type>;
            Component* readPtr = ecs.try_read<Component>(entity);
            Component* writePtr = ecs.try_write<Component>(entity);
            if (nullptr == readPtr || nullptr == writePtr)
                throw std::out_of_range("Missing component for ReadWrite argument");
            return ReadWrite<Component>(*readPtr, *writePtr);
        }
        else
        {
            static_assert(sizeof(Arg) == 0, "Unsupported ECSSimulator argument");
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
        view.each([&](const Entity& entity, Pair<Components>&...)
        {
            invoke_for_entity<Callable, Args...>(callable, ecs, entity);
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

        ArrayList<ArrayList<Job*>> batches;
        ArrayList<AccessSpec> batchAccess;

        for (Job& job : jobs)
        {
            bool placed = false;
            for (size_t i = 0; i < batches.length(); ++i)
            {
                if (conflicts_with(batchAccess[i], job.access))
                    continue;

                batches[i].append(&job);
                append_access(batchAccess[i], job.access);
                placed = true;
                break;
            }

            if (!placed)
            {
                batches.append(ArrayList<Job*>());
                batches[batches.length() - 1].append(&job);
                batchAccess.append(job.access);
            }
        }

        for (const auto& batch : batches)
        {
            ArrayList<Promise<bool>> promises;
            promises.reserve(batch.length());

            for (Job* job : batch)
            {
                promises.append(pool.submit([&ecs, job]()
                {
                    job->run(ecs);
                    return true;
                }));
            }

            await_promises(promises);
        }
    }
}
