#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace project::gameplay2d
{
    using Vec2 = std::array<float, 2>;
    template<typename T> using Result = std::expected<T, std::string>;

    struct Body
    {
        Vec2 position{};
        Vec2 size{1, 1};
        Vec2 offset{};
        bool trigger = false;
        std::uint32_t layer = 1;
        std::uint32_t mask = ~std::uint32_t{};
        bool operator==(const Body&) const = default;
    };

    struct MoveResult
    {
        Vec2 position{};
        std::vector<std::string> contacts;
    };

    enum class TriggerPhase { Enter, Stay, Exit };
    struct TriggerEvent
    {
        std::string first;
        std::string second;
        TriggerPhase phase;
        bool operator==(const TriggerEvent&) const = default;
    };

    // Coordinates are centers, size is full extent; touch alone is not overlap.
    // All returned IDs and events are stable lexicographic order, independent of insertion.
    class World
    {
    public:
        static constexpr std::size_t MaximumBodies = 1024;
        Result<void> upsert(std::string id, Body body);
        bool remove(std::string_view id);
        // Room replacement discards all previous pairs without emitting stale exits.
        void clear();
        std::optional<Body> body(std::string_view id) const;
        Result<MoveResult> move(std::string_view id, Vec2 displacement);
        Result<std::vector<std::string>> overlaps(std::string_view id) const;
        Result<std::vector<std::string>> query(Body area, std::string_view ignore = {}) const;
        Result<bool> isSafe(Body candidate, std::string_view ignore = {}) const;
        // Bounded deterministic search: preferred, then eight directions per ring.
        // Returns no value when no sampled location within radius is safe.
        Result<std::optional<Vec2>> findSafe(Body candidate, float radius, float step,
                                           std::string_view ignore = {}) const;
        // Call once after final movements of a simulation tick. Removal emits exits
        // at the next call; repeated call is another tick and yields stays.
        std::vector<TriggerEvent> advanceTriggers();
    private:
        std::map<std::string, Body, std::less<>> bodies_;
        std::set<std::pair<std::string, std::string>> previousTriggers_;
    };

    // Velocity in units/second; stops inside separation and never exceeds speed.
    Result<Vec2> followVelocity(Vec2 from, Vec2 target, float speed, float separation);
    // Deterministic slots on a circle. Content controls whether/when to flock.
    Result<Vec2> flockTarget(Vec2 center, std::size_t index, std::size_t count, float radius);
}
