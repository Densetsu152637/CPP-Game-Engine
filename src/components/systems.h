//
// Created by Nicholas on 07/05/26.
//

#pragma once

#include "../ecs/ecs.h"
#include "../core/engine.h"
#include "rendering/renderer.h"

// simulation
void movementSystem(Engine& engine, ECSProcessor& sim);
void dirtyCollisionSystemExample(Engine& engine, ECSProcessor& sim);

// rendering
void entityRenderingExample(Engine& engine, ECSProcessor& sim, IRenderer* renderer, IShader* shader);
void entityRenderingWithTagExample(Engine& engine, ECSProcessor& sim);
