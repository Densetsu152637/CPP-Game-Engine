//
// Shared simulation component-pool specialization.
//

#pragma once

#include <stdexcept>
#include <utility>

#include "../core/entt_storage.h"
#include "component_pool_base.h"

template <typename T>
class SharedComponentPool final : public IComponentPool
{
    static constexpr size_t N_POS = static_cast<size_t>(-1);

    struct SharedGroup
    {
        T value;
        ArrayList<size_t> entities;

        explicit SharedGroup(T&& sharedValue)
            : value(std::move(sharedValue))
        {}
    };

    ArrayList<SharedGroup> m_groups;
    ecs::EnTTStorage<size_t> m_entityGroupIndex;
    mutable ArrayList<T> m_denseSnapshot;
    mutable bool m_denseSnapshotDirty = true;

public:
    using value_type = T;
    using dense_array_type = ArrayList<T>;

    static_assert(
        ecs::is_shared_component_alias_v<T>,
        "SharedComponentPool can only store ecs::SharedAlias component types"
    );

    ecs::ComponentTypeId type_id() const override
    { return ecs::component_type_id<T>(); }

    const char* type_name() const override
    { return ecs::component_type_name<T>(); }

    size_t size() const override
    { return m_entityGroupIndex.size(); }

    bool contains(const size_t entityIndex) const
    { return m_entityGroupIndex.contains(entityIndex); }

    bool containsEntity(const size_t entityIndex) const override
    { return contains(entityIndex); }

    T* try_get(const size_t entityIndex)
    {
        const size_t* groupIndex = m_entityGroupIndex.try_get(entityIndex);
        if (nullptr == groupIndex || *groupIndex >= m_groups.length())
            return nullptr;

        return &m_groups[*groupIndex].value;
    }

    const T* try_get(const size_t entityIndex) const
    {
        const size_t* groupIndex = m_entityGroupIndex.try_get(entityIndex);
        if (nullptr == groupIndex || *groupIndex >= m_groups.length())
            return nullptr;

        return &m_groups[*groupIndex].value;
    }

    T& at(const size_t entityIndex)
    {
        T* component = try_get(entityIndex);
        if (nullptr == component)
            throw std::out_of_range("SharedComponentPool entity does not have component");

        return *component;
    }

    const T& at(const size_t entityIndex) const
    {
        const T* component = try_get(entityIndex);
        if (nullptr == component)
            throw std::out_of_range("SharedComponentPool entity does not have component");

        return *component;
    }

    template <typename... Args>
    T& emplace(const size_t entityIndex, Args&&... args)
    { return assign(entityIndex, T(std::forward<Args>(args)...)); }

    T& insert_or_assign(const size_t entityIndex, T value)
    { return assign(entityIndex, std::move(value)); }

    void erase(const size_t entityIndex) override
    {
        if (!contains(entityIndex))
            return;

        this->markEntityDirty(entityIndex);
        removeEntityFromCurrentGroup(entityIndex);
        this->bumpGeneration();
        m_denseSnapshotDirty = true;
    }

    void clear() override
    {
        if (!m_entityGroupIndex.empty())
            this->bumpGeneration();

        this->markDirty();
        m_groups.clear();
        m_entityGroupIndex.clear();
        m_denseSnapshot.clear();
        m_denseSnapshotDirty = false;
    }

    size_t entity_at(const size_t denseIndex) const
    { return entityAt(denseIndex); }

    size_t entityAt(const size_t denseIndex) const override
    { return m_entityGroupIndex.key_at(denseIndex); }

    T& dense_at(const size_t denseIndex)
    { return at(entityAt(denseIndex)); }

    const T& dense_at(const size_t denseIndex) const
    { return at(entityAt(denseIndex)); }

    bool copyComponentTo(const size_t entityIndex, void* destination) const override
    {
        if (nullptr == destination)
            return false;

        const T* component = try_get(entityIndex);
        if (nullptr == component)
            return false;

        *static_cast<T*>(destination) = *component;
        return true;
    }

    ArrayList<T>& dense()
    {
        refreshDenseSnapshot();
        return m_denseSnapshot;
    }

    const ArrayList<T>& dense() const
    {
        refreshDenseSnapshot();
        return m_denseSnapshot;
    }

    size_t dense_index_of(const size_t entityIndex) const
    {
        return m_entityGroupIndex.index_of(entityIndex);
    }

    ecs::BackendSet& backend() { return m_entityGroupIndex.backend(); }

    void swapBuffers() override
    {}

    void markDirty() override
    {
        IComponentPool::markDirty();
        m_denseSnapshotDirty = true;
    }

    void markEntityDirty(const size_t entityIndex) override
    {
        IComponentPool::markEntityDirty(entityIndex);
        m_denseSnapshotDirty = true;
    }

private:
    T& assign(const size_t entityIndex, T value)
    {
        const bool existed = contains(entityIndex);
        if (existed)
            removeEntityFromCurrentGroup(entityIndex, false);

        const size_t groupIndex = ensureGroup(std::move(value));
        m_entityGroupIndex.insert_or_assign(entityIndex, groupIndex);
        m_groups[groupIndex].entities.append(entityIndex);

        noteEntityMutation(entityIndex, !existed);
        return m_groups[groupIndex].value;
    }

    size_t findGroup(const T& value) const
    {
        for (size_t groupIndex = 0; groupIndex < m_groups.length(); ++groupIndex)
        {
            if (m_groups[groupIndex].value == value)
                return groupIndex;
        }

        return N_POS;
    }

    size_t ensureGroup(T value)
    {
        const size_t existing = findGroup(value);
        if (N_POS != existing)
            return existing;

        const size_t groupIndex = m_groups.length();
        m_groups.emplace(std::move(value));
        return groupIndex;
    }

    void removeEntityFromCurrentGroup(const size_t entityIndex, const bool eraseMembership = true)
    {
        const size_t* groupIndexPtr = m_entityGroupIndex.try_get(entityIndex);
        if (nullptr == groupIndexPtr)
            return;

        const size_t groupIndex = *groupIndexPtr;
        if (groupIndex < m_groups.length())
            removeEntityFromGroup(groupIndex, entityIndex);

        // Reassignment preserves EnTT membership and its dense index, so cached
        // component views remain paired with the same entities.
        if (eraseMembership)
            m_entityGroupIndex.erase(entityIndex);
    }

    void removeEntityFromGroup(const size_t groupIndex, const size_t entityIndex)
    {
        SharedGroup& group = m_groups[groupIndex];
        group.entities.remove(entityIndex);
        if (!group.entities.empty())
            return;

        removeGroup(groupIndex);
    }

    void removeGroup(const size_t groupIndex)
    {
        const size_t lastIndex = m_groups.length() - 1;
        if (groupIndex != lastIndex)
        {
            m_groups[groupIndex] = std::move(m_groups[lastIndex]);
            for (const size_t movedEntityIndex : m_groups[groupIndex].entities)
                m_entityGroupIndex.insert_or_assign(movedEntityIndex, groupIndex);
        }

        m_groups.pop();
    }

    void refreshDenseSnapshot() const
    {
        if (!m_denseSnapshotDirty)
            return;

        m_denseSnapshot.clear();
        m_denseSnapshot.reserve(m_entityGroupIndex.size());
        for (size_t index = 0; index < m_entityGroupIndex.size(); ++index)
            m_denseSnapshot.append(at(entityAt(index)));

        m_denseSnapshotDirty = false;
    }

    void noteEntityMutation(const size_t entityIndex, const bool structural)
    {
        if (structural)
            this->bumpGeneration();

        this->markEntityDirty(entityIndex);
    }
};
