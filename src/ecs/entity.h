//
// Created by Nicholas on 26/04/26.
//

#pragma once
#include "structs/arraylist.h"

struct Entity
{
    ArrayList<std::type_index> flags{4};
    bool init = false;

    Entity() = default;
    Entity(const bool flag) : init(flag) {};
    ~Entity()
    { init = false; };

    template <typename T>
    bool has() const { return flags.contains(typeid(T)); };
    bool valid() const { return init; };
};

