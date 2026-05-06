//
// Created by Nicholas on 06/05/26.
//

#pragma once

#include <type_traits>

#include "../structs/sparse_set.h"

class IComponentPool
{
public:
    virtual ~IComponentPool() = default;
    virtual void erase(std::size_t entityIndex) = 0;
    virtual void clear() = 0;
    virtual size_t size() const = 0;
};

template <typename T>
class ComponentPool final : public IComponentPool
{
    SparseSet<T> m_storage;

public:

    size_t size() const override
    { return m_storage.size(); }

    bool contains(const size_t entityIndex) const
    { return m_storage.contains(entityIndex); }

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
    { return m_storage.emplace(entityIndex, std::forward<Args>(args)...); }

    T& insert_or_assign(const size_t entityIndex, T value)
    { return m_storage.insert_or_assign(entityIndex, std::move(value)); }

    void erase(const size_t entityIndex) override
    { m_storage.erase(entityIndex); }

    void clear() override
    { m_storage.clear(); }

    size_t entity_at(const size_t denseIndex) const
    { return m_storage.key_at(denseIndex); }
};
