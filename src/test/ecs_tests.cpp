#include <atomic>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "ecs/aliases/component_alias.h"
#include "ecs/processor.h"

namespace
{
    struct PositionTag {};
    struct VelocityTag {};
    struct HealthTag {};
    struct ManaTag {};

    using Position = ecs::BufferedAlias<int, PositionTag>;
    using Velocity = ecs::Alias<int, VelocityTag>;
    using Health = ecs::Alias<int, HealthTag>;
    using Mana = ecs::Alias<int, ManaTag>;

    class CapturingLogger final : public Logger
    {
    public:
        ArrayList<std::string> lines;

        void log(const std::string& message) override
        {
            lines.append(message);
        }
    };

    void require(const bool condition, const std::string& message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    template <typename Func>
    void require_throws(Func&& func, const std::string& message)
    {
        try
        {
            func();
        }
        catch (const std::exception&)
        {
            return;
        }

        throw std::runtime_error(message);
    }

    bool contains_line(const ArrayList<std::string>& lines, const std::string& token)
    {
        for (const std::string& line : lines)
        {
            if (line.find(token) != std::string::npos)
                return true;
        }

        return false;
    }

    void test_sparse_tuple_component_storage_accesses_components()
    {
        SparseTupleComponentStorage<Velocity, Health> storage;

        storage.emplace(42, Velocity(3), Health(7));

        Velocity* velocity = storage.try_get_component<Velocity>(42);
        Health* health = storage.try_get_component<Health>(42);

        require(nullptr != velocity, "tuple component storage did not return Velocity by entity");
        require(nullptr != health, "tuple component storage did not return Health by entity");
        require(static_cast<int>(*velocity) == 3, "tuple component storage returned wrong Velocity value");
        require(static_cast<int>(*health) == 7, "tuple component storage returned wrong Health value");
        require(storage.entity_at(0) == 42, "tuple component storage did not preserve entity key");

        storage.dense_component_at<Velocity>(0) = 11;
        require(
            static_cast<int>(*storage.try_get_component<Velocity>(42)) == 11,
            "tuple component storage dense component access did not update the entity component"
        );
    }

    void test_archetype_registration_tracks_components()
    {
        ECS ecs;
        auto& pool = ecs.registerArchetype<Velocity, Health>();
        const Entity entity = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(entity, 3);
        ecs.emplaceComponent<Health>(entity, 7);

        require(ecs.isArchetypedComponent<Velocity>(), "archetype registration did not track Velocity");
        require(ecs.isArchetypedComponent<Health>(), "archetype registration did not track Health");
        require(
            ecs.archetypePoolIfExists<Velocity, Health>() == &pool,
            "archetype pool lookup did not return the registered tuple pool"
        );

        require(
            static_cast<int>(*pool.try_get_component<Velocity>(entity.index)) == 3,
            "registered archetype pool did not store Velocity"
        );
        require(
            static_cast<int>(*pool.try_get_component<Health>(entity.index)) == 7,
            "registered archetype pool did not store Health"
        );
    }

    void test_archetype_registration_merges_superset_groups()
    {
        ECS ecs;
        auto& first = ecs.registerArchetype<Velocity, Health>();
        const Entity entity = ecs.createEntity();
        first.assignComponent<Velocity>(entity.index, Velocity(3));
        first.assignComponent<Health>(entity.index, Health(7));

        auto& merged = ecs.registerArchetype<Velocity, Health, Mana>();

        require(ecs.isArchetypedComponent<Mana>(), "merged archetype did not track the added component");
        require(
            nullptr == ecs.archetypePoolIfExists<Velocity, Health>(),
            "merged archetype left the old subset pool registered"
        );
        require(
            ecs.archetypePoolIfExists<Velocity, Health, Mana>() == &merged,
            "merged archetype pool lookup did not return the superset tuple pool"
        );
        require(
            static_cast<int>(*merged.try_get_component<Velocity>(entity.index)) == 3,
            "merged archetype did not preserve Velocity"
        );
        require(
            static_cast<int>(*merged.try_get_component<Health>(entity.index)) == 7,
            "merged archetype did not preserve Health"
        );
        require(
            nullptr == merged.try_get_component<Mana>(entity.index),
            "merged archetype marked the newly added component present without an explicit insert"
        );
    }

    void test_archetype_rows_track_partial_component_presence()
    {
        ECS ecs;
        ecs.registerArchetype<Velocity, Health>();

        const Entity entity = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(entity, 3);

        require(ecs.hasComponent<Velocity>(entity), "archetype row did not report inserted Velocity");
        require(!ecs.hasComponent<Health>(entity), "archetype row reported missing Health as present");
        require(ecs.view<Velocity>().size() == 1, "single-component archetype view missed present Velocity");
        require(ecs.view<Velocity, Health>().size() == 0, "pair archetype view matched a partial row");

        ecs.emplaceComponent<Health>(entity, 7);
        require(ecs.hasComponent<Health>(entity), "archetype row did not report inserted Health");
        require(ecs.view<Velocity, Health>().size() == 1, "pair archetype view missed completed row");
    }

    void test_archetype_component_removal_keeps_other_components()
    {
        ECS ecs;
        ecs.registerArchetype<Velocity, Health>();

        const Entity entity = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(entity, 3);
        ecs.emplaceComponent<Health>(entity, 7);

        require(ecs.removeComponent<Health>(entity), "archetype component removal returned false");
        require(ecs.hasComponent<Velocity>(entity), "removing Health erased Velocity from the archetype row");
        require(!ecs.hasComponent<Health>(entity), "removed Health was still present in the archetype row");
        require(ecs.view<Velocity>().size() == 1, "Velocity view missed row after removing Health");
        require(ecs.view<Velocity, Health>().size() == 0, "pair view matched row after removing Health");
    }

    void test_archetype_registration_migrates_existing_standalone_components()
    {
        ECS ecs;
        const Entity first = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(first, 3);

        const Entity second = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(second, 5);
        ecs.emplaceComponent<Health>(second, 7);

        ecs.registerArchetype<Velocity, Health>();

        require(ecs.hasComponent<Velocity>(first), "late archetype registration lost standalone Velocity");
        require(!ecs.hasComponent<Health>(first), "late archetype registration defaulted missing Health");
        require(static_cast<int>(*ecs.try_get<Velocity>(first)) == 3, "late archetype migration changed Velocity");
        require(ecs.view<Velocity>().size() == 2, "late archetype migration lost Velocity rows");
        require(ecs.view<Velocity, Health>().size() == 1, "late archetype migration built wrong pair view");
        require(
            static_cast<int>(*ecs.try_get<Health>(second)) == 7,
            "late archetype migration changed Health"
        );
    }

    void test_archetype_registration_rejects_incomplete_overlap()
    {
        ECS ecs;
        ecs.registerArchetype<Velocity, Health>();

        require_throws(
            [&]()
            {
                ecs.registerArchetype<Health, Mana>();
            },
            "incomplete overlapping archetype registration was not rejected"
        );
    }

    void test_archetype_view_uses_tuple_pool_with_standalone_components()
    {
        ECS ecs;
        ecs.registerArchetype<Velocity, Health>();

        const Entity first = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(first, 3);
        ecs.emplaceComponent<Health>(first, 7);
        ecs.emplaceComponent<Mana>(first, 11);

        const Entity second = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(second, 5);
        ecs.emplaceComponent<Health>(second, 13);

        int count = 0;
        int sum = 0;
        auto view = ecs.view<Velocity, Health, Mana>();
        view.each([&](Velocity& velocity, Health& health, Mana& mana)
        {
            ++count;
            sum += static_cast<int>(velocity);
            sum += static_cast<int>(health);
            sum += static_cast<int>(mana);
        });

        require(count == 1, "archetype-backed mixed view matched the wrong number of entities");
        require(sum == 21, "archetype-backed mixed view read incorrect component values");
    }

    void test_archetype_sim_job_mutates_tuple_components()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        processor.registerArchetype<Velocity, Health>();

        const Entity entity = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(entity, 3);
        ecs.emplaceComponent<Health>(entity, 7);
        ecs.emplaceComponent<Mana>(entity, 11);

        processor.queue_into_sim<Velocity, Health, Mana>(
            "archetype-mixed-update",
            [](Velocity& velocity, Health& health, Mana& mana)
        {
            velocity = static_cast<int>(velocity) + 1;
            health = static_cast<int>(health) + 2;
            mana = static_cast<int>(mana) + 3;
        });

        processor.simulate();

        require(static_cast<int>(*ecs.try_get<Velocity>(entity)) == 4, "sim job did not mutate archetyped Velocity");
        require(static_cast<int>(*ecs.try_get<Health>(entity)) == 9, "sim job did not mutate archetyped Health");
        require(static_cast<int>(*ecs.try_get<Mana>(entity)) == 14, "sim job did not mutate standalone Mana");
    }

