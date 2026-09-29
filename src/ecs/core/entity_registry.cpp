// Entity allocation and generation reuse are owned by EnTT. Records are a
// compatibility snapshot for existing entity-only iteration, never a free list.
#include "entity_registry.h"

namespace
{
    using Traits = entt::entt_traits<uint64_t>;
    uint64_t native(const Entity entity)
    { return Traits::construct(static_cast<uint32_t>(entity.index), entity.version - 1); }
    Entity handle(const uint64_t entity)
    { return {Traits::to_entity(entity), Traits::to_version(entity) + 1}; }
}

void EntityRegistry::record(const uint64_t entity, const bool alive)
{
    const Entity value = handle(entity);
    while (m_records.length() <= value.index)
        m_records.append(EntityRecord{});
    m_records[value.index] = EntityRecord{value.version, alive};
}

Entity EntityRegistry::makeHandle(const EntityRecord& value, const size_t index)
{ return {index, value.version}; }
Entity EntityRegistry::makeHandle(const size_t index) const
{ return index < m_records.length() ? makeHandle(m_records[index], index) : Entity{}; }
bool EntityRegistry::isAliveIndex(const size_t index) const
{ return index < m_records.length() && isValidHandle(makeHandle(index)); }
bool EntityRegistry::isValidHandle(const Entity& entity) const
{ return entity.valid() && entity.version != 0 && entity.index < Traits::entity_mask && m_registry.valid(native(entity)); }
bool EntityRegistry::isKnownHandle(const Entity& entity) const
{ return isValidHandle(entity); }
Entity EntityRegistry::create()
{
    const auto entity = m_registry.create();
    record(entity, true);
    ++m_generation;
    return handle(entity);
}
bool EntityRegistry::activateReserved(const Entity& entity)
{
    if (!entity.valid() || entity.version == 0 || entity.index >= Traits::entity_mask || isAliveIndex(entity.index))
        return false;
    const auto created = m_registry.create(native(entity));
    if (created != native(entity))
    {
        m_registry.destroy(created);
        return false;
    }
    record(created, true);
    ++m_generation;
    return true;
}
void EntityRegistry::invalidateReserved(const Entity& entity)
{
    if (!entity.valid() || entity.version == 0 || entity.index >= Traits::entity_mask || isValidHandle(entity)) return;
    if (m_registry.current(static_cast<uint32_t>(entity.index)) != entity.version - 1) return;
    const auto created = m_registry.create(native(entity));
    if (created != native(entity)) return;
    const auto version = m_registry.destroy(created);
    record(Traits::construct(static_cast<uint32_t>(entity.index), version), false);
    ++m_generation;
}
bool EntityRegistry::destroy(const Entity& entity)
{
    if (!isValidHandle(entity)) return false;
    const auto version = m_registry.destroy(native(entity));
    record(Traits::construct(static_cast<uint32_t>(entity.index), version), false);
    ++m_generation;
    return true;
}
void EntityRegistry::clear()
{
    // Keep EnTT's generations so pre-clear handles can never alias new entities.
    m_registry.clear();
    for (size_t index = 0; index < m_records.length(); ++index)
        record(Traits::construct(static_cast<uint32_t>(index), m_registry.current(index)), false);
    ++m_generation;
}
size_t EntityRegistry::generation() const { return m_generation; }
size_t EntityRegistry::aliveCount() const { return m_registry.storage<uint64_t>()->free_list(); }
size_t EntityRegistry::nextIndex() const { return m_records.length(); }
const ArrayList<EntityRecord>& EntityRegistry::records() const { return m_records; }
