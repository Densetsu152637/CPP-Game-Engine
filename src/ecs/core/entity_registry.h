//
// Entity handle lifetime, free-list reuse, and generation tracking.
//

#pragma once

#include "entity.h"
#include "../../structs/arraylist.h"

class EntityRegistry
{
    ArrayList<EntityRecord> m_records;
    ArrayList<size_t> m_freeList;
    size_t m_generation = 0;

public:
    Entity create();
    bool destroy(const Entity& entity);
    void clear();

    bool isAliveIndex(size_t index) const;
    bool isValidHandle(const Entity& entity) const;
    Entity makeHandle(size_t index) const;
    static Entity makeHandle(const EntityRecord& record, size_t index);

    size_t generation() const;
    size_t aliveCount() const;
    const ArrayList<EntityRecord>& records() const;
};
