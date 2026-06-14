//
// ECS simulation/render job creation and callable context binding.
//

#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "ecs_access_analyzer.h"
#include "ecs_iteration_traits.h"
#include "ecs_sim_invocation.h"

namespace ecs_sim
{
    template <typename... Args>
    inline constexpr bool job_uses_internal_parallelism_v =
        has_entity_arg_pack_v<Args...> ||
        has_filter_arg_pack_v<Args...> ||
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

    template <typename Callable, typename TagList, typename ComponentList, typename ExcludeList, typename SharedList, typename MatchComponentList>
    struct RenderJobExecutor;

    template <typename SharedComponent>
    struct SharedRenderBatch
    {
        ecs::component_value_t<SharedComponent> value;
        ArrayList<Entity> entities;

        explicit SharedRenderBatch(const ecs::component_value_t<SharedComponent>& sharedValue)
            : value(sharedValue)
        {}
    };

    template <typename SharedComponent>
    size_t find_shared_batch(
        const ArrayList<SharedRenderBatch<SharedComponent>>& batches,
        const ecs::component_value_t<SharedComponent>& value
    ) {
        for (size_t i = 0; i < batches.length(); ++i)
        {
            if (batches[i].value == value)
                return i;
        }

        return Entity::N_POS;
    }

    template <typename SharedComponent>
    ArrayList<SharedRenderBatch<SharedComponent>> group_render_entities_by_shared_component(
        ECS& ecs,
        const ArrayList<Entity>& entities
    ) {
        ArrayList<SharedRenderBatch<SharedComponent>> batches;
        auto sharedView = ecs.template render_view<SharedComponent>();
        sharedView.each_entities(entities, [&](const Entity& entity, const ecs::component_value_t<SharedComponent>& shared)
        {
            const size_t batchIndex = find_shared_batch(batches, shared);
            if (Entity::N_POS == batchIndex)
            {
                SharedRenderBatch<SharedComponent>& batch = batches.emplace(shared);
                batch.entities.append(entity);
                return;
            }

            batches[batchIndex].entities.append(entity);
        });

        return batches;
    }

    template <typename Callable, typename... Tags, typename... Components, typename... Excludes, typename... MatchComponents>
    struct RenderJobExecutor<
        Callable,
        type_list<Tags...>,
        type_list<Components...>,
        type_list<Excludes...>,
        type_list<>,
        type_list<MatchComponents...>
    >
    {
        static void execute(void* rawContext, ECS& ecs, Threadpool& pool)
        {
            auto& context = *static_cast<JobContext<Callable>*>(rawContext);
            ArrayList<Entity> entities = ecs.template renderMatchingEntities<MatchComponents..., ecs::Tag<Tags>..., Excludes...>();
            if (entities.empty())
                return;

            auto view = ecs.render_view<Components...>();
            view.each_entities_mt(entities, [&](const Entity&, const ecs::component_value_t<Components>&... components)
            {
                std::invoke(context.callable, components...);
            }, pool);
        }
    };

    template <
        typename Callable,
        typename... Tags,
        typename... Components,
        typename... Excludes,
        typename SharedComponent,
        typename... MatchComponents
    >
    struct RenderJobExecutor<
        Callable,
        type_list<Tags...>,
        type_list<Components...>,
        type_list<Excludes...>,
        type_list<SharedComponent>,
        type_list<MatchComponents...>
    >
    {
        static void execute(void* rawContext, ECS& ecs, Threadpool& pool)
        {
            auto& context = *static_cast<JobContext<Callable>*>(rawContext);
            ArrayList<Entity> entities = ecs.template renderMatchingEntities<MatchComponents..., ecs::Tag<Tags>..., Excludes...>();
            if (entities.empty())
                return;

            ArrayList<SharedRenderBatch<SharedComponent>> batches =
                group_render_entities_by_shared_component<SharedComponent>(ecs, entities);
            if (batches.empty())
                return;

            auto promise = pool.map<SharedRenderBatch<SharedComponent>, bool>(
                std::function<bool(const SharedRenderBatch<SharedComponent>&)>(
                    [&ecs, &context](const SharedRenderBatch<SharedComponent>& batch)
                    {
                        auto view = ecs.template render_view<Components...>();
                        view.each_entities(batch.entities, [&](const Entity&, const ecs::component_value_t<Components>&... components)
                        {
                            std::invoke(context.callable, components...);
                        });
                        return true;
                    }
                ),
                &batches,
                1
            );

            auto& result = promise.await();
            if (result.is_failure())
                std::rethrow_exception(result.exception());
        }
    };

    template <typename Callable, typename ComponentList, typename TagList, typename ExcludeList, typename SharedList, typename MatchComponentList>
    struct RenderJobFactory;

