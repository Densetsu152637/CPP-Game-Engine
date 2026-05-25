//
// Created by Nicholas on 06/05/26.
//

#pragma once

#include <type_traits>

#include "../structs/sparse_set.h"
#include "structs/templates.h"

class IComponentPool
{
public:
    virtual ~IComponentPool() = default;
    virtual void erase(size_t entityIndex) = 0;
    virtual void clear() = 0;
    virtual size_t size() const = 0;
    virtual void swapBuffers() = 0;
};

template <typename T>
class ComponentPool final : public IComponentPool
{

    SparseSet<Pair<T>> m_storage;

public:

    size_t read = 0;
    size_t write = 1;

    ComponentPool() = default;

    size_t size() const override
    { return m_storage.size(); }

    bool contains(const size_t entityIndex) const
    { return m_storage.contains(entityIndex); }

    Pair<T>* try_get(const size_t entityIndex)
    { return m_storage.try_get(entityIndex); }

    const Pair<T>* try_get(const size_t entityIndex) const
    { return m_storage.try_get(entityIndex); }

    T* try_read(const size_t entityIndex)
    {
        Pair<T>* pair = this->try_get(entityIndex);
        if (nullptr == pair)
            return nullptr;

        return &(pair->buff[read]);
    }

    const T* try_read(const size_t entityIndex) const
    {
        const Pair<T>* pair = this->try_get(entityIndex);
        if (nullptr == pair)
            return nullptr;

        return &(pair->buff[read]);
    }

    T* try_write(const size_t entityIndex)
    {
        Pair<T>* pair = this->try_get(entityIndex);
        if (nullptr == pair)
            return nullptr;

        return &(pair->buff[write]);
    }

    const T* try_write(const size_t entityIndex) const
    {
        const Pair<T>* pair = this->try_get(entityIndex);
        if (nullptr == pair)
            return nullptr;

        return &(pair->buff[write]);
    }

    Pair<T>& at(const size_t entityIndex)
    { return m_storage.at(entityIndex); }

    const Pair<T>& at(const size_t entityIndex) const
    { return m_storage.at(entityIndex); }

    template <typename... Args>
    Pair<T>& emplace(const size_t entityIndex, Args&&... args)
    { return m_storage.emplace(entityIndex, std::forward<Args>(args)...); }

    Pair<T>& insert_or_assign(const size_t entityIndex, T value)
    { return m_storage.insert_or_assign(entityIndex, Pair { value } ); }

    void erase(const size_t entityIndex) override
    { m_storage.erase(entityIndex); }

    void clear() override
    { m_storage.clear(); }

    size_t entity_at(const size_t denseIndex) const
    { return m_storage.key_at(denseIndex); }

    size_t read_index() const
    { return read; }

    size_t write_index() const
    { return write; }

    void swapBuffers() override
    { std::swap(read, write); }

};
