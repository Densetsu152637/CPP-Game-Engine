// Entity allocation, generations, and live iteration are owned by EnTT.
#include "entity_registry.h"

#include <algorithm>

namespace
{
    using Traits = entt::entt_traits<uint64_t>;
    uint64_t native(const Entity entity)
    { return Traits::construct(static_cast<uint32_t>(entity.index), entity.version - 1); }
    Entity handle(const uint64_t entity)
    { return {Traits::to_entity(entity), Traits::to_version(entity) + 1}; }
}

Entity EntityRegistry::makeHandle(const size_t index) const
{
    if (index >= m_nextIndex) return {};
    const auto version = m_registry.current(static_cast<uint64_t>(index));
    return {index, version + 1};
}
bool EntityRegistry::isAliveIndex(const size_t index) const
{ return index < m_nextIndex && isValidHandle(makeHandle(index)); }
bool EntityRegistry::isValidHandle(const Entity& entity) const
{ return entity.valid() && entity.version != 0 && entity.index < Traits::entity_mask && m_registry.valid(native(entity)); }
bool EntityRegistry::isKnownHandle(const Entity& entity) const
{ return isValidHandle(entity); }
Entity EntityRegistry::create()
{
    const auto entity = m_registry.create();
    m_nextIndex = std::max(m_nextIndex, static_cast<size_t>(Traits::to_entity(entity)) + 1);
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
    m_nextIndex = std::max(m_nextIndex, static_cast<size_t>(entity.index) + 1);
    ++m_generation;
    return true;
}
void EntityRegistry::invalidateReserved(const Entity& entity)
{
    if (!entity.valid() || entity.version == 0 || entity.index >= Traits::entity_mask || isValidHandle(entity)) return;
    const auto current = m_registry.current(static_cast<uint64_t>(entity.index));
    if (current != entity.version - 1 && current != Traits::version_mask) return;
    const auto created = m_registry.create(native(entity));
    if (created != native(entity)) return;
    m_registry.destroy(created);
    m_nextIndex = std::max(m_nextIndex, entity.index + 1);
    ++m_generation;
}
bool EntityRegistry::destroy(const Entity& entity)
{
    if (!isValidHandle(entity)) return false;
    m_registry.destroy(native(entity));
    ++m_generation;
    return true;
}
void EntityRegistry::clear()
{
    // Keep EnTT's generations so pre-clear handles can never alias new entities.
    m_registry.clear();
    ++m_generation;
}
size_t EntityRegistry::generation() const { return m_generation; }
size_t EntityRegistry::aliveCount() const { return m_registry.storage<uint64_t>()->free_list(); }
size_t EntityRegistry::nextIndex() const { return m_nextIndex; }
