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
    sim.queue_into_sim<Read<Velocity3D>, ReadWrite<Position3D>>(
        "MOVEMENT_SYSTEM_3D",
        [&engine](Read<Velocity3D>& velocity, ReadWrite<Position3D>& position)
        {
            const float deltaTime = engine.upsMs();
            position.dst = position.src + (velocity.src * deltaTime);
        }
    );
}

// rendering
void entityRenderer(Engine&, ECSProcessor& sim)
{
    sim.queue_into_rendering<Position3D>(
        "ENTITY_RENDERING_EXAMPLE",
        [](const auto&)
        {
            // do some kind of per-entity rendering here
        }
    );
}
