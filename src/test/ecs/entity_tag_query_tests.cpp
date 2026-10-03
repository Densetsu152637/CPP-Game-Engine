#include "test/ecs/ecs_test_fixtures.h"

using namespace ecs_test;

void test_ecs_tags_track_entities_and_cleanup()
{
    ECS ecs;
    const Entity first = ecs.createEntity();
    const Entity second = ecs.createEntity();

    require(ecs.addTag<RenderableTag>(first), "adding a tag returned false");
    require(ecs.hasTag<RenderableTag>(first), "added tag was not visible");
    require(ecs.hasTag<ecs::Tag<RenderableTag>>(first), "tag wrapper lookup did not unwrap tag name");
    require(!ecs.hasTag<RenderableTag>(second), "untagged entity reported tag");
    require(!ecs.addTag<RenderableTag>(first), "adding an existing tag should return false");

    const ecs::BackendSet* pool = ecs.tagPoolIfExists<RenderableTag>();
    require(nullptr != pool, "tag pool was not created");
    require(pool->size() == 1, "tag pool had wrong size after add");

    require(ecs.removeTag<RenderableTag>(first), "removing existing tag returned false");
    require(!ecs.hasTag<RenderableTag>(first), "removed tag was still visible");
    require(!ecs.removeTag<RenderableTag>(first), "removing missing tag should return false");

    ecs.addTag<SelectedTag>(second);
    ecs.destroyEntity(second);
    require(!ecs.hasTag<SelectedTag>(second), "destroyed entity retained tag");
    require(ecs.tagPoolIfExists<SelectedTag>()->empty(), "tag pool retained destroyed entity");
}

void test_tag_pool_keeps_dense_entities()
{
    ECS ecs;
    const Entity first = ecs.createEntity();
    const Entity second = ecs.createEntity();
    const Entity third = ecs.createEntity();

    ecs.addTag<RenderableTag>(first);
    ecs.addTag<RenderableTag>(second);
    ecs.addTag<RenderableTag>(third);
    ecs.removeTag<RenderableTag>(second);

    const ecs::BackendSet* pool = ecs.tagPoolIfExists<RenderableTag>();
    require(nullptr != pool, "dense tag pool was not created");
    require(pool->size() == 2, "dense tag pool had wrong size after removal");
    require(pool->contains(first.index), "dense tag pool lost first tagged entity");
    require(!pool->contains(second.index), "dense tag pool kept removed entity");
    require(pool->contains(third.index), "dense tag pool lost swapped tagged entity");

    size_t rangeCount = 0;
    bool foundFirst = false;
    bool foundThird = false;
    for (const auto entityIndex : *pool)
    {
        ++rangeCount;
        foundFirst = foundFirst || static_cast<size_t>(entityIndex) == first.index;
        foundThird = foundThird || static_cast<size_t>(entityIndex) == third.index;
    }
    require(rangeCount == 2, "native tag range had wrong size");
    require(foundFirst && foundThird, "native tag range missed a surviving entity");
}

void test_entities_can_be_created_with_tags()
{
    ECS ecs;

    const Entity immediate = ecs.createEntityWithTags<RenderableTag, SelectedTag>();
    require(ecs.hasEntity(immediate), "tagged entity was not created immediately");
    require(ecs.hasTag<RenderableTag>(immediate), "createEntityWithTags missed first tag");
    require(ecs.hasTag<SelectedTag>(immediate), "createEntityWithTags missed second tag");

    const Entity overload = ecs.createEntity(ecs::Tag<HiddenTag>{});
    require(ecs.hasTag<HiddenTag>(overload), "createEntity(tag...) overload missed tag");

    ecs.beginStructuralDeferral();
    const Entity deferred = ecs.createEntityWithTags<HiddenTag>();
    require(!ecs.hasEntity(deferred), "deferred tagged entity became visible before flush");
    require(!ecs.hasTag<HiddenTag>(deferred), "deferred tag became visible before flush");
    ecs.endStructuralDeferral();
    ecs.flushDeferredStructuralChanges();

    require(ecs.hasEntity(deferred), "deferred tagged entity did not flush");
    require(ecs.hasTag<HiddenTag>(deferred), "deferred tagged entity missed tag after flush");
}

