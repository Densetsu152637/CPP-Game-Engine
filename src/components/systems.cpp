//
// Created by Nicholas on 07/05/26.
//

#include "systems.h"
#include "../components/alias.h"
#include "core/engine.h"
#include "ecs/ecs.h"
#include "ecs/processor.h"
#include "rendering/ecs_rendering.h"

#include <stdexcept>
#include <utility>

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

void dirtyCollisionSystemExample(Engine&, ECSProcessor& sim)
{
    sim.queue_into_sim<ecs::Dirty<Position3D>, CollisionSurface, ecs::ViewOf<CollisionSurface>>(
        "COLLISION_SYSTEM_EXAMPLE",
        [](
            Position3D& position,
            const CollisionSurface& localSurface,
            const View<CollisionSurface>& surfaces
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

struct ExampleTag{};

void entityRenderingExample(Engine&, ECSProcessor& sim, IRenderer* renderer, IShader* shader)
{
    const rendering::ShaderBinding<Position3D, Velocity3D> binding(
        {"position", "velocity"}
    );

    rendering::queue_shader_rendering<Position3D, Velocity3D, ecs::Shared<Mesh>, ecs::Tag<ExampleTag>>(
        sim,
        "ENTITY_RENDERING_EXAMPLE",
        renderer,
        shader,
        binding
    );
}

void entityRenderingWithTagExample(Engine&, ECSProcessor& sim)
{
    sim.queue_into_rendering<Position3D, ecs::Tag<ExampleTag>>(
        "ENTITY_RENDERING_WITH_TAG_EXAMPLE",
        [](const Position3D& position)
        {
            // do some kind of per-entity rendering here
        }
    );
}
