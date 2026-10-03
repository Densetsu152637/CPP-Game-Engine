//
// Entity handle lifetime, free-list reuse, and generation tracking.
//

#pragma once

#include "entity.h"
#include <entt/entity/registry.hpp>

class EntityRegistry
{
    entt::basic_registry<uint64_t> m_registry;
    size_t m_generation = 0;
    // Deferred reservations advance monotonically even when EnTT has sparse live slots.
    size_t m_nextIndex = 0;

public:
    Entity create();
    bool activateReserved(const Entity& entity);
    void invalidateReserved(const Entity& entity);
    bool destroy(const Entity& entity);
    void clear();

    bool isAliveIndex(size_t index) const;
    bool isValidHandle(const Entity& entity) const;
    bool isKnownHandle(const Entity& entity) const;
    Entity makeHandle(size_t index) const;

    template <typename Func>
    void eachAlive(Func&& func) const
    {
        const auto* entities = m_registry.storage<uint64_t>();
        for (const auto [entity] : entities->each())
        {
            using Traits = entt::entt_traits<uint64_t>;
            func(Entity{Traits::to_entity(entity), Traits::to_version(entity) + 1});
        }
    }

    size_t generation() const;
    size_t aliveCount() const;
    size_t nextIndex() const;
};
