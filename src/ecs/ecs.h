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

    struct EntityRecord
    {
        uint32_t version = 1;
        bool alive = false;
    };

    std::unordered_map<
        std::type_index,
        std::unique_ptr<IComponentPool>
    > m_componentPools;

    ArrayList<EntityRecord> m_entities;
    ArrayList<size_t> m_freeList;

    Entity make_handle(size_t index) const;
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

    template <typename T>
    T* try_get_by_index(size_t entityIndex)
    {
        ComponentPool<T>* pool = storage_if_exists<T>();
        return nullptr == pool ? nullptr : pool->try_get(entityIndex);
    }

    template <typename T>
    const T* try_get_by_index(size_t entityIndex) const
    {
        const ComponentPool<T>* pool = storage_if_exists<T>();
        return nullptr == pool ? nullptr : pool->try_get(entityIndex);
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

    template <typename T>
    bool hasComponent(const Entity& entity) const;

    template <typename T>
    T* try_get(const Entity& entity);

    template <typename T>
    const T* try_get(const Entity& entity) const;

    template <typename T>
    T& getComponent(const Entity& entity);

    template <typename T>
    const T& getComponent(const Entity& entity) const;

    template <typename T, typename... Args>
    T& emplaceComponent(const Entity& entity, Args&&... args);

    template <typename T, typename U>
    T& setComponent(const Entity& entity, U&& newComponent);

    template <typename T>
    void removeComponent(const Entity& entity);

    template <typename... Components>
    View<Components...> view();

    template <typename... Components>
    View<Components...> query()
    { return view<Components...>(); }
};

template <typename T>
bool ECS::hasComponent(const Entity& entity) const
{
    if (!is_valid_handle(entity))
        return false;

    const ComponentPool<T>* pool = storage_if_exists<T>();
    return nullptr != pool && pool->contains(entity.index);
}

template <typename T>
T* ECS::try_get(const Entity& entity)
{
    if (!is_valid_handle(entity))
        return nullptr;

    return try_get_by_index<T>(entity.index);
}

template <typename T>
const T* ECS::try_get(const Entity& entity) const
{
    if (!is_valid_handle(entity))
        return nullptr;

    return try_get_by_index<T>(entity.index);
}

template <typename T>
T& ECS::getComponent(const Entity& entity)
{
    T* component = try_get<T>(entity);
    if (nullptr == component)
        throw std::out_of_range("Component not found for entity");


    return *component;
}

template <typename T>
const T& ECS::getComponent(const Entity& entity) const
{
    const T* component = try_get<T>(entity);
    if (nullptr == component)
        throw std::out_of_range("Component not found for entity");

    return *component;
}

template <typename T, typename... Args>
T& ECS::emplaceComponent(const Entity& entity, Args&&... args)
{
    if (!is_valid_handle(entity))
        throw std::out_of_range("Entity is not valid");

    return storage<T>().emplace(entity.index, std::forward<Args>(args)...);
}

template <typename T, typename U>
T& ECS::setComponent(const Entity& entity, U&& newComponent)
{
    if (!is_valid_handle(entity))
        throw std::out_of_range("Entity is not valid");

    ComponentPool<T>& pool = storage<T>();
    if (T* component = pool.try_get(entity.index))
    {
        *component = std::forward<U>(newComponent);
        return *component;
    }

    return pool.emplace(entity.index, std::forward<U>(newComponent));
}

template <typename T>
void ECS::removeComponent(const Entity& entity)
{
    if (!is_valid_handle(entity))
        return;

    if (ComponentPool<T>* pool = storage_if_exists<T>())
        pool->erase(entity.index);
}

template <typename... Components>
View<Components...> ECS::view()
{
    return View<Components...>(*this);
}

#include "view.h"


