//
// Entity handle lifetime, free-list reuse, and generation tracking.
//

#include "entity_registry.h"

Entity EntityRegistry::makeHandle(const EntityRecord& record, const size_t index)
{
    return Entity{ index, record.version };
}

Entity EntityRegistry::makeHandle(const size_t index) const
{
    return makeHandle(m_records[index], index);
}

bool EntityRegistry::isAliveIndex(const size_t index) const
{
    return index < m_records.length() && m_records[index].alive;
}

bool EntityRegistry::isValidHandle(const Entity& entity) const
{
    return entity.valid() &&
        entity.index < m_records.length() &&
        m_records[entity.index].alive &&
        m_records[entity.index].version == entity.version;
}

Entity EntityRegistry::create()
{
    size_t index = 0;

    if (!m_freeList.empty())
    {
        index = m_freeList.pop();
    }
    else
    {
        index = m_records.length();
        m_records.append(EntityRecord {});
    }

    auto& [version, alive] = m_records[index];
    alive = true;
    ++m_generation;

    return Entity{ index, version };
}

bool EntityRegistry::destroy(const Entity& entity)
{
    if (!isValidHandle(entity))
        return false;

    auto& [version, alive] = m_records[entity.index];
    alive = false;
    ++version;
    ++m_generation;
    m_freeList.append(entity.index);
    return true;
}

void EntityRegistry::clear()
{
    m_freeList.clear();
    m_records.clear();
    ++m_generation;
}

size_t EntityRegistry::generation() const
{
    return m_generation;
}

size_t EntityRegistry::aliveCount() const
{
    size_t alive = 0;
    for (const EntityRecord& record : m_records)
    {
        if (record.alive)
            ++alive;
    }
    return alive;
}

const ArrayList<EntityRecord>& EntityRegistry::records() const
{
    return m_records;
}
