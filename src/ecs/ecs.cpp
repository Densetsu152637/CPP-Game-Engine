//
// Created by Nicholas on 26/04/26.
//

#include "ecs.h"

#include <ranges>

#include "entity.h"

size_t ECS::_next_entity_id()
{
    // prevents memory fragmentation and having to iterate through pages to modify entries
    std::lock_guard<std::mutex> lock(creationLock);
    const size_t ret = m_nextEntityId++;
    if (m_nextEntityId < m_entities.length())
    {
        // loops while the index is not -1 to find the next vacant spot that was previously deleted
        while (m_entities[m_nextEntityId++].valid()) {}
    }
    return ret;
}

size_t ECS::createEntity()
{
    const std::size_t id = _next_entity_id();
    m_entities.emplace(true);
    return id;
}

void ECS::destroyEntity(const size_t id)
{
    std::lock_guard<std::mutex> lock(creationLock);
    (m_entities[id]).~Entity();
    m_nextEntityId = std::min(m_nextEntityId, id);
}

bool ECS::hasEntity(const size_t id) const
{
    return m_entities.at(id).valid();
}

Entity& ECS::getEntity(const size_t id)
{
    return m_entities.at(id);
}

void ECS::clear()
{
    std::lock_guard<std::mutex> lock(creationLock);
    m_nextEntityId = 0;
    m_entities.clear();
    for (auto& bucket : m_components | std::views::values)
    {
        bucket.clear();
    }
}
