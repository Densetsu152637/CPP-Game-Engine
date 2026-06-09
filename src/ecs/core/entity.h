//
// Created by Nicholas on 26/04/26.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

struct Entity
{
    static constexpr size_t N_POS = std::numeric_limits<size_t>::max();

    size_t index = N_POS;
    uint32_t version = 0;

    Entity() = default;
    Entity(const size_t idx, const uint32_t ver) : index(idx), version(ver) {}

    explicit operator bool() const { return valid(); }
    bool valid() const { return index != N_POS; }
};

struct EntityRecord
{
    uint32_t version = 1;
    bool alive = false;
};