void test_deferred_tag_changes_flush_after_wall()
{
    ECS ecs;
    const Entity entity = ecs.createEntity();

    ecs.beginStructuralDeferral();
    require(ecs.addTag<RenderableTag>(entity), "deferred add tag rejected known entity");
    require(!ecs.hasTag<RenderableTag>(entity), "deferred add tag became visible before flush");
    ecs.endStructuralDeferral();
    ecs.flushDeferredStructuralChanges();
    require(ecs.hasTag<RenderableTag>(entity), "deferred add tag did not flush");

    ecs.beginStructuralDeferral();
    require(ecs.removeTag<RenderableTag>(entity), "deferred remove tag rejected known entity");
    require(ecs.hasTag<RenderableTag>(entity), "deferred remove tag became visible before flush");
    ecs.endStructuralDeferral();
    ecs.flushDeferredStructuralChanges();
    require(!ecs.hasTag<RenderableTag>(entity), "deferred remove tag did not flush");
}

void test_filtered_query_uses_tags_and_excludes()
{
    ECS ecs;
    const Entity first = ecs.createEntity();
    const Entity second = ecs.createEntity();
    const Entity third = ecs.createEntity();
    const Entity fourth = ecs.createEntity();

    ecs.emplaceComponent<Velocity>(first, 1);
    ecs.emplaceComponent<Velocity>(second, 2);
    ecs.emplaceComponent<Velocity>(third, 3);
    ecs.emplaceComponent<Velocity>(fourth, 4);
    ecs.emplaceComponent<Health>(third, 99);
    ecs.addTag<RenderableTag>(first);
    ecs.addTag<RenderableTag>(second);
    ecs.addTag<RenderableTag>(third);
    ecs.addTag<SelectedTag>(second);

    ArrayList<Entity> componentFiltered = ecs.matchingEntities<
        Velocity,
        ecs::Tag<RenderableTag>,
        ecs::Exclude<ecs::Tag<SelectedTag>>,
        ecs::Exclude<Health>
    >();
    require(componentFiltered.length() == 1, "matchingEntities did not combine tag and exclude filters");
    require(
        componentFiltered[0].index == first.index && componentFiltered[0].version == first.version,
        "matchingEntities returned the wrong filtered entity"
    );

    struct MissingQueryComponent {};
    require(ecs.matchingEntities<MissingQueryComponent>().empty(), "missing included source should produce no matches");
    require(ecs.matchingEntities<ecs::Exclude<MissingQueryComponent>>().length() == 4,
        "missing excluded source should be ignored");
    require(ecs.matchingEntities<Velocity, Velocity>().length() == 4,
        "repeated include source changed the result");
    require(ecs.matchingEntities<Velocity, ecs::Exclude<Velocity>>().empty(),
        "contradictory include/exclude sources should produce no matches");
    require(ecs.matchingEntities<ecs::Exclude<Health>>().length() == 3,
        "exclude-only query did not enumerate live entities through identity");
    require(ecs.matchingEntities<Entity>().length() == 4,
        "Entity declaration should remain a non-filtering argument");
    require(ecs.matchingEntities<ecs::ViewOf<Health>>().length() == 4,
        "ViewOf declaration should remain a non-filtering argument");

    auto query = ecs.query<Velocity, ecs::Tag<RenderableTag>, ecs::Exclude<ecs::Tag<SelectedTag>>>();
    require(query.size() == 2, "filtered query had wrong initial size");

    int sum = 0;
    query.each([&](const Entity& entity, Velocity& velocity)
    {
        require(entity.index != second.index, "filtered query included excluded tag");
        sum += static_cast<int>(velocity);
    });
    require(sum == 4, "filtered query iterated wrong entities");

    ecs.addTag<SelectedTag>(third);
    require(query.size() == 1, "filtered query cache did not invalidate after tag change");

    struct ExtraMembership {};
    ECS mixed;
    const Entity grouped = mixed.createEntity();
    const Entity partial = mixed.createEntity();
    mixed.emplaceComponent<Velocity>(grouped, 10);
    mixed.emplaceComponent<Health>(grouped, 20);
    mixed.emplaceComponent<Velocity>(partial, 30);
    mixed.emplaceComponent<ExtraMembership>(partial);
    mixed.registerArchetype<Velocity, Health>();
    const auto mixedMatches = mixed.matchingEntities<Velocity, ExtraMembership>();
    require(mixedMatches.length() == 1 && mixedMatches[0] == partial,
        "native query did not intersect partial archetype and standalone membership");

    for (const bool grouped : { false, true })
    {
        Threadpool renderPool(1, std::string("query-publication-test"));
        ECSProcessor processor(renderPool);
        ECS& rendered = processor.ecs();
        if (grouped)
            processor.registerRenderArchetype<Health, Velocity>();
        processor.queue_into_rendering<Health>("query-publication", [](const Health&) {});

        const Entity renderEntity = rendered.createEntity();
        rendered.emplaceComponent<Velocity>(renderEntity, 1);
        processor.simulate();
        processor.render();
        require(rendered.render_query<Health>().empty(), "unexpected initial render query membership");

        rendered.emplaceComponent<Health>(renderEntity, 5);
        processor.simulate();
        require(rendered.render_query<Health>().empty(), "render query exposed component before publication");
        processor.render();
        require(rendered.render_query<Health>().size() == 1, "render query missed published component membership");

        rendered.removeComponent<Health>(renderEntity);
        processor.simulate();
        require(rendered.render_query<Health>().size() == 1, "render query lost membership before removal publication");
        processor.render();
        require(rendered.render_query<Health>().empty(), "render query retained removed published membership");
    }
}

