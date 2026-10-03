#include "../../ecs/ecs.h"
#include "../test_assertions.h"

#include <limits>

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

    DynamicComponentStorage metadata;
    const std::vector<DynamicField> versioned{
        {"hp", DynamicFieldType::Number, 2, 100.0},
        {"active", DynamicFieldType::Boolean, 1, true},
        {"label", DynamicFieldType::String, 1, std::string("new")}};
    test::require(metadata.registerComponent("HealthV2", versioned, 2) &&
        metadata.schemaVersion("HealthV2") == 2 && metadata.schemaFields("HealthV2") == versioned,
        "registered schema and property versions/defaults should be inspectable");
    test::require(metadata.registerComponent("HealthV2", versioned, 2) &&
        !metadata.registerComponent("HealthV2", versioned, 3) &&
        !metadata.registerComponent("HealthV2", {{"hp", DynamicFieldType::Number, 2, 50.0}}, 2),
        "only an identical versioned schema should register twice");
    const Entity defaulted = ecs.createEntity();
    test::require(metadata.set(defaulted, "HealthV2", {}) &&
        metadata.get(defaulted, "HealthV2") == DynamicValues{{"hp", 100.0}, {"active", true},
            {"label", std::string("new")}},
        "omitted fields should receive typed defaults in dense storage");
    test::require(metadata.set(defaulted, "HealthV2", {{"hp", 7.0}}) &&
        metadata.get(defaulted, "HealthV2") == DynamicValues{{"hp", 7.0}, {"active", true},
            {"label", std::string("new")}},
        "replacement writes should use defaults for omitted fields");
    test::require(!metadata.validate("HealthV2", {{"hp", std::numeric_limits<double>::infinity()}}) &&
        !metadata.validate("HealthV2", {{"hp", false}}) &&
        !metadata.validate("HealthV2", {{"missing", 1.0}}) &&
        !metadata.validate("HealthV2", {{"hp", 1.0}, {"hp", 2.0}}),
        "non-finite, mismatched, unknown, and duplicate property values must fail");
    test::require(!metadata.registerComponent("InvalidDefault", {{"hp", DynamicFieldType::Number, 1, false}}) &&
        !metadata.registerComponent("InvalidVersion", {{"hp", DynamicFieldType::Number, 2}}, 1) &&
        !metadata.registerComponent("bad-name", {{"hp", DynamicFieldType::Number}}),
        "invalid metadata and unstable names must be rejected");

    DynamicComponentStorage sparseMembership;
    const std::vector<DynamicField> mixedFields{{"number", DynamicFieldType::Number},
        {"flag", DynamicFieldType::Boolean}, {"text", DynamicFieldType::String}};
    test::require(sparseMembership.registerComponent("Mixed", mixedFields) &&
        sparseMembership.registerComponent("Marker", {{"value", DynamicFieldType::Number}}),
        "schemas for sparse membership tests should register");
    const Entity rowA{12, 3}, rowB{4, 2}, rowC{9, 5};
    test::require(sparseMembership.set(rowA, "Mixed", {{"number", 12.0}, {"flag", true}, {"text", std::string("a")}}) &&
        sparseMembership.set(rowB, "Mixed", {{"number", 4.0}, {"flag", false}, {"text", std::string("b")}}) &&
        sparseMembership.set(rowC, "Mixed", {{"number", 9.0}, {"flag", true}, {"text", std::string("c")}}) &&
        sparseMembership.set(rowB, "Marker", {{"value", 1.0}}) &&
        sparseMembership.set(rowC, "Marker", {{"value", 2.0}}),
        "mixed typed values should insert into sparse membership and aligned columns");
    test::require(sparseMembership.query({"Mixed"}) == std::vector<Entity>{rowB, rowC, rowA} &&
        sparseMembership.query({"Mixed", "Marker"}) == std::vector<Entity>{rowB, rowC},
        "EnTT runtime-view queries should intersect memberships and return stable entity order");
    test::require(sparseMembership.remove(rowB, "Mixed") &&
        sparseMembership.get(rowC, "Mixed") == DynamicValues{{"number", 9.0}, {"flag", true}, {"text", std::string("c")}} &&
        sparseMembership.query({"Mixed", "Marker"}) == std::vector<Entity>{rowC},
        "swap removal should keep all typed columns aligned with the moved membership row");

    const Entity reusedIndex{rowC.index, rowC.version + 1};
    test::require(!sparseMembership.set(reusedIndex, "Mixed",
            {{"number", 90.0}, {"flag", false}, {"text", std::string("stale")}}) &&
        !sparseMembership.get(reusedIndex, "Mixed") && !sparseMembership.remove(reusedIndex, "Mixed") &&
        sparseMembership.get(rowC, "Mixed").has_value(),
        "a different generation must not alias a live sparse index");
    test::require(sparseMembership.remove(rowC, "Mixed") &&
        sparseMembership.set(reusedIndex, "Mixed",
            {{"number", 90.0}, {"flag", false}, {"text", std::string("reused")}}) &&
        sparseMembership.get(reusedIndex, "Mixed") == DynamicValues{{"number", 90.0}, {"flag", false},
            {"text", std::string("reused")}},
        "an index can be reused after the previous generation is erased");

    const Entity maxVersion{20, std::numeric_limits<std::uint32_t>::max()};
    test::require(sparseMembership.set(maxVersion, "Mixed",
            {{"number", 20.0}, {"flag", true}, {"text", std::string("max-generation")}}) &&
        sparseMembership.get(maxVersion, "Mixed").has_value() &&
        sparseMembership.query({"Mixed"}) == std::vector<Entity>{reusedIndex, rowA, maxVersion},
        "the maximum engine generation should survive EnTT's native generation offset");
    test::require(!sparseMembership.set(Entity{}, "Mixed", {}) &&
        !sparseMembership.set(Entity{1, 0}, "Mixed", {}) &&
        !sparseMembership.set(Entity{std::numeric_limits<std::uint32_t>::max(), 1}, "Mixed", {}) &&
        !sparseMembership.set(Entity{size_t{1} << 32, 1}, "Mixed", {}),
        "invalid, reserved, and out-of-range handles must fail before reaching EnTT");

    DynamicComponentStorage typeQuota;
    for (size_t i = 0; i < max_dynamic_component_types; ++i)
        test::require(typeQuota.registerComponent("Type" + std::to_string(i),
            {{"value", DynamicFieldType::Number}}), "component type quota should permit its boundary");
    test::require(!typeQuota.registerComponent("Overflow", {{"value", DynamicFieldType::Number}}),
        "component type quota should reject one additional schema");
    std::vector<DynamicField> tooManyFields;
    for (size_t i = 0; i <= max_dynamic_fields_per_component; ++i)
        tooManyFields.push_back({"field" + std::to_string(i), DynamicFieldType::Number});
    test::require(!metadata.registerComponent("TooWide", tooManyFields),
        "per-component property quota should be enforced");
    DynamicComponentStorage propertyQuota;
    std::vector<DynamicField> sixteenFields(tooManyFields.begin(), tooManyFields.begin() + 16);
    for (size_t i = 0; i < max_dynamic_properties_per_project / sixteenFields.size(); ++i)
        test::require(propertyQuota.registerComponent("Wide" + std::to_string(i), sixteenFields),
            "project property quota should permit its boundary");
    test::require(!propertyQuota.registerComponent("OneMore", {{"value", DynamicFieldType::Number}}),
        "project property quota should reject one additional property");
}
