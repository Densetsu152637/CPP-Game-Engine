#include "../../ecs/ecs.h"
#include "../test_assertions.h"

void test_dynamic_component_storage_and_deferred_query_coherence()
{
    ECS ecs;
    using namespace ecs;
    const std::vector<DynamicField> schema{{"hp", DynamicFieldType::Number}, {"active", DynamicFieldType::Boolean}};
    test::require(ecs.registerDynamicComponent("Health", schema), "runtime component schema should register");
    test::require(ecs.registerDynamicComponent("Health", schema), "matching schema registration should be idempotent");
    test::require(!ecs.registerDynamicComponent("Health", {{"hp", DynamicFieldType::String}}),
        "a conflicting schema must be rejected");

    const Entity first = ecs.createEntity();
    const Entity second = ecs.createEntity();
    test::require(ecs.setDynamicComponent(first, "Health", {{"hp", 10.0}, {"active", true}}),
        "a complete dynamic component should insert");
    test::require(ecs.setDynamicComponent(second, "Health", {{"hp", 20.0}, {"active", false}}),
        "another dense dynamic row should insert");
    test::require(!ecs.setDynamicComponent(first, "Health", {{"hp", 2.0}}),
        "incomplete component writes must be rejected atomically");
    auto matches = ecs.queryDynamicComponents({"Health"});
    test::require(matches == std::vector<Entity>{first, second}, "dynamic query should return stable packed entity order");

    const size_t generation = ecs.component_query_generation();
    ecs.beginStructuralDeferral();
    const Entity deferred = ecs.createEntity();
    test::require(ecs.knowsEntityHandle(deferred) && !ecs.hasEntity(deferred),
        "reserved handles should be recognized for same-tick component commands");
    test::require(!ecs.knowsEntityHandle(Entity{deferred.index + 1, deferred.version}),
        "a handle that was never reserved must not be accepted");
    test::require(ecs.setDynamicComponent(deferred, "Health", {{"hp", 30.0}, {"active", true}}),
        "a reserved entity should accept deferred dynamic component insertion");
    test::require(ecs.removeDynamicComponent(first, "Health"), "deferred removal should be accepted");
    test::require(ecs.queryDynamicComponents({"Health"}) == matches,
        "queries during a structural batch should retain the pre-batch snapshot");
    test::require(ecs.component_query_generation() == generation,
        "component query generation should remain stable before batch commit");
    ecs.endStructuralDeferral();
    ecs.flushDeferredStructuralChanges();
    matches = ecs.queryDynamicComponents({"Health"});
    test::require(matches == std::vector<Entity>{second, deferred},
        "the next tick query should see deferred add and remove operations");
    test::require(ecs.component_query_generation() == generation + 1,
        "one structural batch should invalidate query caches exactly once");

    ecs.beginStructuralDeferral();
    test::require(ecs.removeDynamicComponent(second, "Health"), "second deferred removal should enqueue");
    ecs.endStructuralDeferral();
    ecs.discardDeferredStructuralChanges();
    test::require(ecs.queryDynamicComponents({"Health"}) == matches,
        "discarding a failed system batch should leave storage unchanged");

    ecs.beginStructuralDeferral();
    const Entity abandoned = ecs.createEntity();
    test::require(ecs.setDynamicComponent(abandoned, "Health", {{"hp", 40.0}, {"active", true}}),
        "a newly reserved entity should accept a queued value");
    ecs.endStructuralDeferral();
    ecs.discardDeferredStructuralChanges();
    test::require(!ecs.knowsEntityHandle(abandoned), "discarded reservation must become stale");
    test::require(!ecs.setDynamicComponent(abandoned, "Health", {{"hp", 50.0}, {"active", true}}),
        "a stale reservation must reject later component writes");

    ecs.beginStructuralDeferral();
    test::require(ecs.setDynamicComponent(second, "Health", {{"hp", 99.0}, {"active", true}}),
        "valid queued write should be accepted before preflight");
    ecs.endStructuralDeferral();
    ecs.destroyEntity(second);
    bool rejected = false;
    try { ecs.flushDeferredStructuralChanges(); }
    catch (const std::runtime_error&) { rejected = true; }
    test::require(rejected, "preflight should reject a handle destroyed before commit");
    test::require(ecs.getDynamicComponent(deferred, "Health").has_value(),
        "a rejected batch must leave unrelated component rows untouched");

    ecs.pruneEmptyDynamicComponentSchemas();
    test::require(ecs.hasDynamicComponentSchema("Health"),
        "schema pruning must preserve schemas with live component rows");
    test::require(ecs.removeDynamicComponent(deferred, "Health"), "last live row should be removable");
    ecs.pruneEmptyDynamicComponentSchemas();
    test::require(!ecs.hasDynamicComponentSchema("Health"),
        "unused schemas should be released after their final row is removed");
}
