//
// ECS simulation/render job creation and callable context binding.
//

#pragma once

#include <memory>
#include <type_traits>
#include <utility>

#include "ecs_access_analyzer.h"
#include "ecs_sim_invocation.h"

namespace ecs_sim
{
    template <typename... Args>
    inline constexpr bool job_uses_internal_parallelism_v =
        has_entity_arg_pack_v<Args...> ||
        type_list_size_v<component_list_t<Args...>> > 0;

    template <typename Callable>
    struct JobContext
    {
        Callable callable;

        explicit JobContext(Callable&& callable)
            : callable(std::move(callable))
        {}
    };

    template <typename Callable, typename DirtyComponents, typename... Args>
    void execute_sim_job_context(void* rawContext, ECS& ecs, Threadpool& pool)
    {
        auto& context = *static_cast<JobContext<Callable>*>(rawContext);
        run_job<Callable, DirtyComponents, Args...>(ecs, pool, context.callable);
    }

    template <typename Callable, typename... Components>
    void execute_render_job_context(void* rawContext, ECS& ecs, Threadpool& pool)
    {
        auto& context = *static_cast<JobContext<Callable>*>(rawContext);
        auto view = ecs.render_view<Components...>();
        view.each_mt([&](const ecs::component_value_t<Components>&... components)
        {
            std::invoke(context.callable, components...);
        }, pool);
    }

    template <typename DirtyComponents, typename... Args, typename Callable>
    Job make_job_from_args(type_list<Args...>, Callable&& callable)
    {
        using DecayedCallable = std::decay_t<Callable>;
        using CallableArgs = callable_arg_list_t<DecayedCallable>;

        static_assert(
            is_callable_inspectable_v<DecayedCallable>,
            "ECSProcessor simulation callables must have a non-generic, inspectable operator() so access can be derived from parameter constness"
        );

        static_assert(
            callable_arity_v<DecayedCallable> == type_list_size_v<type_list<Args...>>,
            "ECSProcessor simulation callable parameter count must match the submitted argument count"
        );

        static_assert(
            valid_callable_params_v<CallableArgs>,
            "ECSProcessor simulation callable parameters must be Entity, component lvalue references, or const ecs::ViewOf<T>&"
        );

        static_assert(
            std::is_invocable_v<
                DecayedCallable&,
                std::add_lvalue_reference_t<call_arg_t<Args>>...
            >,
            "ECSProcessor submit callable is not invocable with the provided component parameters"
        );

        Job job;
        job.access = build_callable_access_spec<DecayedCallable, DirtyComponents>();
        job.usesInternalParallelism = job_uses_internal_parallelism_v<Args...>;
        job.context = std::make_shared<JobContext<DecayedCallable>>(
            DecayedCallable(std::forward<Callable>(callable))
        );
        job.run = &execute_sim_job_context<DecayedCallable, DirtyComponents, Args...>;
        return job;
    }

    template <typename... Args, typename Callable>
    Job make_job(Callable&& callable)
    {
        static_assert(
            (... && is_supported_submit_arg_v<Args>),
            "ECSProcessor submit arguments must be component types, Dirty<T>, ecs::Dirty<T>, ecs::ViewOf<T>, or Entity"
        );

        using CallableArgs = callable_submit_arg_list_t<Args...>;
        using DirtyComponents = guaranteed_dirty_component_list_t<Args...>;
        return make_job_from_args<DirtyComponents>(CallableArgs{}, std::forward<Callable>(callable));
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
        job.usesInternalParallelism = true;
        job.context = std::make_shared<JobContext<DecayedCallable>>(
            DecayedCallable(std::forward<Callable>(callable))
        );
        job.run = &execute_render_job_context<DecayedCallable, Components...>;
        return job;
    }
}
