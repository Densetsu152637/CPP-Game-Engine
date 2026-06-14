//
// Created by Nicholas on 26/04/26.
//

#include "ecs.h"

Entity ECS::make_handle(const EntityRecord& record, const size_t& index)
{ return EntityRegistry::makeHandle(record, index); }

Entity ECS::make_handle(const size_t& index) const
{ return m_entities.makeHandle(index); }

bool ECS::is_alive_index(const size_t index) const
{ return m_entities.isAliveIndex(index); }

bool ECS::is_known_handle(const Entity& entity) const
{
    if (m_entities.isKnownHandle(entity))
        return true;

    if (!structural_changes_deferred())
        return false;

    std::lock_guard lock(m_structuralMutex);
    return entity.valid() &&
        EntityRecord {}.version == entity.version &&
        entity.index >= m_entities.nextIndex() &&
        entity.index < m_nextDeferredEntityIndex;
}

bool ECS::is_valid_handle(const Entity& entity) const
{ return m_entities.isValidHandle(entity); }

size_t ECS::alive_entity_count() const
{ return m_entities.aliveCount(); }

const ArrayList<EntityRecord>& ECS::entity_records() const
{ return m_entities.records(); }

Entity ECS::createEntity()
{
    if (!structural_changes_deferred())
        return createEntityImmediate();

    const Entity entity = reserveEntityForDeferredCreate();
    m_deferredStructuralCommands.enqueue([this, entity]()
    {
        activateDeferredEntity(entity);
    });
    return entity;
}

Entity ECS::createEntityImmediate()
{
    std::lock_guard lock(m_structuralMutex);
    return m_entities.create();
}

Entity ECS::reserveEntityForDeferredCreate()
{
    std::lock_guard lock(m_structuralMutex);
    return Entity{ m_nextDeferredEntityIndex++, EntityRecord {}.version };
}

bool ECS::activateDeferredEntity(const Entity& entity)
{
    std::lock_guard lock(m_structuralMutex);
    return m_entities.activateReserved(entity);
}

void ECS::destroyEntity(const Entity& entity)
{
    if (structural_changes_deferred())
    {
        if (!is_known_handle(entity))
            return;

        m_deferredStructuralCommands.enqueue([this, entity]()
        {
            destroyEntityImmediate(entity);
        });
        return;
    }

    destroyEntityImmediate(entity);
}

void ECS::destroyEntityImmediate(const Entity& entity)
{
    if (!is_valid_handle(entity))
        return;

    m_components.eraseEntityFromAll(entity.index);
    remove_tags_for_entity(entity.index);
    m_entities.destroy(entity);
}

bool ECS::hasEntity(const Entity& entity) const
{
    return is_valid_handle(entity);
}

void ECS::clear()
{
    if (structural_changes_deferred())
    {
        m_deferredStructuralCommands.enqueue([this]()
        {
            clearImmediate();
        });
        return;
    }

    clearImmediate();
}

void ECS::clearImmediate()
{
    m_entities.clear();
    m_components.clearPools();
    m_tags.clear();
    ++m_componentQueryGeneration;
    ++m_renderQueryGeneration;
    ++m_tagGeneration;
}

void ECS::remove_tags_for_entity(const size_t entityIndex)
{
    bool removedAny = false;
    for (auto& [type, pool] : m_tags)
    {
        (void)type;
        removedAny = pool.remove(entityIndex) || removedAny;
    }

    if (removedAny)
        ++m_tagGeneration;
}

void ECS::appendAliveEntities(ArrayList<Entity>& entities) const
{
    const ArrayList<EntityRecord>& records = entity_records();
    entities.reserve(records.length());

    for (size_t index = 0; index < records.length(); ++index)
    {
        const EntityRecord& record = records[index];
        if (record.alive)
            entities.append(make_handle(record, index));
    }
}

void ECS::swapSimBuffers()
{ m_components.swapSimulationBuffers(); }

void ECS::swapRenderBuffers()
{
    m_components.swapRenderBuffers();
    ++m_renderQueryGeneration;
}

static void apply_entity_filter(
    const ECS& ecs,
    ArrayList<Entity>& entities,
    const ecs::query_detail::EntityFilter& filter,
    const bool keepMatches
) {
    ArrayList<Entity> filtered(entities.length());
    for (const Entity& entity : entities)
    {
        const bool matches = nullptr != filter.matches && filter.matches(ecs, entity);
        if (matches == keepMatches)
            filtered.append(entity);
    }

    entities = std::move(filtered);
}

ArrayList<Entity> ECS::filteredEntities(const ArrayList<ecs::query_detail::EntityFilter>& filters) const
{
    ArrayList<ecs::query_detail::EntityFilter> includeFilters(filters.length());
    ArrayList<ecs::query_detail::EntityFilter> excludeFilters(filters.length());

    for (const ecs::query_detail::EntityFilter& filter : filters)
    {
        if (nullptr == filter.matches)
            continue;

        if (ecs::query_detail::EntityFilterMode::Exclude == filter.mode)
            excludeFilters.append(filter);
        else
            includeFilters.append(filter);
    }

    includeFilters.sort([](
        const ecs::query_detail::EntityFilter& lhs,
        const ecs::query_detail::EntityFilter& rhs
    )
    {
        return lhs.size < rhs.size;
    });

    excludeFilters.sort([](
        const ecs::query_detail::EntityFilter& lhs,
        const ecs::query_detail::EntityFilter& rhs
    )
    {
        return lhs.size < rhs.size;
    });

    ArrayList<Entity> entities;
    if (includeFilters.empty())
    {
        appendAliveEntities(entities);
    }
    else
    {
        if (0 == includeFilters[0].size || nullptr == includeFilters[0].appendEntities)
            return entities;

        entities.reserve(includeFilters[0].size);
        includeFilters[0].appendEntities(*this, entities);
        for (size_t filterIndex = 1; filterIndex < includeFilters.length() && !entities.empty(); ++filterIndex)
            apply_entity_filter(*this, entities, includeFilters[filterIndex], true);
    }

    for (const ecs::query_detail::EntityFilter& filter : excludeFilters)
    {
        if (entities.empty())
            break;

        if (0 == filter.size)
            continue;

        apply_entity_filter(*this, entities, filter, false);
    }

    return entities;
}

void ECS::markComponentDirty(const ecs::ComponentTypeId componentTypeId)
{ m_components.markDirty(componentTypeId); }

void ECS::markComponentEntityDirty(const ecs::ComponentTypeId componentTypeId, const size_t entityIndex)
{ m_components.markEntityDirty(componentTypeId, entityIndex); }

bool ECS::structural_changes_deferred() const
{
    return 0 != m_structuralDeferralDepth;
}

void ECS::beginStructuralDeferral()
{
    std::lock_guard lock(m_structuralMutex);
    if (0 == m_structuralDeferralDepth)
        m_nextDeferredEntityIndex = m_entities.nextIndex();

    ++m_structuralDeferralDepth;
}

void ECS::endStructuralDeferral()
{
    std::lock_guard lock(m_structuralMutex);
    if (0 == m_structuralDeferralDepth)
        throw std::logic_error("ECS structural deferral ended without a matching begin");

    --m_structuralDeferralDepth;
}

void ECS::flushDeferredStructuralChanges()
{
    if (structural_changes_deferred())
        throw std::logic_error("Cannot flush ECS structural changes while deferral is still active");

    auto commands = m_deferredStructuralCommands.drain();
    for (auto& command : commands)
        command();
}

void ECS::discardDeferredStructuralChanges()
{
    m_deferredStructuralCommands.clear();
}
