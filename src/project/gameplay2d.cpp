#include "gameplay2d.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace project::gameplay2d
{
    namespace
    {
        constexpr float CoordinateLimit = 1000000;
        bool valid(Vec2 v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::abs(v[0]) <= CoordinateLimit && std::abs(v[1]) <= CoordinateLimit; }
        bool valid(const Body& b) { return valid(b.position) && valid(b.size) && valid(b.offset) && b.size[0] > 0 && b.size[1] > 0 && b.layer != 0 && b.mask != 0; }
        bool matches(const Body& a, const Body& b) { return (a.layer & b.mask) != 0 && (b.layer & a.mask) != 0; }
        struct Box { double min[2]; double max[2]; };
        Box box(const Body& b)
        {
            Box result{};
            for (int axis = 0; axis < 2; ++axis)
            {
                const double center = double(b.position[axis]) + b.offset[axis];
                result.min[axis] = center - double(b.size[axis]) / 2;
                result.max[axis] = center + double(b.size[axis]) / 2;
            }
            return result;
        }
        bool intersects(const Body& a, const Body& b)
        {
            const auto x = box(a), y = box(b);
            return x.min[0] < y.max[0] && x.max[0] > y.min[0] && x.min[1] < y.max[1] && x.max[1] > y.min[1];
        }
        struct Hit { double time = 2; bool x = false; bool y = false; };
        Hit sweep(const Body& moving, const Body& obstacle, Vec2 delta)
        {
            const auto a = box(moving), b = box(obstacle);
            double enter[2]{}, leave[2]{};
            for (int axis = 0; axis < 2; ++axis)
            {
                if (delta[axis] == 0)
                {
                    // Touching on the stationary axis permits tangential sliding.
                    if (a.max[axis] <= b.min[axis] || a.min[axis] >= b.max[axis]) return {};
                    enter[axis] = -std::numeric_limits<double>::infinity();
                    leave[axis] = std::numeric_limits<double>::infinity();
                }
                else
                {
                    const double t1 = (b.min[axis] - a.max[axis]) / delta[axis];
                    const double t2 = (b.max[axis] - a.min[axis]) / delta[axis];
                    enter[axis] = std::min(t1, t2);
                    leave[axis] = std::max(t1, t2);
                }
            }
            const double time = std::max(enter[0], enter[1]);
            // Blocking requires an interval of positive-area overlap. Entry equal
            // to exit is a corner graze, including entry/exit at the initial point.
            if (time < 0 || time > 1 || time >= std::min(leave[0], leave[1])) return {};
            return {time, enter[0] >= enter[1], enter[1] >= enter[0]};
        }
    }

    Result<void> World::upsert(std::string id, Body value)
    {
        if (id.empty() || id.size() > 256 || !valid(value)) return std::unexpected("invalid body: require ID, finite bounded coordinates, positive size and nonzero layer/mask");
        if (!bodies_.contains(id) && bodies_.size() >= MaximumBodies) return std::unexpected("body capacity exceeded");
        bodies_.insert_or_assign(std::move(id), value);
        return {};
    }
    bool World::remove(std::string_view id)
    {
        const auto found = bodies_.find(id);
        if (found == bodies_.end()) return false;
        bodies_.erase(found);
        return true;
    }
    void World::clear() { bodies_.clear(); previousTriggers_.clear(); }
    std::optional<Body> World::body(std::string_view id) const
    {
        const auto found = bodies_.find(id);
        return found == bodies_.end() ? std::nullopt : std::optional(found->second);
    }
    Result<std::vector<std::string>> World::query(Body area, std::string_view ignore) const
    {
        if (!valid(area)) return std::unexpected("invalid query body");
        std::vector<std::string> ids;
        for (const auto& [id, other] : bodies_)
            if (id != ignore && matches(area, other) && intersects(area, other)) ids.push_back(id);
        return ids;
    }
    Result<std::vector<std::string>> World::overlaps(std::string_view id) const
    {
        const auto value = body(id);
        if (!value) return std::unexpected("unknown body");
        return query(*value, id);
    }
    Result<bool> World::isSafe(Body candidate, std::string_view ignore) const
    {
        if (!valid(candidate)) return std::unexpected("invalid candidate body");
        for (const auto& [id, other] : bodies_)
            if (id != ignore && !other.trigger && matches(candidate, other) && intersects(candidate, other)) return false;
        return true;
    }
    Result<std::optional<Vec2>> World::findSafe(Body candidate, float radius, float step, std::string_view ignore) const
    {
        if (!valid(candidate) || !std::isfinite(radius) || !std::isfinite(step) || radius < 0 || radius > CoordinateLimit || step <= 0 || radius / step > 64)
            return std::unexpected("invalid safe search: require nonnegative radius, positive step and at most 64 rings");
        const Vec2 origin = candidate.position;
        if (*isSafe(candidate, ignore)) return std::optional(origin);
        constexpr double diagonal = 0.7071067811865475244;
        constexpr double directions[8][2] = {{-1,0},{0,-1},{0,1},{1,0},{-diagonal,-diagonal},{-diagonal,diagonal},{diagonal,-diagonal},{diagonal,diagonal}};
        const int rings = int(std::ceil(double(radius) / step));
        for (int ring = 1; ring <= rings; ++ring)
        {
            const double distance = std::min(double(radius), double(ring) * step);
            for (const auto& direction : directions)
            {
                candidate.position = {float(origin[0] + distance * direction[0]), float(origin[1] + distance * direction[1])};
                if (valid(candidate) && *isSafe(candidate, ignore)) return std::optional(candidate.position);
            }
        }
        return std::optional<Vec2>{};
    }
    Result<MoveResult> World::move(std::string_view id, Vec2 delta)
    {
        auto found = bodies_.find(id);
        if (found == bodies_.end()) return std::unexpected("unknown body");
        if (!valid(delta)) return std::unexpected("invalid displacement");
        Body candidate = found->second;
        std::set<std::string> contacts;
        if (!candidate.trigger)
        {
            // Resolve starting penetration deterministically before sweeping. Impossible
            // or deeply interlocked starts fail transactionally instead of tunnelling.
            bool resolved = false;
            for (unsigned attempt = 0; attempt < 32; ++attempt)
            {
                double smallest = std::numeric_limits<double>::infinity();
                Vec2 correction{};
                std::string obstacleId;
                for (const auto& [otherId, other] : bodies_)
                {
                    if (otherId == id || other.trigger || !matches(candidate, other) || !intersects(candidate, other)) continue;
                    const auto a = box(candidate), b = box(other);
                    const double shifts[4] = {b.min[0]-a.max[0], b.max[0]-a.min[0], b.min[1]-a.max[1], b.max[1]-a.min[1]};
                    for (int index = 0; index < 4; ++index)
                        if (std::abs(shifts[index]) < smallest)
                        {
                            smallest = std::abs(shifts[index]);
                            correction = {};
                            correction[index / 2] = float(shifts[index]);
                            obstacleId = otherId;
                        }
                }
                if (obstacleId.empty()) { resolved = true; break; }
                candidate.position[0] += correction[0]; candidate.position[1] += correction[1];
                contacts.insert(obstacleId);
                if (intersects(candidate, bodies_.at(obstacleId)))
                    for (int axis = 0; axis < 2; ++axis)
                        if (correction[axis] != 0)
                            candidate.position[axis] = std::nextafter(candidate.position[axis], correction[axis] < 0 ? -std::numeric_limits<float>::infinity() : std::numeric_limits<float>::infinity());
            }
            if (!resolved) return std::unexpected("initial overlap could not be resolved");
            // At most two independent blocked axes; an extra pass handles zero-time contacts.
            for (int pass = 0; pass < 3 && (delta[0] != 0 || delta[1] != 0); ++pass)
            {
                Hit earliest;
                std::vector<std::string> hitIds;
                for (const auto& [otherId, other] : bodies_)
                {
                    if (otherId == id || other.trigger || !matches(candidate, other)) continue;
                    const auto hit = sweep(candidate, other, delta);
                    if (hit.time < earliest.time)
                    {
                        earliest = hit;
                        hitIds = {otherId};
                    }
                    else if (hit.time <= 1 && hit.time == earliest.time)
                    {
                        earliest.x |= hit.x; earliest.y |= hit.y; hitIds.push_back(otherId);
                    }
                }
                const double fraction = std::min(1.0, earliest.time);
                candidate.position[0] = float(double(candidate.position[0]) + delta[0] * fraction);
                candidate.position[1] = float(double(candidate.position[1]) + delta[1] * fraction);
                if (earliest.time > 1) break;
                contacts.insert(hitIds.begin(), hitIds.end());
                // Float storage can round a mathematically exact contact inward.
                // Move one representable value outward only when that happens.
                for (const auto& hitId : hitIds)
                    if (intersects(candidate, bodies_.at(hitId)))
                    {
                        if (earliest.x && delta[0] != 0)
                            candidate.position[0] = std::nextafter(candidate.position[0], delta[0] > 0 ? -std::numeric_limits<float>::infinity() : std::numeric_limits<float>::infinity());
                        if (earliest.y && delta[1] != 0)
                            candidate.position[1] = std::nextafter(candidate.position[1], delta[1] > 0 ? -std::numeric_limits<float>::infinity() : std::numeric_limits<float>::infinity());
                    }
                delta[0] = earliest.x ? 0 : float(delta[0] * (1 - fraction));
                delta[1] = earliest.y ? 0 : float(delta[1] * (1 - fraction));
            }
        }
        else
        {
            candidate.position[0] += delta[0]; candidate.position[1] += delta[1];
        }
        if (!valid(candidate)) return std::unexpected("movement exceeds coordinate bounds");
        if (!candidate.trigger && !*isSafe(candidate, id)) return std::unexpected("movement cannot represent a safe contact");
        found->second = candidate;
        return MoveResult{candidate.position, {contacts.begin(), contacts.end()}};
    }
    std::vector<TriggerEvent> World::advanceTriggers()
    {
        std::set<std::pair<std::string, std::string>> current;
        for (auto a = bodies_.begin(); a != bodies_.end(); ++a)
            for (auto b = std::next(a); b != bodies_.end(); ++b)
                if ((a->second.trigger || b->second.trigger) && matches(a->second, b->second) && intersects(a->second, b->second)) current.emplace(a->first, b->first);
        auto all = current;
        all.insert(previousTriggers_.begin(), previousTriggers_.end());
        std::vector<TriggerEvent> events;
        for (const auto& pair : all)
            events.push_back({pair.first, pair.second, current.contains(pair) ? (previousTriggers_.contains(pair) ? TriggerPhase::Stay : TriggerPhase::Enter) : TriggerPhase::Exit});
        previousTriggers_ = std::move(current);
        return events;
    }
    Result<Vec2> followVelocity(Vec2 from, Vec2 target, float speed, float separation)
    {
        if (!valid(from) || !valid(target) || !std::isfinite(speed) || speed < 0 || speed > CoordinateLimit || !std::isfinite(separation) || separation < 0 || separation > CoordinateLimit)
            return std::unexpected("invalid follow parameters");
        const double x = double(target[0]) - from[0], y = double(target[1]) - from[1];
        const double distance = std::hypot(x, y);
        if (distance <= separation || distance == 0) return Vec2{};
        return Vec2{float(x / distance * speed), float(y / distance * speed)};
    }
    Result<Vec2> flockTarget(Vec2 center, std::size_t index, std::size_t count, float radius)
    {
        if (!valid(center) || count == 0 || count > World::MaximumBodies || index >= count || !std::isfinite(radius) || radius < 0 || radius > CoordinateLimit)
            return std::unexpected("invalid flock parameters");
        const double angle = 2 * std::numbers::pi * double(index) / double(count);
        const Vec2 result{float(center[0] + radius * std::cos(angle)), float(center[1] + radius * std::sin(angle))};
        if (!valid(result)) return std::unexpected("flock target exceeds coordinate bounds");
        return result;
    }
}
