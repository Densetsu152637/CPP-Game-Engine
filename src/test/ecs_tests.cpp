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

    using Position = ecs::BufferedAlias<int, PositionTag>;
    using Velocity = ecs::Alias<int, VelocityTag>;
    using Health = ecs::Alias<int, HealthTag>;

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

    void test_dirty_wrapper_forces_full_transfer()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        for (int i = 0; i < 5; ++i)
        {
            const Entity entity = ecs.createEntity();
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
        processor.queue_into_sim<ecs::Dirty<Velocity>>("mark-velocity-dirty", [](Velocity& velocity)
        {
            velocity = static_cast<int>(velocity);
        });

        processor.simulate();
        logger.logHistory();

        require(
            contains_line(logger.lines, "mode=full"),
            "ecs::Dirty<T> wrapper did not force a full render transfer"
        );
    }

    void test_global_dirty_wrapper_matches_namespaced_dirty_wrapper()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        const Entity entity = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(entity, 1);

        processor.queue_into_rendering<Velocity>("noop", [](const Velocity& velocity)
        {
            (void)velocity;
        });

        processor.simulate();
        processor.render();
        processor.render();

        CapturingLogger logger;
        processor.setSchedulerLogger(&logger);
        processor.queue_into_sim<Dirty<Velocity>>("mark-velocity-dirty", [](Velocity& velocity)
        {
            velocity = static_cast<int>(velocity);
        });

        processor.simulate();
        logger.logHistory();

        require(
            contains_line(logger.lines, "mode=full"),
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
        test_component_type_ids_are_stable_and_distinct();
        test_buffered_write_write_conflict();
        test_buffered_read_write_is_allowed();
        test_nonbuffered_read_write_conflict();
        test_view_cache_invalidates_on_storage_changes();
        test_dirty_entity_render_transfer_preserves_unchanged_entities();
        test_buffered_simulation_write_transfers_to_render();
        test_unwritten_buffered_pool_does_not_swap_on_unrelated_wall();
        test_dirty_threshold_promotes_to_full_transfer();
        test_dirty_wrapper_forces_full_transfer();
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
