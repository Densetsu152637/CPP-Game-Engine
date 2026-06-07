//
// Created by Nicholas on 07/05/26.
//

#include "systems.h"
#include "../components/alias.h"
#include "core/engine.h"
#include "ecs/ecs.h"
#include "ecs/processor.h"

// simulation
void movementSystem(Engine& engine, ECSProcessor& sim)
{
    sim.queue_into_sim<Velocity3D, ecs::Dirty<Position3D>>(
        "MOVEMENT_SYSTEM_3D",
        [&engine](const Velocity3D& velocity, Position3D& position)
        {
            const float deltaTime = engine.upsMs();
            position = position + (velocity * deltaTime);
        }
    );
}

void dirtyCollisionSystemExample(Engine& engine, ECSProcessor& sim)
{
    sim.queue_into_sim<ecs::Dirty<Position3D>, CollisionSurface, ArrayFor<CollisionSurface>>(
        "COLLISION_SYSTEM_EXAMPLE",
        [](
            Position3D& position,
            const CollisionSurface& localSurface,
            const ArrayList<CollisionSurface>& surfaces
        )
        {
            // transform surface into position
            for (const auto& surface : surfaces)
            {
                // check if the surface intersects the transformed surface
            }
        }
    );
}

// rendering
void entityRenderingExample(Engine&, ECSProcessor& sim)
{
    sim.queue_into_rendering<Position3D>(
        "ENTITY_RENDERING_EXAMPLE",
        [](const Position3D& position)
        {
            // do some kind of per-entity rendering here
        }
    );
}
