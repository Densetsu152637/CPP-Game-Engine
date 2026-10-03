#include "project/gameplay2d.h"
#include "test/test_assertions.h"

#include <cmath>
#include <limits>

namespace
{
    using namespace project::gameplay2d;
    void add(World& world, std::string id, Vec2 position, Vec2 size = {1, 1}, bool trigger = false,
             std::uint32_t layer = 1, std::uint32_t mask = ~std::uint32_t{})
    {
        test::require(world.upsert(std::move(id), Body{position, size, {}, trigger, layer, mask}).has_value(), "body insertion");
    }
    void at(Vec2 actual, Vec2 expected, const char* message)
    {
        test::require(std::abs(actual[0]-expected[0]) < 0.0001f && std::abs(actual[1]-expected[1]) < 0.0001f, message);
    }
}

void runGameplay2dTests()
{
    using namespace project::gameplay2d;
    World world;
    add(world, "player", {0,0}); add(world, "wall", {3,0}, {1,100});
    auto moved = world.move("player", {1000,10});
    test::require(moved.has_value(), "swept move succeeds");
    at(moved->position, {2,10}, "high speed slides along wall without tunnelling");
    test::require(moved->contacts == std::vector<std::string>{"wall"}, "stable contacts");
    at(world.move("player", {0,-4})->position, {2,6}, "contact tangent slides");
    at(world.move("player", {-8,0})->position, {-6,6}, "contact can move away");
    test::require(world.overlaps("player")->empty(), "touch is not overlap");
    world.clear();
    add(world, "player", {0,0}); add(world, "right", {3,0}, {1,100}); add(world, "top", {0,3}, {100,1});
    at(world.move("player", {100,100})->position, {2,2}, "simultaneous corner blocks both axes");
    world.clear();
    add(world, "player", {0,0}); add(world, "left", {-3,0}, {1,100});
    at(world.move("player", {-100,5})->position, {-2,5}, "negative sweep and slide");
    world.clear();
    add(world, "player", {0,0}); add(world, "obstacle", {0.25f,0}, {1,1});
    test::require(world.move("player", {0,0}).has_value(), "starting overlap resolves");
    test::require(*world.isSafe(*world.body("player"), "player"), "resolved body safe");
    world.clear(); add(world, "player", {0,0}); add(world, "decimal-wall", {3.1f,0}, {0.3f,100});
    test::require(world.move("player", {999,2}).has_value(), "fractional contact move succeeds");
    test::require(*world.isSafe(*world.body("player"), "player"), "fractional contact remains outside solid");
    const auto before = world.body("player");
    test::require(!world.move("player", {std::numeric_limits<float>::infinity(),0}), "nonfinite movement rejected");
    test::require(world.body("player") == before, "failed movement preserves state");
    world.clear();
    add(world, "player", {0,0}); add(world, "z-trigger", {0,0}, {4,4}, true); add(world, "a-trigger", {0,0}, {4,4}, true);
    test::require(*world.overlaps("player") == std::vector<std::string>({"a-trigger","z-trigger"}), "overlap IDs sorted");
    auto events = world.advanceTriggers();
    test::require(events.size() == 3 && events[0].first == "a-trigger" && events[0].second == "player" && events[0].phase == TriggerPhase::Enter, "pairs enter sorted");
    for (const auto& event : world.advanceTriggers()) test::require(event.phase == TriggerPhase::Stay, "pairs stay");
    test::require(world.remove("a-trigger"), "remove trigger");
    events = world.advanceTriggers();
    test::require(events.size() == 3 && events[0].phase == TriggerPhase::Exit && events[1].phase == TriggerPhase::Exit, "destroy exits once");
    test::require(world.advanceTriggers().size() == 1, "no stale destroyed pairs");
    at(world.move("player", {20,0})->position, {20,0}, "triggers never block");
    test::require(world.advanceTriggers()[0].phase == TriggerPhase::Exit, "departure exit");
    test::require(world.move("player", {-20,0}).has_value(), "return move succeeds");
    test::require(world.advanceTriggers()[0].phase == TriggerPhase::Enter, "reentry enter");
    world.clear(); test::require(world.advanceTriggers().empty(), "room clear forgets pairs");
    add(world, "player", {0,0}, {1,1}, false, 1, 1); add(world, "unmatched", {3,0}, {1,100}, false, 2, 2);
    at(world.move("player", {10,0})->position, {10,0}, "disjoint masks do not collide");
    Body invalid; invalid.mask = 0; test::require(!world.upsert("invalid", invalid), "zero mask rejected");
    invalid = {}; invalid.size[0] = 0; test::require(!world.upsert("invalid", invalid), "zero extent rejected");
    world.clear(); add(world,"player",{0,0}); add(world,"left",{-0.75f,0},{1,10}); add(world,"right",{0.75f,0},{1,10});
    const auto trapped = world.body("player");
    test::require(!world.move("player",{0,0}) && world.body("player") == trapped, "impossible starting overlap fails transactionally");
    world.clear(); add(world,"player",{0,0});
    at(world.move("player",{0,0})->position, {0,0}, "empty zero movement remains still");
    test::require(!world.overlaps("missing") && !world.move("missing",{1,0}), "missing body queries fail");
    world.clear(); add(world, "wall", {0,0}, {2,2});
    Body candidate{{0,0},{1,1}};
    auto safe = world.findSafe(candidate, 5, 1);
    test::require(safe && safe->has_value(), "bounded safe placement found");
    candidate.position = **safe; test::require(*world.isSafe(candidate), "safe placement avoids solids");
    test::require(world.findSafe(Body{{0,0},{1,1}}, 0, 1)->has_value() == false, "blocked search returns none");
    test::require(!world.findSafe(candidate, 100, 1), "search capacity bounded");
    at(*followVelocity({0,0},{3,4},10,1), {6,8}, "following capped speed and direction");
    at(*followVelocity({0,0},{0.5f,0},10,1), {0,0}, "follow separation stop");
    at(*flockTarget({0,0},0,4,2), {2,0}, "flock first slot");
    at(*flockTarget({0,0},1,4,2), {0,2}, "flock separate slot");
    test::require(!flockTarget({0,0},0,0,2), "empty flock rejected");
    // Identical input sequences are independent of insertion order.
    World a, b; add(a,"p",{0,0}); add(a,"w",{3,0},{1,100}); add(b,"w",{3,0},{1,100}); add(b,"p",{0,0});
    for (auto delta : {Vec2{6,2}, Vec2{-2,1}, Vec2{5,-4}})
        test::require(a.move("p",delta)->position == b.move("p",delta)->position, "deterministic insertion independent sequence");
}
