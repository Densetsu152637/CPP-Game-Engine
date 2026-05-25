//
// Created by Nicholas on 07/05/26.
//

#include "systems.h"

#include "../components/alias.h"
#include "core/engine.h"

void movementSystem(Engine& engine, ECSSimulator& sim)
{
    sim.submit<Read<Velocity3D>, ReadWrite<Position3D>>(
        "MOVEMENT_SYSTEM_3D",
        [&engine](auto& velocity, auto& position)
        {
            const float deltaTime = engine.upsMs();
            position.dst = position.src + (velocity.src * deltaTime);
        }
    );
}