    void test_archetype_view_of_iterates_requested_components()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        processor.registerArchetype<Velocity, Health>();

        const Entity first = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(first, 3);

        const Entity second = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(second, 5);
        ecs.emplaceComponent<Health>(second, 7);

        int sum = 0;
        processor.queue_into_sim<ecs::ViewOf<Velocity>>(
            "archetype-view-of",
            [&](const View<Velocity>& velocities)
        {
            for (const Velocity& velocity : velocities)
                sum += static_cast<int>(velocity);
        });

        processor.simulate();

        require(sum == 8, "ecs::ViewOf<T> did not iterate archetyped components");
    }

    void test_view_of_read_conflicts_with_nonbuffered_writer()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);

        processor.queue_into_sim<ecs::ViewOf<Velocity>>(
            "read-velocity-view",
            [](const View<Velocity>& velocities)
        {
            (void)velocities;
        });

        require_throws(
            [&]()
            {
                processor.queue_into_sim<Velocity>("write-velocity", [](Velocity& velocity)
                {
                    velocity = static_cast<int>(velocity) + 1;
                });
            },
            "ecs::ViewOf<T> read did not conflict with same-wall non-buffered writer"
        );
    }

    void test_view_of_can_be_used_in_component_job()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        const Entity target = ecs.createEntity();
        ecs.emplaceComponent<Health>(target, 0);

        const Entity first = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(first, 3);

        const Entity second = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(second, 5);

        processor.queue_into_sim<Health, ecs::ViewOf<Velocity>>(
            "component-job-with-view-of",
            [](Health& health, const View<Velocity>& velocities)
        {
            int sum = 0;
            for (const Velocity& velocity : velocities)
                sum += static_cast<int>(velocity);

            health = sum;
        });

        processor.simulate();

        require(
            static_cast<int>(*ecs.try_get<Health>(target)) == 8,
            "ecs::ViewOf<T> did not work as a global argument in a component job"
        );
    }

    void test_archetype_render_transfer_uses_tuple_pool_dirty_state()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        processor.registerArchetype<Velocity, Health>();

        const Entity first = ecs.createEntity();
        const Entity second = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(first, 1);
        ecs.emplaceComponent<Velocity>(second, 2);
        ecs.emplaceComponent<Health>(second, 7);

        std::atomic<int> sum = 0;
        processor.queue_into_rendering<Velocity>("sum-archetype", [&](const Velocity& velocity)
        {
            sum.fetch_add(static_cast<int>(velocity), std::memory_order_relaxed);
        });

        processor.simulate();
        sum = 0;
        processor.render();
        sum = 0;
        processor.render();
        require(sum.load() == 3, "initial archetype render transfer failed");

        *ecs.try_get_mut<Velocity>(first) = 10;
        processor.simulate();
        sum = 0;
        processor.render();
        sum = 0;
        processor.render();
        require(sum.load() == 12, "dirty archetype render transfer failed after mutation");

        ecs.removeComponent<Velocity>(second);
        processor.simulate();
        sum = 0;
        processor.render();
        sum = 0;
        processor.render();
        require(sum.load() == 10, "archetype render transfer did not remove deleted component");
    }

    void test_sim_archetype_does_not_create_render_archetype()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        processor.registerArchetype<Velocity, Health>();

        const Entity entity = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(entity, 3);
        ecs.emplaceComponent<Health>(entity, 7);

        std::atomic<int> sum = 0;
        processor.queue_into_rendering<Velocity>("sum-render-sparse", [&](const Velocity& velocity)
        {
            sum.fetch_add(static_cast<int>(velocity), std::memory_order_relaxed);
        });

        require(
            !ecs.isRenderArchetypedComponent<Velocity>(),
            "simulation archetype implicitly registered Velocity as a render archetype"
        );

        processor.simulate();
        sum = 0;
        processor.render();
        sum = 0;
        processor.render();
        require(sum.load() == 3, "sparse render pool did not receive sim archetype component transfer");
    }

    void test_explicit_render_archetype_transfers_and_renders()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        processor.registerArchetype<Velocity, Health>();
        processor.registerRenderArchetype<Velocity, Health>();

        const Entity entity = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(entity, 3);
        ecs.emplaceComponent<Health>(entity, 7);

        require(ecs.isRenderArchetypedComponent<Velocity>(), "Velocity render archetype was not explicit");
        require(ecs.isRenderArchetypedComponent<Health>(), "Health render archetype was not explicit");
        require(
            nullptr != ecs.renderArchetypePoolIfExists<Velocity, Health>(),
            "explicit render archetype pool was not registered"
        );

        std::atomic<int> sum = 0;
        processor.queue_into_rendering<Velocity, Health>(
            "sum-render-archetype",
            [&](const Velocity& velocity, const Health& health)
        {
            sum.fetch_add(static_cast<int>(velocity) + static_cast<int>(health), std::memory_order_relaxed);
        });

        processor.simulate();
        sum = 0;
        processor.render();
        require(sum.load() == 10, "explicit render archetype did not render transferred components");

        *ecs.try_get_mut<Velocity>(entity) = 5;
        processor.simulate();
        sum = 0;
        processor.render();
        require(sum.load() == 12, "explicit render archetype did not receive dirty component updates");
    }

    void test_render_archetype_registration_migrates_existing_render_sparse_pool()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        const Entity entity = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(entity, 4);

        std::atomic<int> sum = 0;
        processor.queue_into_rendering<Velocity>("sum-migrated-render", [&](const Velocity& velocity)
        {
            sum.fetch_add(static_cast<int>(velocity), std::memory_order_relaxed);
        });

        processor.simulate();
        sum = 0;
        processor.render();
        sum = 0;
        processor.render();
        require(sum.load() == 4, "initial sparse render transfer failed before render archetype migration");

        processor.registerRenderArchetype<Velocity, Health>();
        require(ecs.isRenderArchetypedComponent<Velocity>(), "render archetype registration did not track Velocity");

        sum = 0;
        processor.render();
        require(sum.load() == 4, "render archetype registration did not migrate existing render sparse data");
    }

    void test_component_type_ids_are_stable_and_distinct()
    {
        const ecs::ComponentTypeId firstPositionId = ecs::component_type_id<Position>();
        const ecs::ComponentTypeId secondPositionId = ecs::component_type_id<Position>();
        const ecs::ComponentTypeId velocityId = ecs::component_type_id<Velocity>();

        require(firstPositionId == secondPositionId, "component type ID was not stable for the same type");
        require(firstPositionId != velocityId, "component type IDs were not distinct across component types");
    }

    void test_buffered_write_write_conflict()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);

        processor.queue_into_sim<Position>("writer-a", [](Position& position)
        {
            position = 1;
        });

        require_throws(
            [&]()
            {
                processor.queue_into_sim<Position>("writer-b", [](Position& position)
                {
                    position = 2;
                });
            },
            "buffered mutable/mutable same-wall conflict was not rejected"
        );
    }

    void test_buffered_read_write_is_allowed()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);

        processor.queue_into_sim<Position>("reader", [](const Position& position)
        {
            (void)position;
        });

        processor.queue_into_sim<Position>("writer", [](Position& position)
        {
            position = 2;
        });
    }

    void test_nonbuffered_read_write_conflict()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);

        processor.queue_into_sim<Velocity>("reader", [](const Velocity& velocity)
        {
            (void)velocity;
        });

        require_throws(
            [&]()
            {
                processor.queue_into_sim<Velocity>("writer", [](Velocity& velocity)
                {
                    velocity = 2;
                });
            },
            "non-buffered read/write same-wall conflict was not rejected"
        );
    }

    void test_view_cache_invalidates_on_storage_changes()
    {
        ECS ecs;
        const Entity entity = ecs.createEntity();

        auto velocityView = ecs.view<Velocity>();
        require(velocityView.size() == 0, "new component view should be empty");

        ecs.emplaceComponent<Velocity>(entity, 1);
        require(velocityView.size() == 1, "component view did not observe later component insertion");

        auto pairView = ecs.view<Velocity, Health>();
        require(pairView.size() == 0, "pair view should not match before second component insertion");

        ecs.emplaceComponent<Health>(entity, 10);
        require(pairView.size() == 1, "pair view did not invalidate after component insertion");

        ecs.removeComponent<Health>(entity);
        require(pairView.size() == 0, "pair view did not invalidate after component removal");
    }

    void test_dirty_entity_render_transfer_preserves_unchanged_entities()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        const Entity first = ecs.createEntity();
        const Entity second = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(first, 1);
        ecs.emplaceComponent<Velocity>(second, 2);

        std::atomic<int> sum = 0;
        processor.queue_into_rendering<Velocity>("sum", [&](const Velocity& velocity)
        {
            sum.fetch_add(static_cast<int>(velocity), std::memory_order_relaxed);
        });

        processor.simulate();
        sum = 0;
        processor.render();
        sum = 0;
        processor.render();
        require(sum.load() == 3, "initial full render transfer failed");

        *ecs.try_get_mut<Velocity>(first) = 10;
        processor.simulate();
        sum = 0;
        processor.render();
        sum = 0;
        processor.render();
        require(sum.load() == 12, "dirty transfer failed after first entity update");

        *ecs.try_get_mut<Velocity>(second) = 20;
        processor.simulate();
        sum = 0;
        processor.render();
        sum = 0;
        processor.render();
        require(sum.load() == 30, "dirty transfer rolled back an unchanged render entity");
    }

    void test_buffered_simulation_write_transfers_to_render()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        const Entity entity = ecs.createEntity();
        ecs.emplaceComponent<Position>(entity, 1);

        std::atomic<int> sum = 0;
        processor.queue_into_rendering<Position>("sum", [&](const Position& position)
        {
            sum.fetch_add(static_cast<int>(position), std::memory_order_relaxed);
        });

        processor.simulate();
        sum = 0;
        processor.render();
        sum = 0;
        processor.render();
        require(sum.load() == 1, "initial buffered render transfer failed");

        processor.queue_into_sim<Position>("increment", [](Position& position)
        {
            position = static_cast<int>(position) + 1;
        });

        processor.simulate();
        sum = 0;
        processor.render();
        sum = 0;
        processor.render();
        require(sum.load() == 2, "scheduled buffered write did not transfer to render");
    }

    void test_unwritten_buffered_pool_does_not_swap_on_unrelated_wall()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        const Entity entity = ecs.createEntity();
        ecs.emplaceComponent<Position>(entity, 5);
        ecs.emplaceComponent<Velocity>(entity, 1);

        *ecs.try_get_mut<Position>(entity) = 6;
        processor.simulate();
        require(static_cast<int>(*ecs.try_get<Position>(entity)) == 6, "direct buffered mutation was not flushed");

        processor.queue_into_sim<Velocity>("velocity-only", [](Velocity& velocity)
        {
            velocity = static_cast<int>(velocity) + 1;
        });

        processor.simulate();
        require(
            static_cast<int>(*ecs.try_get<Position>(entity)) == 6,
            "unwritten buffered component rolled back after unrelated simulation job"
        );
    }

    void test_dirty_threshold_promotes_to_full_transfer()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        ArrayList<Entity> entities;
        for (int i = 0; i < 5; ++i)
        {
            const Entity entity = ecs.createEntity();
            entities.append(entity);
            ecs.emplaceComponent<Velocity>(entity, i);
        }

        processor.queue_into_rendering<Velocity>("noop", [](const Velocity& velocity)
        {
            (void)velocity;
        });

        processor.simulate();
        processor.render();
        processor.render();

        CapturingLogger logger;
        processor.setSchedulerLogger(&logger);

        for (const Entity& entity : entities)
            *ecs.try_get_mut<Velocity>(entity) = 100;

        processor.simulate();
        logger.logHistory();

        require(
            contains_line(logger.lines, "mode=full"),
            "dirty threshold did not promote broad dirty set to full transfer"
        );
    }

    void test_dirty_wrapper_marks_only_touched_entities()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        for (int i = 0; i < 100; ++i)
        {
            const Entity entity = ecs.createEntity();
            ecs.emplaceComponent<Velocity>(entity, i);
            if (i < 5)
                ecs.emplaceComponent<Health>(entity, 1);
        }

        processor.queue_into_rendering<Velocity>("noop", [](const Velocity& velocity)
        {
            (void)velocity;
        });

        processor.simulate();
        processor.render();
        processor.render();

        CapturingLogger logger;
        processor.setSchedulerLogger(&logger);
        processor.queue_into_sim<ecs::Dirty<Velocity>, Health>(
            "mark-velocity-dirty",
            [](Velocity& velocity, const Health& health)
        {
            (void)health;
            velocity = static_cast<int>(velocity) + 1000;
        });

        processor.simulate();
        logger.logHistory();

        require(
            contains_line(logger.lines, "mode=entities count=5"),
            "ecs::Dirty<T> wrapper did not track only touched entities"
        );
    }

    void test_dirty_wrapper_marks_only_touched_archetyped_entities()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        processor.registerArchetype<Velocity, Health>();

        for (int i = 0; i < 100; ++i)
        {
            const Entity entity = ecs.createEntity();
            ecs.emplaceComponent<Velocity>(entity, i);
            if (i < 5)
                ecs.emplaceComponent<Health>(entity, 1);
        }

        processor.queue_into_rendering<Velocity>("noop", [](const Velocity& velocity)
        {
            (void)velocity;
        });

        processor.simulate();
        processor.render();
        processor.render();

        CapturingLogger logger;
        processor.setSchedulerLogger(&logger);
        processor.queue_into_sim<ecs::Dirty<Velocity>, Health>(
            "mark-archetype-velocity-dirty",
            [](Velocity& velocity, const Health& health)
        {
            (void)health;
            velocity = static_cast<int>(velocity) + 1000;
        });

        processor.simulate();
        logger.logHistory();

        require(
            contains_line(logger.lines, "archetype component type=") &&
                contains_line(logger.lines, "mode=entities count=5"),
            "ecs::Dirty<T> wrapper did not track only touched archetyped entities"
        );
    }

    void test_dirty_wrapper_marks_full_when_touched_count_reaches_threshold()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        for (int i = 0; i < 100; ++i)
        {
            const Entity entity = ecs.createEntity();
            ecs.emplaceComponent<Velocity>(entity, i);
            if (i < 64)
                ecs.emplaceComponent<Health>(entity, 1);
        }

        processor.queue_into_rendering<Velocity>("noop", [](const Velocity& velocity)
        {
            (void)velocity;
        });

        processor.simulate();
        processor.render();
        processor.render();

        CapturingLogger logger;
        processor.setSchedulerLogger(&logger);
        processor.queue_into_sim<ecs::Dirty<Velocity>, Health>(
            "mark-velocity-dirty",
            [](Velocity& velocity, const Health& health)
        {
            (void)health;
            velocity = static_cast<int>(velocity) + 1000;
        });

        processor.simulate();
        logger.logHistory();

        require(
            contains_line(logger.lines, "mode=full"),
            "ecs::Dirty<T> wrapper did not mark full when touched count reached the dirty threshold"
        );
    }

    void test_global_dirty_wrapper_matches_namespaced_dirty_wrapper()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        for (int i = 0; i < 100; ++i)
        {
            const Entity entity = ecs.createEntity();
            ecs.emplaceComponent<Velocity>(entity, i);
            if (i < 5)
                ecs.emplaceComponent<Health>(entity, 1);
        }

        processor.queue_into_rendering<Velocity>("noop", [](const Velocity& velocity)
        {
            (void)velocity;
        });

        processor.simulate();
        processor.render();
        processor.render();

        CapturingLogger logger;
        processor.setSchedulerLogger(&logger);
        processor.queue_into_sim<Dirty<Velocity>, Health>(
            "mark-velocity-dirty",
            [](Velocity& velocity, const Health& health)
        {
            (void)health;
            velocity = static_cast<int>(velocity) + 1000;
        });

        processor.simulate();
        logger.logHistory();

        require(
            contains_line(logger.lines, "mode=entities count=5"),
            "Dirty<T> wrapper did not match ecs::Dirty<T> dirty transfer behaviour"
        );
    }

    void test_structural_changes_are_deferred_until_wall_finishes()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();
        processor.createWall("AFTER_CREATE", 2);

        Entity created;
        int sameWallHealthCount = 0;
        int nextWallHealthCount = 0;

        processor.queue_into_sim<>("create-entity", [&]()
        {
            created = ecs.createEntity();
            Health& staged = ecs.emplaceComponent<Health>(created, 1);
            staged = 7;
            require(!ecs.hasEntity(created), "deferred entity became visible inside the creating wall");
            require(!ecs.hasComponent<Health>(created), "deferred component became visible inside the creating wall");
        });

        processor.queue_into_sim<Health>("same-wall-reader", [&](const Health& health)
        {
            (void)health;
            ++sameWallHealthCount;
        });

        processor.queue_into_sim<Health>("next-wall-reader", "AFTER_CREATE", [&](const Health& health)
        {
            (void)health;
            ++nextWallHealthCount;
        });

        processor.simulate();

        require(sameWallHealthCount == 0, "deferred component was observed by another job in the same wall");
        require(nextWallHealthCount == 1, "deferred component was not visible to the next wall");
        require(ecs.hasEntity(created), "deferred entity was not created after wall flush");
        require(ecs.hasComponent<Health>(created), "deferred component was not created after wall flush");
        require(static_cast<int>(*ecs.try_get<Health>(created)) == 7, "deferred component did not flush staged value");
    }

    void test_deferred_destroy_and_component_removal_are_invisible_until_wall_finishes()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        const Entity entity = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(entity, 3);
        ecs.emplaceComponent<Health>(entity, 9);

        int sameWallVelocityCount = 0;
        processor.queue_into_sim<>("destroy-and-remove", [&]()
        {
            ecs.removeComponent<Health>(entity);
            ecs.destroyEntity(entity);
            require(ecs.hasEntity(entity), "deferred destroy became visible inside the destroying wall");
            require(ecs.hasComponent<Health>(entity), "deferred component removal became visible inside the destroying wall");
        });

        processor.queue_into_sim<Velocity>("same-wall-velocity-reader", [&](const Velocity& velocity)
        {
            (void)velocity;
            ++sameWallVelocityCount;
        });

        processor.simulate();

        require(sameWallVelocityCount == 1, "deferred destroy hid the entity from another job in the same wall");
        require(!ecs.hasEntity(entity), "deferred destroy did not flush after wall");
        require(!ecs.hasComponent<Velocity>(entity), "deferred destroy did not remove component storage after wall");
    }

    void test_scheduler_logging()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        CapturingLogger logger;
        processor.setSchedulerLogger(&logger);

        processor.queue_into_sim<Velocity>("move", [](Velocity& velocity)
        {
            velocity = 1;
        });
        processor.queue_into_rendering<Velocity>("draw", [](const Velocity& velocity)
        {
            (void)velocity;
        });

        processor.simulate();
        processor.render();
        logger.logHistory();

        require(contains_line(logger.lines, "queued simulation job"), "scheduler logger missed sim queue event");
        require(contains_line(logger.lines, "queued rendering job"), "scheduler logger missed render queue event");
        require(contains_line(logger.lines, "running simulation wall"), "scheduler logger missed wall execution event");
    }
}

