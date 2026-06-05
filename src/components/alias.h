//
// Created by Nicholas on 07/05/26.
//

#pragma once

#include "../ecs/component_alias.h"
#include "../structs/vector.h"

struct Position3DTag {};
struct Velocity3DTag {};

using Position3D = ecs::Alias<Vector3f, Position3DTag>;
using Velocity3D = ecs::Alias<Vector3f, Velocity3DTag>;

