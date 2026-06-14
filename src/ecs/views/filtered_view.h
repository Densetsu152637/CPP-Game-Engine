//
// Filtered ECS views backed by the shared entity-query pipeline.
//

#pragma once

#include <type_traits>
#include <utility>

#include "view.h"

namespace ecs_query_detail
{
    template <typename... Ts>
    struct TypeList
    {};

    template <typename List, typename T>
    struct PushUnique;

    template <typename... Ts, typename T>
    struct PushUnique<TypeList<Ts...>, T>
    {
        static constexpr bool present = (std::is_same_v<Ts, T> || ...);
        using type = std::conditional_t<present, TypeList<Ts...>, TypeList<Ts..., T>>;
    };

    template <typename Arg>
    struct QueryComponent
    {
        using CleanArg = std::remove_cvref_t<Arg>;
        using type = std::conditional_t<
            ecs::query_detail::is_dirty_v<CleanArg>,
            ecs::query_detail::dirty_component_t<CleanArg>,
            std::conditional_t<
                ecs::query_detail::is_query_component_v<CleanArg>,
                CleanArg,
                void
            >
        >;
    };

    template <typename List, typename... Filters>
    struct CollectQueryComponents;

    template <typename List>
    struct CollectQueryComponents<List>
    {
        using type = List;
    };

    template <typename List, typename Filter, typename... Rest>
    struct CollectQueryComponents<List, Filter, Rest...>
    {
        using Component = typename QueryComponent<Filter>::type;
        using Next = std::conditional_t<
            std::is_void_v<Component>,
            List,
            typename PushUnique<List, Component>::type
        >;
        using type = typename CollectQueryComponents<Next, Rest...>::type;
    };

    template <typename... Filters>
    using QueryComponentList = typename CollectQueryComponents<TypeList<>, Filters...>::type;

    template <typename ComponentList, typename... Filters>
    class FilteredViewImpl;

    template <typename... Components, typename... Filters>
    class FilteredViewImpl<TypeList<Components...>, Filters...>
    {
        static constexpr size_t UNOBSERVED_GENERATION = static_cast<size_t>(-1);

        ECS* m_ecs = nullptr;
        ViewStorage m_storage = ViewStorage::Simulation;
        bool m_trackDirtyWrites = true;
        mutable bool m_cacheResolved = false;
        mutable size_t m_observedEntityGeneration = UNOBSERVED_GENERATION;
        mutable size_t m_observedQueryGeneration = UNOBSERVED_GENERATION;
        mutable size_t m_observedTagGeneration = UNOBSERVED_GENERATION;
        mutable ArrayList<Entity> m_entities;

        size_t current_query_generation() const
        {
            return ViewStorage::Rendering == m_storage
                ? m_ecs->render_query_generation()
                : m_ecs->component_query_generation();
        }

        bool cache_current() const
        {
            return m_cacheResolved
                && nullptr != m_ecs
                && m_observedEntityGeneration == m_ecs->entity_generation()
                && m_observedQueryGeneration == current_query_generation()
                && m_observedTagGeneration == m_ecs->tag_generation();
        }

        void capture_cache_generations() const
        {
            m_observedEntityGeneration = m_ecs->entity_generation();
            m_observedQueryGeneration = current_query_generation();
            m_observedTagGeneration = m_ecs->tag_generation();
        }

        void resolve_entities() const
        {
            if (ViewStorage::Rendering == m_storage)
                m_entities = m_ecs->template renderMatchingEntities<Filters...>();
            else
                m_entities = m_ecs->template matchingEntities<Filters...>();

            m_cacheResolved = true;
            capture_cache_generations();
        }

        const ArrayList<Entity>& entity_cache() const
        {
            if (nullptr == m_ecs)
                return m_entities;

            if (!cache_current())
                resolve_entities();

            return m_entities;
        }

        View<Components...> component_view() const
        {
            View<Components...> view(*m_ecs, m_storage);
            view.set_dirty_tracking(m_trackDirtyWrites);
            return view;
        }

    public:
        explicit FilteredViewImpl(ECS& ecs, const ViewStorage storage)
            : m_ecs(&ecs),
              m_storage(storage)
        {}

        void set_dirty_tracking(const bool enabled)
        { m_trackDirtyWrites = enabled; }

        void refresh()
        {
            m_cacheResolved = false;
            (void)entity_cache();
        }

        bool empty()
        { return entity_cache().empty(); }

        size_t size()
        { return entity_cache().length(); }

        ArrayList<Entity> allEntities()
        { return entity_cache(); }

        template <typename Func>
        void each(Func&& func)
        {
            const ArrayList<Entity>& entities = entity_cache();
            if (entities.empty())
                return;

            auto view = component_view();
            view.each_entities(entities, std::forward<Func>(func));
        }

        template <typename Func>
        void each(Func&& func) const
        {
            auto* self = const_cast<FilteredViewImpl*>(this);
            self->each(std::forward<Func>(func));
        }

        template <typename Func>
        void each_mt(Func&& func, Threadpool& pool, const size_t minChunk = 256)
        {
            const ArrayList<Entity>& entities = entity_cache();
            if (entities.empty())
                return;

            auto view = component_view();
            view.each_entities_mt(entities, std::forward<Func>(func), pool, minChunk);
        }

        template <typename Func>
        void each(Func&& func, Threadpool& pool)
        {
            each_mt(std::forward<Func>(func), pool);
        }
    };
}

template <typename... Filters>
class FilteredView
    : public ecs_query_detail::FilteredViewImpl<
          ecs_query_detail::QueryComponentList<Filters...>,
          Filters...
      >
{
    using base_type = ecs_query_detail::FilteredViewImpl<
        ecs_query_detail::QueryComponentList<Filters...>,
        Filters...
    >;

public:
    explicit FilteredView(ECS& ecs, const ViewStorage storage = ViewStorage::Simulation)
        : base_type(ecs, storage)
    {}
};
