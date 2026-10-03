#include "test/ecs/ecs_test_fixtures.h"

using namespace ecs_test;

void test_component_type_ids_are_stable_and_distinct()
{
    const ecs::ComponentTypeId firstPositionId = ecs::component_type_id<Position>();
    const ecs::ComponentTypeId secondPositionId = ecs::component_type_id<Position>();
    const ecs::ComponentTypeId velocityId = ecs::component_type_id<Velocity>();

    require(firstPositionId == secondPositionId, "component type ID was not stable for the same type");
    require(firstPositionId != velocityId, "component type IDs were not distinct across component types");
}
