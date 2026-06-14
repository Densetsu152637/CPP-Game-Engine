//
// Base ECS component-pool interfaces and shared simulation storage implementation.
//

#pragma once

#include <mutex>
#include <stdexcept>
#include <utility>

#include "../../structs/templates.h"
#include "component_pool_dirty.h"
#include "component_pool_storage.h"
#include "../core/component_type_id.h"

namespace ecs
{
    class IArchetypePool;
}

template <typename T>
class ComponentPool;

template <typename T>
class BufferedComponentPool;

template <typename T>
class SharedComponentPool;

template <typename T>
class RenderComponentPool;

class IComponentPool
{
    ComponentPoolDirtyTracker m_dirty;
    std::mutex m_dirtyMutex;
    size_t m_generation = 0;

protected:
    void bumpGeneration()
    { ++m_generation; }

public:
    virtual ~IComponentPool() = default;
    virtual ecs::ComponentTypeId type_id() const = 0;
    virtual const char* type_name() const = 0;
    virtual void erase(size_t entityIndex) = 0;
    virtual void clear() = 0;
    virtual size_t size() const = 0;
    virtual bool containsEntity(size_t entityIndex) const = 0;
    virtual size_t entityAt(size_t denseIndex) const = 0;
    virtual bool copyComponentTo(size_t entityIndex, void* destination) const = 0;
    virtual void swapBuffers() = 0;
    virtual void writeFrom(IComponentPool&)
    { throw std::runtime_error("writeFrom is only supported by render component pools"); }
    virtual void writeFromArchetype(ecs::IArchetypePool&, ecs::ComponentTypeId)
    { throw std::runtime_error("writeFromArchetype is only supported by render component pools"); }

    size_t generation() const
    { return m_generation; }

    bool isDirty() const
    { return m_dirty.dirty(); }

    bool isFullyDirty() const
    { return m_dirty.fullyDirty(); }

    const ArrayList<size_t>& dirtyEntities() const
    { return m_dirty.entities(); }

    virtual void markDirty()
    {
        std::lock_guard lock(m_dirtyMutex);
        m_dirty.markFull();
    }

    virtual void markEntityDirty(const size_t entityIndex)
    {
        std::lock_guard lock(m_dirtyMutex);
        m_dirty.markEntity(entityIndex, size());
    }

    bool markFullIfEntityDirtyCountReachesThreshold(const size_t entityCount)
    {
        std::lock_guard lock(m_dirtyMutex);
        if (!m_dirty.wouldMarkFullAfterAddingEntities(size(), entityCount))
            return false;

        m_dirty.markFull();
        return true;
    }

    virtual void clearDirty()
    {
        std::lock_guard lock(m_dirtyMutex);
        m_dirty.clear();
    }
};

template <typename T, typename StoragePolicy>
class SimulationComponentPoolBase : public IComponentPool
{
    friend class RenderComponentPool<T>;

    StoragePolicy m_storage;

public:
    using value_type = T;
    using dense_array_type = ArrayList<T>;

    SimulationComponentPoolBase() = default;

    ecs::ComponentTypeId type_id() const override
    { return ecs::component_type_id<T>(); }

    const char* type_name() const override
    { return ecs::component_type_name<T>(); }

    size_t size() const override
    { return m_storage.size(); }

    bool contains(const size_t entityIndex) const
    { return m_storage.contains(entityIndex); }

    bool containsEntity(const size_t entityIndex) const override
    { return contains(entityIndex); }

    T* try_get(const size_t entityIndex)
    { return m_storage.try_get(entityIndex); }

    const T* try_get(const size_t entityIndex) const
    { return m_storage.try_get(entityIndex); }

    T& at(const size_t entityIndex)
    { return m_storage.at(entityIndex); }

    const T& at(const size_t entityIndex) const
    { return m_storage.at(entityIndex); }

    template <typename... Args>
    T& emplace(const size_t entityIndex, Args&&... args)
    {
        const bool existed = contains(entityIndex);
        T& component = m_storage.emplace(entityIndex, std::forward<Args>(args)...);
        noteEntityMutation(entityIndex, !existed);
        return component;
    }

    T& insert_or_assign(const size_t entityIndex, T value)
    {
        const bool existed = contains(entityIndex);
        T& component = m_storage.insert_or_assign(entityIndex, std::move(value));
        noteEntityMutation(entityIndex, !existed);
        return component;
    }

    void erase(const size_t entityIndex) override
    {
        if (!contains(entityIndex))
            return;

        this->markEntityDirty(entityIndex);
        this->bumpGeneration();
        m_storage.erase(entityIndex);
    }

    void clear() override
    {
        if (!m_storage.empty())
            this->bumpGeneration();

        this->markDirty();
        m_storage.clear();
    }

    size_t entity_at(const size_t denseIndex) const
    { return entityAt(denseIndex); }

    size_t entityAt(const size_t denseIndex) const override
    { return m_storage.entity_at(denseIndex); }

    T& dense_at(const size_t denseIndex)
    { return m_storage.dense_at(denseIndex); }

    const T& dense_at(const size_t denseIndex) const
    { return m_storage.dense_at(denseIndex); }

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
    { return m_storage.dense(); }

    const ArrayList<T>& dense() const
    { return m_storage.dense(); }

    size_t dense_index_of(const size_t entityIndex) const
    { return m_storage.dense_index_of(entityIndex); }

    void swapBuffers() override
    { m_storage.swapBuffers(); }

    void writeFrom(IComponentPool&) override
    { throw std::runtime_error("Simulation component pools cannot write from another pool"); }

protected:
    SparseSet<T>& sourceSet()
    { return m_storage.set(); }

    const SparseSet<T>& sourceSet() const
    { return m_storage.set(); }

private:
    void noteEntityMutation(const size_t entityIndex, const bool structural)
    {
        if (structural)
            this->bumpGeneration();

        this->markEntityDirty(entityIndex);
    }
};
