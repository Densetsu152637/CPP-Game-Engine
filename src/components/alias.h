//
// Created by Nicholas on 07/05/26.
//

#pragma once

#include "../ecs/aliases/component_alias.h"
#include "../structs/vector.h"

// structs

struct Surface3D
{
    union
    {
        struct
        {
            Vector3f p1;
            Vector3f p2;
            Vector3f p3;
        };
        Vector3f tris[3];
    };

    constexpr Surface3D()
        : tris {}
    {}

    constexpr Surface3D(const Vector3f& a, const Vector3f& b, const Vector3f& c)
        : p1(a),
          p2(b),
          p3(c)
    {}
};


// aliases

struct Position3DTag {};
struct Velocity3DTag {};
struct Collision3DTag {};

using Position3D = ecs::BufferedAlias<Vector3f, Position3DTag>;
using Velocity3D = ecs::Alias<Vector3f, Velocity3DTag>;
using CollisionSurface = ecs::Alias<Surface3D, Collision3DTag>;
