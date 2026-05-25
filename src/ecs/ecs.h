//
// Created by Nicholas on 26/04/26.
//

#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <utility>

#include "component_pool.h"
#include "entity.h"
#include "../structs/arraylist.h"

template <typename... Components>
class View;

class ECS
{
    template <typename...>
    friend class View;

    std::unordered_map<
        std::type_index,
        std::unique_ptr<IComponentPool>
    > m_componentPools;

    ArrayList<EntityRecord> m_entities;
    ArrayList<size_t> m_freeList;

    static Entity make_handle(const EntityRecord& record, const size_t& index);
    Entity make_handle(const size_t& index);
    bool is_alive_index(size_t index) const;
    bool is_valid_handle(const Entity& entity) const;

    template <typename T>
    ComponentPool<T>* storage_if_exists()
    {
        const auto it = m_componentPools.find(std::type_index(typeid(T)));
        if (it == m_componentPools.end())
            return nullptr;

        return static_cast<ComponentPool<T>*>(it->second.get());
    }

    template <typename T>
    const ComponentPool<T>* storage_if_exists() const
    {
        const auto it = m_componentPools.find(std::type_index(typeid(T)));
        if (it == m_componentPools.end())
        {
            return nullptr;
        }

        return static_cast<const ComponentPool<T>*>(it->second.get());
    }

    template <typename T>
    ComponentPool<T>& storage()
    {
        const std::type_index key(typeid(T));
        auto it = m_componentPools.find(key);
        if (it == m_componentPools.end())
        {
            auto inserted = m_componentPools.emplace(key, std::make_unique<ComponentPool<T>>());
            it = inserted.first;
        }

        return *static_cast<ComponentPool<T>*>(it->second.get());
    }

public:

    ECS() = default;
    ECS(const ECS&) = delete;
    ECS(ECS&&) = delete;
    ECS& operator=(const ECS&) = delete;
    ECS& operator=(ECS&&) = delete;

    Entity createEntity();
    void destroyEntity(const Entity& entity);
    bool hasEntity(const Entity& entity) const;
    void clear();
    void swapBuffers();

    template <typename T>
    bool hasComponent(const Entity& entity) const;

    template <typename T>
    Pair<T>* try_get_index(const size_t& index);

    template <typename T>
    const Pair<T>* try_get_index(const size_t& index) const;

    template <typename T>
    Pair<T>* try_get(const Entity& entity);

    template <typename T>
    const Pair<T>* try_get(const Entity& entity) const;

    template <typename T>
    T* try_read(const Entity& entity);

    template <typename T>
    const T* try_read(const Entity& entity) const;

    template <typename T>
    T* try_write(const Entity& entity);

    template <typename T, typename... Args>
    Pair<T>& emplaceComponent(const Entity& entity, Args&&... args);

    template <typename T, typename U>
    Pair<T>& setComponent(const Entity& entity, U&& newComponent);

    template <typename T>
    bool removeComponent(const Entity& entity);

    template <typename... Components>
    View<Components...> view();

    template <typename... Components>
    View<Components...> query()
    { return view<Components...>(); }

    template <typename Func>
    void eachEntity(Func&& func);
};

#include "view.h"

template <typename T>
bool ECS::hasComponent(const Entity& entity) const
{
    if (!is_valid_handle(entity))
        return false;

    const ComponentPool<T>* pool = storage_if_exists<T>();
    return nullptr != pool && pool->contains(entity.index);
}

template <typename T>
Pair<T>* ECS::try_get_index(const size_t& index)
{
    ComponentPool<T>* pool = storage_if_exists<T>();
    if (nullptr == pool) return nullptr;
    return pool->try_get(index);
}

template <typename T>
const Pair<T>* ECS::try_get_index(const size_t& index) const
{
    const ComponentPool<T>* pool = storage_if_exists<T>();
    if (nullptr == pool) return nullptr;
    return pool->try_get(index);
}

template <typename T>
Pair<T>* ECS::try_get(const Entity& entity)
{
    if (!is_valid_handle(entity))
        return nullptr;

    return this->try_get_index<T>(entity.index);
}

template <typename T>
const Pair<T>* ECS::try_get(const Entity& entity) const
{
    if (!is_valid_handle(entity))
        return nullptr;

    return this->try_get_index<T>(entity.index);
}

template <typename T>
T* ECS::try_read(const Entity& entity)
{
    if (!is_valid_handle(entity))
        return nullptr;

    ComponentPool<T>* pool = storage_if_exists<T>();
    if (nullptr == pool)
        return nullptr;

    return pool->try_read(entity.index);
}

template <typename T>
const T* ECS::try_read(const Entity& entity) const
{
    if (!is_valid_handle(entity))
        return nullptr;

    const ComponentPool<T>* pool = storage_if_exists<T>();
    if (nullptr == pool)
        return nullptr;

    return pool->try_read(entity.index);
}

template <typename T>
T* ECS::try_write(const Entity& entity)
{
    if (!is_valid_handle(entity))
        return nullptr;

    ComponentPool<T>* pool = storage_if_exists<T>();
    if (nullptr == pool)
        return nullptr;

    return pool->try_write(entity.index);
}

template <typename T, typename... Args>
Pair<T>& ECS::emplaceComponent(const Entity& entity, Args&&... args)
{
    if (!is_valid_handle(entity))
        throw std::out_of_range("Entity is not valid");

    return storage<T>().emplace(entity.index, std::forward<Args>(args)...);
}

template <typename T, typename U>
Pair<T>& ECS::setComponent(const Entity& entity, U&& newComponent)
{
    if (!is_valid_handle(entity))
        throw std::out_of_range("Entity is not valid");

    return storage<T>().emplace(entity.index, std::forward<U>(newComponent));
}

template <typename T>
bool ECS::removeComponent(const Entity& entity)
{
    if (!is_valid_handle(entity))
        return false;

    ComponentPool<T>* pool = storage_if_exists<T>();
    if (nullptr == pool || !pool->contains(entity.index))
        return false;

    pool->erase(entity.index);
    return true;
}

template <typename... Components>
View<Components...> ECS::view()
{ return View<Components...>(*this); }

template <typename Func>
void ECS::eachEntity(Func&& func)
{
    auto view = this->view<>();
    view.each([&func](const Entity& entity)
    {
        func(entity);
    });
}
