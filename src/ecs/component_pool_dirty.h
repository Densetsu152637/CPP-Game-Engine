//
// Dirty-state tracking for ECS component pools.
//

#pragma once

#include <algorithm>
#include <cstddef>
#include <unordered_set>

#include "../structs/arraylist.h"

enum class ComponentDirtyMode
{
    None,
    Entities,
    Full
};

class ComponentPoolDirtyTracker
{
    static constexpr size_t MIN_PRECISE_DIRTY_ENTITIES = 64;
    static constexpr size_t DIRTY_RATIO_NUMERATOR = 1;
    static constexpr size_t DIRTY_RATIO_DENOMINATOR = 3;

    ComponentDirtyMode m_mode = ComponentDirtyMode::Full;
    ArrayList<size_t> m_entities;
    std::unordered_set<size_t> m_entitySet;

    static size_t full_dirty_threshold(const size_t poolSize)
    {
        if (0 == poolSize)
            return 0;

        const size_t ratioThreshold = std::max<size_t>(
            1,
            (poolSize * DIRTY_RATIO_NUMERATOR) / DIRTY_RATIO_DENOMINATOR
        );

        return std::min(
            poolSize,
            std::max(MIN_PRECISE_DIRTY_ENTITIES, ratioThreshold)
        );
    }

public:
    bool dirty() const
    { return ComponentDirtyMode::None != m_mode; }

    bool fullyDirty() const
    { return ComponentDirtyMode::Full == m_mode; }

    ComponentDirtyMode mode() const
    { return m_mode; }

    const ArrayList<size_t>& entities() const
    { return m_entities; }

    void markFull()
    {
        m_mode = ComponentDirtyMode::Full;
        m_entities.clear();
        m_entitySet.clear();
    }

    void markEntity(const size_t entityIndex, const size_t poolSize)
    {
        if (ComponentDirtyMode::Full == m_mode)
            return;

        if (m_entitySet.insert(entityIndex).second)
            m_entities.append(entityIndex);

        if (m_entities.length() >= full_dirty_threshold(poolSize))
        {
            markFull();
            return;
        }

        m_mode = ComponentDirtyMode::Entities;
    }

    void clear()
    {
        m_mode = ComponentDirtyMode::None;
        m_entities.clear();
        m_entitySet.clear();
    }
};
