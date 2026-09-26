#pragma once

#include "../core/entt_storage.h"
#include "../../structs/arraylist.h"

// Tags use EnTT's sparse set. The returned array is a read-only compatibility
// snapshot, not a second membership index.
class TagPool
{
    ecs::BackendSet m_entities;
    size_t m_generation = 0;
    mutable ArrayList<size_t> m_snapshot;
    mutable size_t m_snapshotGeneration = static_cast<size_t>(-1);
public:
    size_t generation() const { return m_generation; }
    size_t size() const { return m_entities.size(); }
    bool empty() const { return m_entities.empty(); }
    bool contains(size_t entity) const { return m_entities.contains(entity); }
    bool add(size_t entity)
    {
        if (contains(entity)) return false;
        m_entities.push(entity);
        ++m_generation;
        return true;
    }
    bool remove(size_t entity)
    {
        if (!m_entities.remove(entity)) return false;
        ++m_generation;
        return true;
    }
    bool set(size_t entity, bool value) { return value ? add(entity) : remove(entity); }
    void clear()
    {
        if (empty()) return;
        m_entities.clear();
        ++m_generation;
    }
    ecs::BackendSet& backend() { return m_entities; }
    const ArrayList<size_t>& entity_indices() const
    {
        if (m_snapshotGeneration != m_generation)
        {
            m_snapshot.clear();
            for (size_t index = 0; index < size(); ++index)
                m_snapshot.append(static_cast<size_t>(m_entities.data()[index]));
            m_snapshotGeneration = m_generation;
        }
        return m_snapshot;
    }
};
