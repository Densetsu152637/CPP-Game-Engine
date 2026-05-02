//
// Created by Nicholas on 26/04/26.
//

#pragma once

#include <any>
#include <mutex>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>

#include "entity.h"
#include "structs/sparse_set.h"

class ECS {

    using ComponentBucket = SparseSet<std::any>;
    std::unordered_map<std::type_index, ComponentBucket> m_components;
    ArrayList<Entity> m_entities;
    std::mutex creationLock;
    size_t m_nextEntityId = 0;

    size_t _next_entity_id();

public:

    ECS() = default;
    ECS(const ECS&) = delete;
    ECS(ECS&&) = delete;
    ECS& operator=(const ECS&) = delete;
    ECS& operator=(ECS&&) = delete;

    size_t createEntity();
    void destroyEntity(size_t id);
    bool hasEntity(size_t id) const;
    Entity& getEntity(size_t id);

    void clear();

    template <typename T>
    bool hasComponent(const size_t& id) const;

    template <typename T>
    T& getComponent(const size_t& id);

    template <typename T, typename... Args>
    T& emplaceComponent(const size_t& id, Args&&... args);

    template <typename T>
    T& setComponent(const size_t& id, T&& newComponent);

    template <typename T>
    void removeComponent(const size_t& id);
};

template <typename T>
bool ECS::hasComponent(const size_t& id) const
{
    const Entity& e = m_entities.at(id);
    if (!e.valid()) return false;
    return e.flags.contains(typeid(T));
}

template <typename T>
T& ECS::getComponent(const size_t& id)
{
    ComponentBucket& bucket = m_components[typeid(T)];
    return std::any_cast<T&>(bucket.at(id));
}

template <typename T, typename... Args>
T& ECS::emplaceComponent(const size_t& id, Args&&... args)
{
    m_entities[id].flags.append(typeid(T));
    ComponentBucket& bucket = m_components[typeid(T)];
    return std::any_cast<T&>(bucket.emplace(id, std::forward<Args>(args)...));
}

template <typename T>
T& ECS::setComponent(const size_t& id, T&& newComponent)
{
    ComponentBucket& bucket = m_components[typeid(T)];
    T& component = std::any_cast<T&>(bucket[id]);
    component = newComponent;
    return component;
}

template <typename T>
void ECS::removeComponent(const size_t& id)
{
    m_entities[id].flags.remove(typeid(T));
    ComponentBucket& bucket = m_components[typeid(T)];
    bucket.pop(id);
}
