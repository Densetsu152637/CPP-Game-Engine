//
// Created by Nicholas on 07/05/26.
//

#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <typeinfo>

#include "ecs.h"
#include "async/threadpool.h"
#include "../structs/sorted_array.h"

enum class AccessMode
{
    ReadOnly,
    WriteExclusive,
    ReadWrite
};

namespace ecs_simulator_detail
{
    template <typename... Ts>
    struct type_list
    {};

    template <typename List>
    struct type_list_size;

    template <typename... Ts>
    struct type_list_size<type_list<Ts...>> : std::integral_constant<size_t, sizeof...(Ts)>
    {};

    template <typename List>
    inline constexpr size_t type_list_size_v = type_list_size<List>::value;

    template <typename List>
    inline constexpr bool type_list_empty_v = 0 == type_list_size_v<List>;

    template <typename... Lists>
    struct list_cat_many;

    template <typename... Lists>
    using list_cat_many_t = typename list_cat_many<Lists...>::type;

    template <>
    struct list_cat_many<>
    {
        using type = type_list<>;
    };

    template <typename L>
    struct list_cat_many<L>
    {
        using type = L;
    };

    template <typename... A, typename... B, typename... Rest>
    struct list_cat_many<type_list<A...>, type_list<B...>, Rest...>
    {
        using type = list_cat_many_t<type_list<A..., B...>, Rest...>;
    };

    template <typename T>
    struct function_traits : function_traits<decltype(&T::operator())>
    {};

    template <typename T>
    using function_args_t = typename function_traits<T>::args_tuple;

    template <typename R, typename... Args>
    struct function_traits<R(Args...)>
    {
        using result_type = R;
        using args_tuple = std::tuple<Args...>;
    };

    template <typename R, typename... Args>
    struct function_traits<R (*)(Args...)> : function_traits<R(Args...)>
    {};

    template <typename C, typename R, typename... Args>
    struct function_traits<R (C::*)(Args...) const> : function_traits<R(Args...)>
    {};

    template <typename C, typename R, typename... Args>
    struct function_traits<R (C::*)(Args...)> : function_traits<R(Args...)>
    {};

    template <typename R, typename... Args>
    struct function_traits<std::function<R(Args...)>> : function_traits<R(Args...)>
    {};

    template <typename T>
    struct is_entity_arg : std::bool_constant<std::is_same_v<std::remove_cvref_t<T>, Entity>>
    {};

    template <typename T>
    inline constexpr bool is_entity_arg_v = is_entity_arg<T>::value;

    template <typename T>
    struct is_mutable_lvalue_ref : std::false_type
    {};

    template <typename T>
    struct is_mutable_lvalue_ref<T&> : std::bool_constant<!std::is_const_v<T>>
    {};

    template <typename T>
    inline constexpr bool is_mutable_lvalue_ref_v = is_mutable_lvalue_ref<T>::value;

    template <typename T>
    struct arg_to_component
    {
        using type = std::conditional_t<
            is_entity_arg_v<T>,
            type_list<>,
            type_list<std::remove_cvref_t<T>>
        >;
    };

    template <typename T>
    using arg_to_component_t = typename arg_to_component<T>::type;

    template <typename Tuple>
    struct tuple_to_components;

    template <typename Tuple>
    using tuple_to_components_t = typename tuple_to_components<Tuple>::type;

    template <typename... Args>
    struct tuple_to_components<std::tuple<Args...>>
    {
        using type = list_cat_many_t<arg_to_component_t<Args>...>;
    };

    template <typename Tuple, size_t I = 0>
    consteval bool tuple_has_entity()
    {
        if constexpr (I == std::tuple_size_v<Tuple>)
        {
            return false;
        }
        else
        {
            using Arg = std::tuple_element_t<I, Tuple>;
            if constexpr (is_entity_arg_v<Arg>)
                return true;

            return tuple_has_entity<Tuple, I + 1>();
        }
    }

    template <typename Tuple, size_t I = 0>
    consteval bool tuple_has_write()
    {
        if constexpr (I == std::tuple_size_v<Tuple>)
        {
            return false;
        }
        else
        {
            using Arg = std::tuple_element_t<I, Tuple>;
            if constexpr (is_mutable_lvalue_ref_v<Arg> && !is_entity_arg_v<Arg>)
                return true;

            return tuple_has_write<Tuple, I + 1>();
        }
    }

    using TypeId = size_t;

    struct AccessSpec
    {
        SortedArray<TypeId> reads;
        SortedArray<TypeId> writes;
    };

    inline bool contains_any(const SortedArray<TypeId>& haystack, const SortedArray<TypeId>& needles)
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

    template <AccessMode Mode, typename Arg>
    decltype(auto) fetch_argument(ECS& ecs, const Entity& entity)
    {
        using Component = std::remove_cvref_t<Arg>;

        if constexpr (is_entity_arg_v<Arg>)
        {
            return entity;
        }
        else if constexpr (Mode == AccessMode::ReadOnly)
        {
            return static_cast<const Component&>(ecs.getComponent<Component>(entity));
        }
        else if constexpr (Mode == AccessMode::WriteExclusive)
        {
            return static_cast<Component&>(ecs.getComponent<Component>(entity));
        }
        else
        {
            if constexpr (std::is_const_v<std::remove_reference_t<Arg>>)
                return static_cast<const Component&>(ecs.getComponent<Component>(entity));

            return static_cast<Component&>(ecs.getComponent<Component>(entity));
        }
    }

