#include <atomic>

#include "ecs/pools/component_pool_storage.h"
#include "test/ecs/ecs_test_fixtures.h"

using namespace ecs_test;

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

void test_view_of_const_multi_component_each()
{
    Threadpool pool(1, std::string("ecs-test"));
    ECSProcessor processor(pool);
    ECS& ecs = processor.ecs();

    processor.registerArchetype<Velocity, Health>();

    const Entity first = ecs.createEntity();
    ecs.emplaceComponent<Velocity>(first, 3);
    ecs.emplaceComponent<Health>(first, 7);

    const Entity second = ecs.createEntity();
    ecs.emplaceComponent<Velocity>(second, 5);
    ecs.emplaceComponent<Health>(second, 11);

    int sum = 0;
    processor.queue_into_sim<ecs::ViewOf<Velocity, Health>>(
        "const-multi-view-of",
        [&](const View<Velocity, Health>& pairs)
    {
        pairs.each([&](const Velocity& velocity, const Health& health)
        {
            sum += static_cast<int>(velocity);
            sum += static_cast<int>(health);
        });
    });

    processor.simulate();

    require(sum == 26, "const multi-component ecs::ViewOf<T...> did not iterate read-only components");
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
    require(sum.load() == 0, "explicit render archetype exposed write-buffer data before publish");
    sum = 0;
    processor.render();
    require(sum.load() == 10, "explicit render archetype did not render transferred components after publish");

    *ecs.try_get_mut<Velocity>(entity) = 5;
    processor.simulate();
    sum = 0;
    processor.render();
    require(sum.load() == 10, "explicit render archetype did not preserve the previous read snapshot");
    sum = 0;
    processor.render();
    require(sum.load() == 12, "explicit render archetype did not receive dirty component updates after publish");

    ecs.removeComponent<Velocity>(entity);
    processor.simulate();
    sum = 0;
    processor.render();
    require(sum.load() == 12, "explicit render archetype removal did not preserve the previous read snapshot");
    sum = 0;
    processor.render();
    require(sum.load() == 0, "explicit render archetype did not publish dirty component removal");
}

void test_direct_mutable_view_marks_components_dirty()
{
    Threadpool pool(1, std::string("ecs-test"));
    ECSProcessor processor(pool);
    ECS& ecs = processor.ecs();

    const Entity entity = ecs.createEntity();
    ecs.emplaceComponent<Velocity>(entity, 3);

    std::atomic<int> sum = 0;
    processor.queue_into_rendering<Velocity>("sum-direct-view-write", [&](const Velocity& velocity)
    {
        sum.fetch_add(static_cast<int>(velocity), std::memory_order_relaxed);
    });

    processor.simulate();
    sum = 0;
    processor.render();
    sum = 0;
    processor.render();
    require(sum.load() == 3, "initial direct view dirty test render transfer failed");

    auto view = ecs.view<Velocity>();
    view.each([](Velocity& velocity)
    {
        velocity = 9;
    });

    processor.simulate();
    sum = 0;
    processor.render();
    sum = 0;
    processor.render();
    require(sum.load() == 9, "direct mutable View::each did not mark the component dirty");
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

void test_render_archetype_registration_preserves_pending_sparse_publish()
{
    Threadpool pool(1, std::string("ecs-test"));
    ECSProcessor processor(pool);
    ECS& ecs = processor.ecs();

    const Entity entity = ecs.createEntity();
    ecs.emplaceComponent<Velocity>(entity, 4);

    std::atomic<int> sum = 0;
    processor.queue_into_rendering<Velocity>("sum-pending-migrated-render", [&](const Velocity& velocity)
    {
        sum.fetch_add(static_cast<int>(velocity), std::memory_order_relaxed);
    });

    processor.simulate();
    processor.registerRenderArchetype<Velocity, Health>();

    sum = 0;
    processor.render();
    require(sum.load() == 0, "pending sparse-to-archetype migration exposed unpublished write data");

    sum = 0;
    processor.render();
    require(sum.load() == 4, "pending sparse-to-archetype migration lost the pending publish");
}
