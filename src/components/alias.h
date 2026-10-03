//
// Created by Nicholas on 07/05/26.
//

#pragma once

#include <cstddef>

#include "../ecs/aliases/component_alias.h"
#include "../structs/vector.h"

// aliases

struct Position3DTag {};

using Position3D = ecs::BufferedAlias<Vector3f, Position3DTag>;