int main()
{
    try
    {
        test_sparse_tuple_component_storage_accesses_components();
        test_archetype_registration_tracks_components();
        test_archetype_registration_merges_superset_groups();
        test_archetype_rows_track_partial_component_presence();
        test_archetype_component_removal_keeps_other_components();
        test_archetype_registration_migrates_existing_standalone_components();
        test_archetype_registration_rejects_incomplete_overlap();
        test_archetype_view_uses_tuple_pool_with_standalone_components();
        test_archetype_sim_job_mutates_tuple_components();
        test_archetype_view_of_iterates_requested_components();
        test_view_of_read_conflicts_with_nonbuffered_writer();
        test_view_of_can_be_used_in_component_job();
        test_archetype_render_transfer_uses_tuple_pool_dirty_state();
        test_sim_archetype_does_not_create_render_archetype();
        test_explicit_render_archetype_transfers_and_renders();
        test_render_archetype_registration_migrates_existing_render_sparse_pool();
        test_component_type_ids_are_stable_and_distinct();
        test_buffered_write_write_conflict();
        test_buffered_read_write_is_allowed();
        test_nonbuffered_read_write_conflict();
        test_view_cache_invalidates_on_storage_changes();
        test_dirty_entity_render_transfer_preserves_unchanged_entities();
        test_buffered_simulation_write_transfers_to_render();
        test_unwritten_buffered_pool_does_not_swap_on_unrelated_wall();
        test_dirty_threshold_promotes_to_full_transfer();
        test_dirty_wrapper_marks_only_touched_entities();
        test_dirty_wrapper_marks_only_touched_archetyped_entities();
        test_dirty_wrapper_marks_full_when_touched_count_reaches_threshold();
        test_global_dirty_wrapper_matches_namespaced_dirty_wrapper();
        test_structural_changes_are_deferred_until_wall_finishes();
        test_deferred_destroy_and_component_removal_are_invisible_until_wall_finishes();
        test_scheduler_logging();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[FAIL] " << exception.what() << '\n';
        return 1;
    }

    std::cout << "[PASS] ECS tests\n";
    return 0;
}
