//
// Created by Nicholas on 07/05/26.
//

#pragma once

#include "../ecs/ecs.h"
#include "../core/engine.h"

// simulation
void movementSystem(Engine& engine, ECSProcessor& sim);




// rendering
void entityRenderer(Engine& engine, ECSProcessor& sim);
