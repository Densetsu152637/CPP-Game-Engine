//
// Created by Nicholas on 26/04/26.
//

#pragma once

#include <utility>

#include "ecs.h"

struct Entity
{
    ArrayList<std::type_index> flags;
    bool init = false;

    Entity() = default;
    Entity(const bool flag) : init(flag) {};
    ~Entity()
    { init = false; };

    template <typename T>
    bool has() const { return flags.contains(typeid(T)); };
    bool valid() const { return init; };
};

