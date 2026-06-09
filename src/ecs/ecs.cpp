//
// Created by Nicholas on 26/04/26.
//

#include "ecs.h"

Entity ECS::make_handle(const EntityRecord& record, const size_t& index)
{ return EntityRegistry::makeHandle(record, index); }

Entity ECS::make_handle(const size_t& index)
{ return m_entities.makeHandle(index); }

bool ECS::is_alive_index(const size_t index) const
{ return m_entities.isAliveIndex(index); }

bool ECS::is_valid_handle(const Entity& entity) const
{ return m_entities.isValidHandle(entity); }

size_t ECS::alive_entity_count() const
{ return m_entities.aliveCount(); }

const ArrayList<EntityRecord>& ECS::entity_records() const
{ return m_entities.records(); }

Entity ECS::createEntity()
{ return m_entities.create(); }

void ECS::destroyEntity(const Entity& entity)
{
    if (!is_valid_handle(entity))
        return;

    m_components.eraseEntityFromAll(entity.index);
    m_entities.destroy(entity);
}

bool ECS::hasEntity(const Entity& entity) const
{
    return is_valid_handle(entity);
}

void ECS::clear()
{
    m_entities.clear();
    m_components.clearPools();
}

void ECS::swapSimBuffers()
{ m_components.swapSimulationBuffers(); }

void ECS::swapRenderBuffers()
{ m_components.swapRenderBuffers(); }

void ECS::markComponentDirty(const ecs::ComponentTypeId componentTypeId)
{ m_components.markDirty(componentTypeId); }

void ECS::markComponentEntityDirty(const ecs::ComponentTypeId componentTypeId, const size_t entityIndex)
{ m_components.markEntityDirty(componentTypeId, entityIndex); }
