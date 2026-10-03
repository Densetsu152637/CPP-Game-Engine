//
// Created by Nicholas on 26/04/26.
//

#include "ecs.h"

#include <algorithm>

Entity ECS::make_handle(const size_t& index) const
{ return m_entities.makeHandle(index); }

bool ECS::is_alive_index(const size_t index) const
{ return m_entities.isAliveIndex(index); }

bool ECS::is_known_handle(const Entity& entity) const
{
    if (m_entities.isKnownHandle(entity))
        return true;

    if (!structural_changes_deferred() && !m_validatingDeferredStructural)
        return false;

    std::lock_guard lock(m_structuralMutex);
    return std::find(m_deferredReservedEntities.begin(), m_deferredReservedEntities.end(), entity) !=
        m_deferredReservedEntities.end();
}

bool ECS::is_valid_handle(const Entity& entity) const
{ return m_entities.isValidHandle(entity); }

size_t ECS::alive_entity_count() const
{ return m_entities.aliveCount(); }

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
    const Entity entity{ m_nextDeferredEntityIndex++, 1 };
    m_deferredReservedEntities.push_back(entity);
    return entity;
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
    if (m_dynamicComponents.eraseEntity(entity)) bumpComponentQueryGeneration();
    remove_tags_for_entity(entity.index);
    m_entities.destroy(entity);
}

bool ECS::hasEntity(const Entity& entity) const
{
    return is_valid_handle(entity);
}

bool ECS::registerDynamicComponent(const std::string_view name, const std::vector<ecs::DynamicField>& fields,
    const std::uint32_t version)
{
    const bool existed = m_dynamicComponents.hasSchema(name);
    const bool registered = m_dynamicComponents.registerComponent(name, fields, version);
    if (registered && !existed) bumpComponentQueryGeneration();
    return registered;
}

bool ECS::hasDynamicComponentSchema(const std::string_view name) const
{ return m_dynamicComponents.hasSchema(name); }

bool ECS::unregisterDynamicComponent(const std::string_view name)
{
    const bool removed = m_dynamicComponents.unregisterComponent(name);
    if (removed) bumpComponentQueryGeneration();
    return removed;
}

bool ECS::rollbackDynamicComponentSchema(const std::string_view name)
{
    const bool removed = m_dynamicComponents.unregisterComponent(name, true);
    if (removed) bumpComponentQueryGeneration();
    return removed;
}

void ECS::pruneEmptyDynamicComponentSchemas()
{
    if (m_dynamicComponents.pruneEmptySchemas() != 0) bumpComponentQueryGeneration();
}

bool ECS::setDynamicComponent(const Entity& entity, const std::string_view name, const ecs::DynamicValues& values)
{
    if (!is_known_handle(entity) || !m_dynamicComponents.validate(name, values)) return false;
    const std::string componentName(name);
    if (structural_changes_deferred())
    {
        m_deferredStructuralCommands.enqueueValidated(
        [this, entity] { return is_known_handle(entity); },
        [this, entity, componentName, values]
        {
            if (!is_valid_handle(entity)) return;
            const bool existed = m_dynamicComponents.get(entity, componentName).has_value();
            if (m_dynamicComponents.set(entity, componentName, values) && !existed) bumpComponentQueryGeneration();
        });
        return true;
    }
    const bool existed = m_dynamicComponents.get(entity, componentName).has_value();
    const bool changed = m_dynamicComponents.set(entity, componentName, values);
    if (changed && !existed) bumpComponentQueryGeneration();
    return changed;
}

std::optional<ecs::DynamicValues> ECS::getDynamicComponent(const Entity& entity, const std::string_view name) const
{
    if (!is_valid_handle(entity)) return std::nullopt;
    return m_dynamicComponents.get(entity, name);
}

bool ECS::removeDynamicComponent(const Entity& entity, const std::string_view name)
{
    if (!is_known_handle(entity) || !m_dynamicComponents.hasSchema(name)) return false;
    const std::string componentName(name);
    if (structural_changes_deferred())
    {
        m_deferredStructuralCommands.enqueue([this, entity, componentName]
        {
            if (is_valid_handle(entity) && m_dynamicComponents.remove(entity, componentName)) bumpComponentQueryGeneration();
        });
        return true;
    }
    const bool removed = m_dynamicComponents.remove(entity, componentName);
    if (removed) bumpComponentQueryGeneration();
    return removed;
}

std::vector<Entity> ECS::queryDynamicComponents(const std::vector<std::string>& all) const
{
    auto result = m_dynamicComponents.query(all);
    result.erase(std::remove_if(result.begin(), result.end(), [this](const Entity& entity) { return !is_valid_handle(entity); }), result.end());
    return result;
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
    m_dynamicComponents.clear();
    m_tags.clear();
    bumpComponentQueryGeneration();
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
    entities.reserve(m_entities.aliveCount());
    m_entities.eachAlive([&entities](const Entity& entity)
    {
        entities.append(entity);
    });
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
    m_validatingDeferredStructural = true;
    try
    {
        for (const auto& command : commands)
            if (!command.validate()) throw std::runtime_error("Deferred ECS structural command validation failed; no commands were applied");
    }
    catch (...)
    {
        m_validatingDeferredStructural = false;
        discardDeferredStructuralChanges();
        throw;
    }
    m_validatingDeferredStructural = false;
    m_applyingDeferredStructural = true;
    m_deferredComponentQueryDirty = false;
    try
    {
        for (auto& command : commands) command.apply();
    }
    catch (...)
    {
        m_applyingDeferredStructural = false;
        if (m_deferredComponentQueryDirty) ++m_componentQueryGeneration;
        m_deferredComponentQueryDirty = false;
        throw;
    }
    m_applyingDeferredStructural = false;
    if (m_deferredComponentQueryDirty) ++m_componentQueryGeneration;
    m_deferredComponentQueryDirty = false;
    m_deferredReservedEntities.clear();
}

void ECS::discardDeferredStructuralChanges()
{
    m_deferredStructuralCommands.clear();
    for (const Entity& entity : m_deferredReservedEntities) m_entities.invalidateReserved(entity);
    m_deferredReservedEntities.clear();
}