void test_entt_entity_generations_survive_reuse_and_clear()
{
    ECS ecs;
    const Entity original = ecs.createEntity();
    require(Entity::fromPacked(original.packed()) == original, "entity handle did not round-trip");
    require(!Entity::fromPacked(INVALID_ENTITY.packed()), "null handle did not round-trip");
    ecs.emplaceComponent<Velocity>(original, 7);
    ecs.addTag<RenderableTag>(original);
    ecs.destroyEntity(original);
    const Entity reused = ecs.createEntity();
    require(reused.index == original.index, "EnTT did not recycle the destroyed entity index");
    require(reused.version != original.version, "recycled entity retained its old generation");
    require(!ecs.hasEntity(original), "destroyed handle became valid after index reuse");
    require(!ecs.try_get<Velocity>(original), "destroyed handle exposed a component");
    require(!ecs.hasComponent<Velocity>(reused), "new entity inherited a destroyed component");
    require(!ecs.hasTag<RenderableTag>(reused), "new entity inherited a destroyed tag");
    ECS sparseEntities;
    const Entity firstLive = sparseEntities.createEntity();
    const Entity hole = sparseEntities.createEntity();
    const Entity lastLive = sparseEntities.createEntity();
    sparseEntities.destroyEntity(hole);
    ArrayList<Entity> liveEntities = sparseEntities.view<>().allEntities();
    require(liveEntities.length() == 2 && liveEntities.contains(firstLive) && liveEntities.contains(lastLive),
        "entity-only view did not enumerate live native entities across a hole");
    require(!liveEntities.contains(hole), "entity-only view included a destroyed entity");
    ecs.emplaceComponent<Velocity>(reused, 11);
    ecs.destroyEntity(original);
    require(ecs.hasEntity(reused), "stale destroy removed the recycled entity");
    ecs.clear();
    const Entity afterClear = ecs.createEntity();
    require(!ecs.hasEntity(reused), "clear allowed a stale entity handle to revive");
    require(!ecs.hasEntity(original), "clear reset the original entity generation");
    require(ecs.hasEntity(afterClear), "entity created after clear was invalid");
    auto entityView = ecs.view<>();
    require(entityView.size() == 1, "entity registry count was wrong after clear/reuse");
    liveEntities = entityView.allEntities();
    require(liveEntities.length() == 1 && liveEntities[0] == afterClear,
        "entity-only view retained entities from before clear");

    ecs.beginStructuralDeferral();
    const Entity canceledReservation = ecs.createEntity();
    ecs.endStructuralDeferral();
    ecs.discardDeferredStructuralChanges();
    require(!ecs.knowsEntityHandle(canceledReservation), "discarded reservation remained a known handle");
    const Entity immediateAfterCancellation = ecs.createEntity();
    require(immediateAfterCancellation != canceledReservation,
        "immediate entity creation revived a canceled reservation");
    ecs.beginStructuralDeferral();
    const Entity afterCanceledReservation = ecs.createEntity();
    ecs.endStructuralDeferral();
    ecs.flushDeferredStructuralChanges();
    require(afterCanceledReservation.index > canceledReservation.index,
        "discarded reservation did not advance the deferred high-water index");
    require(ecs.hasEntity(afterCanceledReservation), "entity after a canceled reservation did not activate");
    require(!ecs.hasEntity(canceledReservation), "canceled reservation revived after a later activation");

    for (const bool grouped : {false, true})
    {
        Threadpool pool(1, std::string("entt-reuse-test"));
        ECSProcessor processor(pool);
        ECS& rendered = processor.ecs();
        if (grouped) processor.registerRenderArchetype<Velocity, Health>();
        processor.queue_into_rendering<Velocity>("retain-snapshot", [](const Velocity&) {});
        const Entity published = rendered.createEntity();
        rendered.emplaceComponent<Velocity>(published, 21);
        processor.simulate();
        processor.render();
        require(rendered.render_view<Velocity>().size() == 1, "render snapshot was not published");
        rendered.destroyEntity(published);
        const Entity replacement = rendered.createEntity();
        require(replacement.index == published.index, "render test did not exercise index reuse");
        require(rendered.render_view<Velocity>().empty(), "recycled entity inherited an old render snapshot");
    }
}