    template <typename Callable, typename... Components, typename... Tags, typename... Excludes, typename... SharedComponents, typename... MatchComponents>
    struct RenderJobFactory<
        Callable,
        type_list<Components...>,
        type_list<Tags...>,
        type_list<Excludes...>,
        type_list<SharedComponents...>,
        type_list<MatchComponents...>
    >
    {
        template <typename SubmittedCallable>
        static RenderJob make(SubmittedCallable&& callable)
        {
            static_assert(
                sizeof...(SharedComponents) <= 1,
                "ECSProcessor render jobs can use at most one ecs::Shared<T> component"
            );

            static_assert(
                std::is_invocable_v<
                    Callable&,
                    const ecs::component_value_t<Components>&...
                >,
                "ECSProcessor render callable is not invocable with const component references"
            );

            RenderJob job;
            job.usesInternalParallelism = true;
            job.context = std::make_shared<JobContext<Callable>>(
                Callable(std::forward<SubmittedCallable>(callable))
            );
            job.run = &RenderJobExecutor<
                Callable,
                type_list<Tags...>,
                type_list<Components...>,
                type_list<Excludes...>,
                type_list<SharedComponents...>,
                type_list<MatchComponents...>
            >::execute;
            return job;
        }
    };

    template <
        typename DirtyComponents,
        typename... SubmittedArgs,
        typename... CallableArgs,
        typename Callable
    >
    Job make_job_from_args(type_list<SubmittedArgs...>, type_list<CallableArgs...>, Callable&& callable)
    {
        using DecayedCallable = std::decay_t<Callable>;
        using ActualCallableArgs = callable_arg_list_t<DecayedCallable>;
        using SharedComponents = shared_component_list_t<SubmittedArgs...>;

        static_assert(
            is_callable_inspectable_v<DecayedCallable>,
            "ECSProcessor simulation callables must have a non-generic, inspectable operator() so access can be derived from parameter constness"
        );

        static_assert(
            callable_arity_v<DecayedCallable> == type_list_size_v<type_list<CallableArgs...>>,
            "ECSProcessor simulation callable parameter count must match the submitted argument count"
        );

        static_assert(
            valid_callable_params_v<ActualCallableArgs>,
            "ECSProcessor simulation callable parameters must be Entity, component lvalue references, or const View<T...>& for ecs::ViewOf<T...>"
        );

        static_assert(
            std::is_invocable_v<
                DecayedCallable&,
                std::add_lvalue_reference_t<call_arg_t<CallableArgs>>...
            >,
            "ECSProcessor submit callable is not invocable with the provided component parameters"
        );

        static_assert(
            type_list_size_v<SharedComponents> <= 1,
            "ECSProcessor simulation jobs can use at most one ecs::Shared<T> component"
        );

        Job job;
        job.access = build_job_access_spec<DecayedCallable, DirtyComponents, type_list<SubmittedArgs...>>();
        job.usesInternalParallelism = job_uses_internal_parallelism_v<SubmittedArgs...>;
        job.context = std::make_shared<JobContext<DecayedCallable>>(
            DecayedCallable(std::forward<Callable>(callable))
        );
        job.run = &execute_sim_job_context<DecayedCallable, DirtyComponents, SubmittedArgs...>;
        return job;
    }

    template <typename... Args, typename Callable>
    Job make_job(Callable&& callable)
    {
        static_assert(
            (... && is_supported_submit_arg_v<Args>),
            "ECSProcessor submit arguments must be component types, Dirty<T>, ecs::Dirty<T>, ecs::ViewOf<T>, ecs::Tag<T>, ecs::Exclude<T>, or Entity"
        );

        using Iteration = SimulationIterationTypes<Args...>;
        return make_job_from_args<typename Iteration::dirty_components>(
            typename Iteration::submitted_args{},
            typename Iteration::callable_args{},
            std::forward<Callable>(callable)
        );
    }

    template <typename... Args, typename Callable>
    SimulationJob make_sim_job(Callable&& callable)
    {
        return make_job<Args...>(std::forward<Callable>(callable));
    }

    template <typename... Args, typename Callable>
    RenderJob make_render_job(Callable&& callable)
    {
        static_assert(
            (... && (is_plain_component_arg_v<Args> || is_tag_arg_v<Args> || is_exclude_arg_v<Args> || is_shared_arg_v<Args>)),
            "ECSProcessor render arguments must be plain component types, ecs::Tag<T> filters, ecs::Exclude<T> filters, or ecs::Shared<T> grouping components"
        );

        using DecayedCallable = std::decay_t<Callable>;
        using Iteration = RenderIterationTypes<Args...>;
        return RenderJobFactory<
            DecayedCallable,
            typename Iteration::iteration_components,
            typename Iteration::tags,
            typename Iteration::excludes,
            typename Iteration::shared_components,
            typename Iteration::match_components
        >::make(
            std::forward<Callable>(callable)
        );
    }
}