    template <AccessMode Mode, typename Arg>
    using argument_ref_t = decltype(fetch_argument<Mode, Arg>(std::declval<ECS&>(), std::declval<const Entity&>()));

    template <AccessMode Mode, typename Callable, typename... Args>
    bool invoke_for_entity(ECS& ecs, Callable& callable, const Entity& entity)
    {
        if constexpr (std::is_invocable_v<Callable&, argument_ref_t<Mode, Args>...>)
        {
            std::invoke(callable, fetch_argument<Mode, Args>(ecs, entity)...);
            return true;
        }

        static_assert(sizeof...(Args) == 0, "Callable is not compatible with this ECS job");
        return false;
    }

    template <typename... Components>
    ArrayList<Entity> collect_matching_entities(ECS& ecs)
    {
        ArrayList<Entity> entities;
        ecs.view<Components...>().each([&](Entity entity, Components&...)
        {
            entities.append(entity);
        });
        return entities;
    }

    template <typename List>
    struct collector_from_list;

    template <typename... Components>
    struct collector_from_list<type_list<Components...>>
    {
        static ArrayList<Entity> collect(ECS& ecs)
        {
            return collect_matching_entities<Components...>(ecs);
        }
    };

    inline ArrayList<Entity> collect_all_entities(ECS& ecs)
    {
        ArrayList<Entity> entities;
        ecs.eachEntity([&](Entity entity)
        {
            entities.append(entity);
        });
        return entities;
    }
}

class ECSSimulator
{
public:
    using AccessMode = ::AccessMode;

private:
    struct Job
    {
        std::function<std::shared_ptr<Promise<ArrayList<bool>>>(ECS&, Threadpool&)> run;
        ecs_simulator_detail::AccessSpec access;
    };

    struct Phase
    {
        SortedArray<size_t> jobs;
        ecs_simulator_detail::AccessSpec access;
    };

    ECS m_ecs;
    Threadpool& m_pool;
    std::unordered_map<std::string, Job> m_jobs;
    ArrayList<std::string> m_order;

    template <AccessMode Mode, typename Callable>
    Job make_job(Callable&& callable)
    {
        using Decayed = std::decay_t<Callable>;
        using ArgsTuple = ecs_simulator_detail::function_args_t<Decayed>;
        using ComponentList = ecs_simulator_detail::tuple_to_components_t<ArgsTuple>;

        Job job;
        job.access = build_access_spec<Mode, ArgsTuple>();
        job.run = [callable = Decayed(std::forward<Callable>(callable))](ECS& ecs, Threadpool& pool) mutable
            -> std::shared_ptr<Promise<ArrayList<bool>>>
        {
            return run_job<Mode, Decayed, ArgsTuple, ComponentList>(ecs, pool, std::move(callable));
        };

        return job;
    }

    template <AccessMode Mode, typename Tuple, size_t I = 0>
    static ecs_simulator_detail::AccessSpec build_access_spec()
    {
        ecs_simulator_detail::AccessSpec spec;

        if constexpr (I < std::tuple_size_v<Tuple>)
        {
            using Arg = std::tuple_element_t<I, Tuple>;
            if constexpr (!ecs_simulator_detail::is_entity_arg_v<Arg>)
            {
                const ecs_simulator_detail::TypeId componentId = typeid(std::remove_cvref_t<Arg>).hash_code();

                if constexpr (Mode == AccessMode::ReadOnly)
                {
                    spec.reads.append(componentId);
                }
                else if constexpr (Mode == AccessMode::WriteExclusive)
                {
                    spec.writes.append(componentId);
                }
                else
                {
                    if constexpr (ecs_simulator_detail::is_mutable_lvalue_ref_v<Arg>)
                        spec.writes.append(componentId);
                    else
                        spec.reads.append(componentId);
                }
            }

            auto next = build_access_spec<Mode, Tuple, I + 1>();
            spec.reads.append(next.reads);
            spec.writes.append(next.writes);
        }

        spec.reads.sort();
        spec.writes.sort();
        return spec;
    }

    template <AccessMode Mode, typename Callable, typename ArgsTuple, typename ComponentList>
    static std::shared_ptr<Promise<ArrayList<bool>>> run_job(ECS& ecs, Threadpool& pool, Callable callable)
    {
        return run_job_impl<Mode, Callable, ArgsTuple, ComponentList>(
            ecs,
            pool,
            std::move(callable),
            std::make_index_sequence<std::tuple_size_v<ArgsTuple>>{}
        );
    }