void test_entt_storage_growth_and_view_membership()
{
    ECS ecs;
    const Entity first = ecs.createEntity();
    auto* stable = &ecs.emplaceComponent<Velocity>(first, 1);
    ArrayList<Entity> entities;
    entities.append(first);
    for (int i = 1; i < 2300; ++i)
    {
        const Entity entity = ecs.createEntity();
        entities.append(entity);
        ecs.emplaceComponent<Velocity>(entity, i + 1);
        if (i % 2 == 0) ecs.emplaceComponent<Health>(entity, i);
    }
    require(ecs.try_get<Velocity>(first) == stable, "EnTT page growth invalidated a component address");
    auto view = ecs.view<Velocity, Health>();
    require(view.size() == 1149, "EnTT runtime view intersection had the wrong size");
    ecs.removeComponent<Health>(entities[2]);
    ecs.destroyEntity(entities[4]);
    require(view.size() == 1147, "EnTT runtime view did not refresh removed memberships");
    size_t count = 0;
    view.each([&](const Entity& entity, const Velocity&, const Health&)
    {
        require(ecs.hasEntity(entity), "EnTT view emitted a destroyed entity");
        require(entity != entities[2] && entity != entities[4], "EnTT view emitted a removed membership");
        ++count;
    });
    require(count == 1147, "EnTT view iteration disagreed with its size");
    auto dense = ecs.denseComponentsMut<Velocity>();
    dense[0] = 37;
    require(static_cast<int>(*ecs.try_get<Velocity>(first)) == 37, "paged dense range copied component values");
    ecs.registerArchetype<Velocity, Health>();
    require(view.size() == 1147, "migration to grouped EnTT rows changed the view intersection");

    struct SharedKey {};
    struct SharedPayload
    {
        int id;
        bool operator==(const SharedPayload&) const = default;
    };
    using SharedValue = ecs::SharedAlias<SharedPayload, SharedKey>;
    ECS shared;
    const Entity a = shared.createEntity();
    const Entity b = shared.createEntity();
    const Entity c = shared.createEntity();
    for (const Entity entity : {a, b, c})
    {
        shared.emplaceComponent<Health>(entity, static_cast<int>(entity.index));
        shared.emplaceComponent<SharedValue>(entity, SharedPayload{static_cast<int>(entity.index) + 10});
    }
    auto cached = shared.view<Health, SharedValue>();
    require(cached.size() == 3, "shared view was not cached");
    shared.setComponent<SharedValue>(a, SharedValue(SharedPayload{99}));
    cached.each([&](const Entity& entity, const Health& health, const SharedValue& value)
    {
        require(static_cast<int>(health) == static_cast<int>(entity.index), "cached health became mispaired");
        require(value.id == (entity == a ? 99 : static_cast<int>(entity.index) + 10),
            "shared reassignment changed the entity associated with a cached dense index");
    });

}
