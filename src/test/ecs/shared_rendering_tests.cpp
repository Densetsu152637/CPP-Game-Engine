#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "ecs/aliases/component_alias.h"
#include "ecs/jobs/ecs_sim_arg_traits.h"
#include "ecs/processor.h"
#include "test/test_assertions.h"

namespace
{
    struct SharedPositionTag {};
    struct SharedMeshTag {};
    struct SharedMaterialTag {};

    using SharedPosition = ecs::Alias<int, SharedPositionTag>;

    struct SharedMeshValue
    {
        int id = 0;

        bool operator==(const SharedMeshValue&) const = default;
    };

    struct SharedMaterialValue
    {
        int id = 0;

        bool operator==(const SharedMaterialValue&) const = default;
    };

    using SharedMesh = ecs::SharedAlias<SharedMeshValue, SharedMeshTag>;
    using SharedMaterial = ecs::SharedAlias<SharedMaterialValue, SharedMaterialTag>;
}

void test_shared_render_jobs_group_by_unique_component()
{
    Threadpool pool(2, std::string("shared-render-test"));
    ECSProcessor processor(pool);
    ECS& ecs = processor.ecs();

    const Entity first = ecs.createEntity();
    const Entity second = ecs.createEntity();
    const Entity third = ecs.createEntity();
    const Entity fourth = ecs.createEntity();

    ecs.emplaceComponent<SharedPosition>(first, 1);
    ecs.emplaceComponent<SharedPosition>(second, 2);
    ecs.emplaceComponent<SharedPosition>(third, 10);
    ecs.emplaceComponent<SharedPosition>(fourth, 20);

    ecs.emplaceComponent<SharedMesh>(first, SharedMeshValue{1});
    ecs.emplaceComponent<SharedMesh>(second, SharedMeshValue{1});
    ecs.emplaceComponent<SharedMesh>(third, SharedMeshValue{2});
    ecs.emplaceComponent<SharedMesh>(fourth, SharedMeshValue{2});

    std::atomic<int> meshOneRunning = 0;
    std::atomic<int> meshTwoRunning = 0;
    std::atomic<int> visited = 0;
    std::atomic<bool> sameMeshOverlap = false;

    processor.queue_into_rendering<SharedPosition, ecs::Shared<SharedMesh>>(
        "shared-mesh-render",
        [&](const SharedPosition& position)
        {
            std::atomic<int>& running = static_cast<int>(position) < 10
                ? meshOneRunning
                : meshTwoRunning;

            if (running.fetch_add(1, std::memory_order_acq_rel) != 0)
                sameMeshOverlap.store(true, std::memory_order_release);

            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            visited.fetch_add(1, std::memory_order_acq_rel);
            running.fetch_sub(1, std::memory_order_acq_rel);
        }
    );

    processor.simulate();
    processor.render();
    visited = 0;
    sameMeshOverlap = false;

    processor.render();
    test::require(visited.load(std::memory_order_acquire) == 4, "shared render job did not visit all entities");
    test::require(!sameMeshOverlap.load(std::memory_order_acquire), "shared render job overlapped entities from the same shared mesh");
}

void test_shared_sim_jobs_group_by_shared_alias()
{
    Threadpool pool(2, std::string("shared-sim-test"));
    ECSProcessor processor(pool);
    ECS& ecs = processor.ecs();

    const Entity first = ecs.createEntity();
    const Entity second = ecs.createEntity();
    const Entity third = ecs.createEntity();
    const Entity fourth = ecs.createEntity();
    const Entity missingMesh = ecs.createEntity();

    ecs.emplaceComponent<SharedPosition>(first, 1);
    ecs.emplaceComponent<SharedPosition>(second, 2);
    ecs.emplaceComponent<SharedPosition>(third, 10);
    ecs.emplaceComponent<SharedPosition>(fourth, 20);
    ecs.emplaceComponent<SharedPosition>(missingMesh, 99);
    ecs.emplaceComponent<SharedMesh>(first, SharedMeshValue{1});
    ecs.emplaceComponent<SharedMesh>(second, SharedMeshValue{1});
    ecs.emplaceComponent<SharedMesh>(third, SharedMeshValue{2});
    ecs.emplaceComponent<SharedMesh>(fourth, SharedMeshValue{2});

    std::atomic<int> meshOneRunning = 0;
    std::atomic<int> meshTwoRunning = 0;
    std::atomic<int> visited = 0;
    std::atomic<bool> sameMeshOverlap = false;
    processor.queue_into_sim<SharedPosition, ecs::Shared<SharedMesh>>(
        "shared-sim-filter",
        [&](SharedPosition& position, SharedMesh& mesh)
        {
            const int meshId = static_cast<SharedMeshValue&>(mesh).id;
            std::atomic<int>& running = 1 == meshId ? meshOneRunning : meshTwoRunning;

            if (running.fetch_add(1, std::memory_order_acq_rel) != 0)
                sameMeshOverlap.store(true, std::memory_order_release);

            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            position = static_cast<int>(position) + 10;
            visited.fetch_add(1, std::memory_order_acq_rel);
            running.fetch_sub(1, std::memory_order_acq_rel);
        }
    );

    processor.simulate();

    test::require(visited.load(std::memory_order_acquire) == 4, "shared sim job did not filter by SharedAlias");
    test::require(!sameMeshOverlap.load(std::memory_order_acquire), "shared sim job overlapped entities from the same shared mesh");
    test::require(static_cast<int>(*ecs.try_get<SharedPosition>(first)) == 11, "shared sim job missed first entity");
    test::require(static_cast<int>(*ecs.try_get<SharedPosition>(second)) == 12, "shared sim job missed second entity");
    test::require(static_cast<int>(*ecs.try_get<SharedPosition>(third)) == 20, "shared sim job missed third entity");
    test::require(static_cast<int>(*ecs.try_get<SharedPosition>(fourth)) == 30, "shared sim job missed fourth entity");
    test::require(static_cast<int>(*ecs.try_get<SharedPosition>(missingMesh)) == 99, "shared sim job visited entity without shared alias");
}

void test_shared_render_jobs_require_only_one_shared_component()
{
    using NoShared = ecs_sim::shared_component_list_t<SharedPosition>;
    using OneShared = ecs_sim::shared_component_list_t<SharedPosition, ecs::Shared<SharedMesh>>;
    using TwoShared = ecs_sim::shared_component_list_t<
        SharedPosition,
        ecs::Shared<SharedMesh>,
        ecs::Shared<SharedMaterial>
    >;

    test::require(ecs_sim::type_list_size_v<NoShared> == 0, "shared trait found a shared component where none was submitted");
    test::require(ecs_sim::type_list_size_v<OneShared> == 1, "shared trait missed a submitted shared component");
    test::require(ecs_sim::type_list_size_v<TwoShared> == 2, "shared trait failed to count multiple shared components");
}