    template <AccessMode Mode, typename Callable, typename ArgsTuple, typename ComponentList, size_t... Is>
    static std::shared_ptr<Promise<ArrayList<bool>>> run_job_impl(
        ECS& ecs,
        Threadpool& pool,
        Callable callable,
        std::index_sequence<Is...>)
    {
        constexpr bool needsEntity = ecs_simulator_detail::tuple_has_entity<ArgsTuple>();

        if constexpr (sizeof...(Is) == 0)
        {
            if constexpr (needsEntity)
            {
                ArrayList<Entity> entities = ecs_simulator_detail::collect_all_entities(ecs);
                auto fn = [callable = std::move(callable)](const Entity& entity) mutable -> bool
                {
                    std::invoke(callable, entity);
                    return true;
                };

                return pool.map<Entity, bool>(std::function<bool(const Entity&)>(fn), &entities);
            }
            else
            {
                auto promise = std::make_shared<Promise<ArrayList<bool>>>(&pool);
                ArrayList<bool> results;
                std::invoke(callable);
                results.append(true);
                promise->complete(Result<ArrayList<bool>>::success(std::move(results)));
                return promise;
            }
        }
        else
        {
            ArrayList<Entity> entities;
            if constexpr (ecs_simulator_detail::type_list_empty_v<ComponentList>)
            {
                entities = ecs_simulator_detail::collect_all_entities(ecs);
            }
            else
            {
                entities = ecs_simulator_detail::collector_from_list<ComponentList>::collect(ecs);
            }

            auto fn = [callable = std::move(callable), &ecs](const Entity& entity) mutable -> bool
            {
                ecs_simulator_detail::invoke_for_entity<Mode, Callable, std::tuple_element_t<Is, ArgsTuple>...>(
                    ecs,
                    callable,
                    entity
                );
                return true;
            };

            return pool.map<Entity, bool>(std::function<bool(const Entity&)>(fn), &entities);
        }
    }

    void store_job(const std::string& name, Job job)
    {
        auto it = m_jobs.find(name);
        if (it == m_jobs.end())
        {
            m_order.append(name);
            m_jobs.emplace(name, std::move(job));
            return;
        }

        it->second = std::move(job);
    }

    ArrayList<Phase> build_phases() const
    {
        ArrayList<Phase> phases;

        for (size_t jobIndex = 0; jobIndex < m_order.length(); ++jobIndex)
        {
            const Job& job = m_jobs.at(m_order[jobIndex]);
            bool placed = false;

            for (size_t phaseIndex = 0; phaseIndex < phases.length(); ++phaseIndex)
            {
                Phase& phase = phases[phaseIndex];

                if (!ecs_simulator_detail::conflicts_with(phase.access, job.access))
                {
                    phase.jobs.append(jobIndex);
                    phase.access.reads.append(job.access.reads);
                    phase.access.writes.append(job.access.writes);
                    phase.access.reads.sort();
                    phase.access.writes.sort();
                    placed = true;
                    break;
                }
            }

            if (!placed)
            {
                Phase phase;
                phase.jobs.append(jobIndex);
                phase.access = job.access;
                phases.append(phase);
            }
        }

        return phases;
    }

    void run_phase(const Phase& phase)
    {
        ArrayList<std::shared_ptr<Promise<ArrayList<bool>>>> promises;

        for (size_t i = 0; i < phase.jobs.length(); ++i)
        {
            const Job& job = m_jobs.at(m_order[phase.jobs[i]]);
            promises.append(job.run(m_ecs, m_pool));
        }

        for (size_t i = 0; i < promises.length(); ++i)
        {
            promises[i]->await();
        }
    }

public:
    explicit ECSSimulator(Threadpool& pool)
        : m_pool(pool)
    {}

    ECS& ecs()
    { return m_ecs; }

    const ECS& ecs() const
    { return m_ecs; }

    void clear()
    {
        m_jobs.clear();
        m_order.clear();
    }

    template <typename Callable>
    void submit(const std::string& name, Callable&& callable)
    {
        using Decayed = std::decay_t<Callable>;
        using ArgsTuple = ecs_simulator_detail::function_args_t<Decayed>;

        if (ecs_simulator_detail::tuple_has_write<ArgsTuple>())
        {
            submit(name, AccessMode::ReadWrite, std::forward<Callable>(callable));
        }
        else
        {
            submit(name, AccessMode::ReadOnly, std::forward<Callable>(callable));
        }
    }

    template <typename Callable>
    void submit(const std::string& name, AccessMode mode, Callable&& callable)
    {
        if (name.empty())
            throw std::invalid_argument("ECSSimulator job name cannot be empty");

        switch (mode)
        {
            case AccessMode::ReadOnly:
                store_job(name, make_job<AccessMode::ReadOnly>(std::forward<Callable>(callable)));
                break;
            case AccessMode::WriteExclusive:
                store_job(name, make_job<AccessMode::WriteExclusive>(std::forward<Callable>(callable)));
                break;
            case AccessMode::ReadWrite:
            default:
                store_job(name, make_job<AccessMode::ReadWrite>(std::forward<Callable>(callable)));
                break;
        }
    }

    void simulate()
    {
        const ArrayList<Phase> phases = build_phases();
        for (size_t i = 0; i < phases.length(); ++i)
        {
            run_phase(phases[i]);
        }
    }
};

using ECS_Simulator = ECSSimulator;
