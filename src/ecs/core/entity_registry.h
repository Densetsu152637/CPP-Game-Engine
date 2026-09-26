//
// Entity handle lifetime, free-list reuse, and generation tracking.
//

#pragma once

#include "entity.h"
#include <entt/entity/registry.hpp>
#include "../../structs/arraylist.h"

class EntityRegistry
{
    ArrayList<EntityRecord> m_records;
    entt::basic_registry<uint64_t> m_registry;
    size_t m_generation = 0;

    void record(uint64_t entity, bool alive);

public:
    Entity create();
    bool activateReserved(const Entity& entity);
    bool destroy(const Entity& entity);
    void clear();

    bool isAliveIndex(size_t index) const;
    bool isValidHandle(const Entity& entity) const;
    bool isKnownHandle(const Entity& entity) const;
    Entity makeHandle(size_t index) const;
    static Entity makeHandle(const EntityRecord& record, size_t index);

    size_t generation() const;
    size_t aliveCount() const;
    size_t nextIndex() const;
    const ArrayList<EntityRecord>& records() const;
};
