//
// Created by Nicholas on 26/04/26.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

struct Entity
{
    static constexpr size_t N_POS = std::numeric_limits<size_t>::max();

    size_t index = N_POS;
    uint32_t version = 0;

    Entity() = default;
    Entity(const size_t idx, const uint32_t ver) : index(idx), version(ver) {}

    explicit operator bool() const { return valid(); }
    bool valid() const { return index != N_POS; }

    // Stable, generation-bearing representation for scripting and serialization.
    uint64_t packed() const noexcept
    { return valid() ? (uint64_t{version} << 32) | static_cast<uint32_t>(index) : UINT64_MAX; }

    static Entity fromPacked(const uint64_t value) noexcept
    { return value == UINT64_MAX ? Entity{} : Entity{static_cast<uint32_t>(value), static_cast<uint32_t>(value >> 32)}; }

    // Equality comparison (required for containers, identity checks)
    bool operator==(const Entity& other) const noexcept
    {
        return index == other.index && version == other.version;
    }

    bool operator!=(const Entity& other) const noexcept { return !(*this == other); }

    // Ordering comparison (required for sorting, map keys)
    bool operator<(const Entity& other) const noexcept
    {
        if (index != other.index) return index < other.index;
        return version < other.version;
    }

    bool operator>(const Entity& other) const noexcept { return other < *this; }
    bool operator<=(const Entity& other) const noexcept { return !(*this > other); }
    bool operator>=(const Entity& other) const noexcept { return !(*this < other); }
};

// Sentinel for invalid/null entity (EnTT-compatible naming)
inline constexpr Entity INVALID_ENTITY{};

struct EntityRecord
{
    uint32_t version = 1;
    bool alive = false;
};
