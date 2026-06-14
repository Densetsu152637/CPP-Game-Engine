#include <atomic>

#include "test/ecs/ecs_test_fixtures.h"

using namespace ecs_test;

void test_tagged_sim_job_filters_component_iteration()
{
    Threadpool pool(1, std::string("ecs-test"));
    ECSProcessor processor(pool);
    ECS& ecs = processor.ecs();

    const Entity first = ecs.createEntity();
    const Entity second = ecs.createEntity();
    const Entity third = ecs.createEntity();
    ecs.emplaceComponent<Velocity>(first, 1);
    ecs.emplaceComponent<Velocity>(second, 2);
    ecs.emplaceComponent<Velocity>(third, 3);
    ecs.addTag<RenderableTag>(first);
    ecs.addTag<RenderableTag>(third);

    processor.queue_into_sim<ecs::Tag<RenderableTag>, Velocity>(
        "tagged-velocity",
        [](Velocity& velocity)
    {
        velocity = static_cast<int>(velocity) + 10;
    });

    processor.simulate();

    require(static_cast<int>(*ecs.try_get<Velocity>(first)) == 11, "tagged sim job missed first tagged entity");
    require(static_cast<int>(*ecs.try_get<Velocity>(second)) == 2, "tagged sim job touched untagged entity");
    require(static_cast<int>(*ecs.try_get<Velocity>(third)) == 13, "tagged sim job missed later tagged entity");
}

void test_tagged_dirty_job_tracks_filtered_entity_count()
{
    Threadpool pool(1, std::string("ecs-test"));
    ECSProcessor processor(pool);
    ECS& ecs = processor.ecs();

    for (int i = 0; i < 20; ++i)
    {
        const Entity entity = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(entity, i);
        if (i < 3)
            ecs.addTag<RenderableTag>(entity);
    }

    processor.queue_into_rendering<Velocity>("noop-tagged-dirty", [](const Velocity& velocity)
    {
        (void)velocity;
    });

    processor.simulate();
    processor.render();
    processor.render();

    CapturingLogger logger;
    processor.setSchedulerLogger(&logger);
    processor.queue_into_sim<ecs::Tag<RenderableTag>, ecs::Dirty<Velocity>>(
        "tagged-dirty",
        [](Velocity& velocity)
    {
        velocity = static_cast<int>(velocity) + 100;
    });

    processor.simulate();
    logger.logHistory();

    require(
        contains_line(logger.lines, "mode=entities count=3"),
        "tagged dirty job did not use the filtered tagged entity count"
    );
}

void test_tag_only_sim_job_iterates_tagged_entities()
{
    Threadpool pool(1, std::string("ecs-test"));
    ECSProcessor processor(pool);
    ECS& ecs = processor.ecs();

    const Entity first = ecs.createEntity();
    const Entity second = ecs.createEntity();
    const Entity third = ecs.createEntity();
    ecs.addTag<SelectedTag>(first);
    ecs.addTag<SelectedTag>(third);

    int count = 0;
    size_t indexSum = 0;
    processor.queue_into_sim<ecs::Tag<SelectedTag>, Entity>(
        "tag-only-entities",
        [&](const Entity entity)
    {
        ++count;
        indexSum += entity.index;
    });

    processor.simulate();

    require(count == 2, "tag-only sim job iterated wrong number of entities");
    require(indexSum == first.index + third.index, "tag-only sim job iterated wrong entities");
    require(!ecs.hasTag<SelectedTag>(second), "tag-only sim test accidentally tagged middle entity");
}

void test_exclude_filters_in_sim_jobs()
{
    Threadpool pool(1, std::string("ecs-test"));
    ECSProcessor processor(pool);
    ECS& ecs = processor.ecs();

    const Entity first = ecs.createEntity();
    const Entity second = ecs.createEntity();
    const Entity third = ecs.createEntity();
    ecs.emplaceComponent<Velocity>(first, 1);
    ecs.emplaceComponent<Velocity>(second, 2);
    ecs.emplaceComponent<Velocity>(third, 3);
    ecs.emplaceComponent<Health>(third, 100);
    ecs.addTag<SelectedTag>(second);

    processor.queue_into_sim<Velocity, ecs::Exclude<ecs::Tag<SelectedTag>>, ecs::Exclude<Health>>(
        "exclude-sim",
        [](Velocity& velocity)
    {
        velocity = static_cast<int>(velocity) + 10;
    });

    processor.simulate();

    require(static_cast<int>(*ecs.try_get<Velocity>(first)) == 11, "exclude sim job missed included entity");
    require(static_cast<int>(*ecs.try_get<Velocity>(second)) == 2, "exclude sim job touched excluded tag");
    require(static_cast<int>(*ecs.try_get<Velocity>(third)) == 3, "exclude sim job touched excluded component");
}

void test_tagged_render_job_filters_render_iteration()
{
    Threadpool pool(1, std::string("ecs-test"));
    ECSProcessor processor(pool);
    ECS& ecs = processor.ecs();

    const Entity first = ecs.createEntity();
    const Entity second = ecs.createEntity();
    const Entity third = ecs.createEntity();
    ecs.emplaceComponent<Velocity>(first, 1);
    ecs.emplaceComponent<Velocity>(second, 2);
    ecs.emplaceComponent<Velocity>(third, 3);
    ecs.addTag<RenderableTag>(first);
    ecs.addTag<RenderableTag>(third);

    std::atomic<int> sum = 0;
    processor.queue_into_rendering<ecs::Tag<RenderableTag>, Velocity>(
        "tagged-render",
        [&](const Velocity& velocity)
    {
        sum.fetch_add(static_cast<int>(velocity), std::memory_order_relaxed);
    });

    processor.simulate();
    sum = 0;
    processor.render();
    sum = 0;
    processor.render();
    require(sum.load() == 4, "tagged render job did not filter render entities");

    ecs.removeTag<RenderableTag>(third);
    sum = 0;
    processor.render();
    require(sum.load() == 1, "tagged render job did not observe removed tag");
}

void test_exclude_filters_in_render_jobs()
{
    Threadpool pool(1, std::string("ecs-test"));
    ECSProcessor processor(pool);
    ECS& ecs = processor.ecs();

    const Entity first = ecs.createEntity();
    const Entity second = ecs.createEntity();
    const Entity third = ecs.createEntity();
    ecs.emplaceComponent<Velocity>(first, 1);
    ecs.emplaceComponent<Velocity>(second, 2);
    ecs.emplaceComponent<Velocity>(third, 3);
    ecs.addTag<SelectedTag>(second);

    std::atomic<int> sum = 0;
    processor.queue_into_rendering<Velocity, ecs::Exclude<ecs::Tag<SelectedTag>>>(
        "exclude-render",
        [&](const Velocity& velocity)
    {
        sum.fetch_add(static_cast<int>(velocity), std::memory_order_relaxed);
    });

    processor.simulate();
    sum = 0;
    processor.render();
    sum = 0;
    processor.render();

    require(sum.load() == 4, "exclude render job did not filter excluded tag");
}
