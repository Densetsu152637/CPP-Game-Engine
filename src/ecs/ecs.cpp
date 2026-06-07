//
// Created by Nicholas on 26/04/26.
//

#include "ecs.h"

#include <format>
#include <ranges>

Entity ECS::make_handle(const EntityRecord& record, const size_t& index)
{ return Entity{ index, record.version }; }

Entity ECS::make_handle(const size_t& index)
{ return make_handle(m_entities[index], index); }

bool ECS::is_alive_index(const size_t index) const
{ return index < m_entities.length() && m_entities[index].alive; }

bool ECS::is_valid_handle(const Entity& entity) const
{
    return entity.valid() &&
       entity.index < m_entities.length() &&
       m_entities[entity.index].alive &&
       m_entities[entity.index].version == entity.version;
}

Entity ECS::createEntity()
{
    size_t index = 0;

    if (!m_freeList.empty())
    {
        index = m_freeList.pop();
    } else
    {
        index = m_entities.length();
        m_entities.append(EntityRecord {} );
    }

    auto& [version, alive] = m_entities[index];
    alive = true;
    ++m_entityGeneration;

    return Entity { index, version };
}

void ECS::destroyEntity(const Entity& entity)
{
    if (!is_valid_handle(entity))
        return;

    auto& [version, alive] = m_entities[entity.index];
    alive = false;
    ++version;
    ++m_entityGeneration;
    m_freeList.append(entity.index);

    for (const auto& pool : m_componentPools | std::views::values)
    {
        pool->erase(entity.index);
    }

    for (const auto& pool : m_renderComponentPools | std::views::values)
    {
        pool->erase(entity.index);
    }
}

bool ECS::hasEntity(const Entity& entity) const
{
    return is_valid_handle(entity);
}

void ECS::clear()
{
    m_freeList.clear();
    m_entities.clear();
    ++m_entityGeneration;

    for (auto& pool : m_componentPools | std::views::values)
    {
        pool->clear();
    }

    for (auto& pool : m_renderComponentPools | std::views::values)
    {
        pool->clear();
    }
}

void ECS::swapSimBuffers()
{
    for (auto& pool : m_componentPools | std::views::values)
    {
        pool->swapBuffers();
    }
}

void ECS::swapRenderBuffers()
{
    for (auto& pool : m_renderComponentPools | std::views::values)
    {
        pool->swapBuffers();
    }
}

void ECS::markComponentDirty(const ecs::ComponentTypeId componentTypeId)
{
    const auto it = m_componentPools.find(componentTypeId);
    if (it != m_componentPools.end())
        it->second->markDirty();
}

void ECS::markComponentEntityDirty(const ecs::ComponentTypeId componentTypeId, const size_t entityIndex)
{
    const auto it = m_componentPools.find(componentTypeId);
    if (it != m_componentPools.end())
        it->second->markEntityDirty(entityIndex);
}
