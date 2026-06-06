//
// Created by Nicholas on 26/04/26.
//

#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>

#include "component_alias.h"
#include "component_pool.h"
#include "entity.h"
#include "../structs/arraylist.h"

template <typename... Components>
class View;

class ECS
{
    template <typename...>
    friend class View;
    friend class ECSProcessor;

    std::unordered_map<
        std::type_index,
        std::unique_ptr<IComponentPool>
    > m_componentPools;

    std::unordered_map<
        std::type_index,
        std::unique_ptr<IComponentPool>
    > m_renderComponentPools;

    ArrayList<EntityRecord> m_entities;
    ArrayList<size_t> m_freeList;

    static Entity make_handle(const EntityRecord& record, const size_t& index);
    Entity make_handle(const size_t& index);
    bool is_alive_index(size_t index) const;
    bool is_valid_handle(const Entity& entity) const;

    template <typename T>
    using component_key_t = ecs::component_key_t<T>;

    template <typename T>
    using component_value_t = ecs::component_value_t<T>;

    template <typename T>
    using pool_t = std::conditional_t<
        ecs::is_buffered_component_v<component_value_t<T>>,
        BufferedComponentPool<component_value_t<T>>,
        ComponentPool<component_value_t<T>>
    >;

    template <typename T>
    using render_pool_t = RenderComponentPool<component_value_t<T>>;

    template <typename T>
    pool_t<T>* storage_if_exists()
    {
        const auto it = m_componentPools.find(std::type_index(typeid(component_key_t<T>)));
        if (it == m_componentPools.end())
            return nullptr;

        return static_cast<pool_t<T>*>(it->second.get());
    }

    template <typename T>
    const pool_t<T>* storage_if_exists() const
    {
        const auto it = m_componentPools.find(std::type_index(typeid(component_key_t<T>)));
        if (it == m_componentPools.end())
            return nullptr;

        return static_cast<const pool_t<T>*>(it->second.get());
    }

    template <typename T>
    pool_t<T>& storage()
    {
        const std::type_index key(typeid(component_key_t<T>));
        auto it = m_componentPools.find(key);
        if (it == m_componentPools.end())
        {
            auto inserted = m_componentPools.emplace(key, std::make_unique<pool_t<T>>());
            it = inserted.first;
        }

        return *static_cast<pool_t<T>*>(it->second.get());
    }

    template <typename T>
    render_pool_t<T>* render_storage_if_exists()
    {
        const auto it = m_renderComponentPools.find(std::type_index(typeid(component_key_t<T>)));
        if (it == m_renderComponentPools.end())
            return nullptr;

        return static_cast<render_pool_t<T>*>(it->second.get());
    }

    template <typename T>
    const render_pool_t<T>* render_storage_if_exists() const
    {
        const auto it = m_renderComponentPools.find(std::type_index(typeid(component_key_t<T>)));
        if (it == m_renderComponentPools.end())
            return nullptr;

        return static_cast<const render_pool_t<T>*>(it->second.get());
    }

    template <typename T>
    render_pool_t<T>& render_storage()
    {
        const std::type_index key(typeid(component_key_t<T>));
        auto it = m_renderComponentPools.find(key);
        if (it == m_renderComponentPools.end())
        {
            auto inserted = m_renderComponentPools.emplace(key, std::make_unique<render_pool_t<T>>());
            it = inserted.first;
        }

        return *static_cast<render_pool_t<T>*>(it->second.get());
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
    void swapSimBuffers();
    void swapRenderBuffers();
    void markComponentDirty(size_t componentTypeId);

    template <typename T>
    void guarantee_component_pool()
    {
        (void)storage<T>();
    }

    template <typename T>
    void guarantee_render_component_pool()
    {
        (void)render_storage<T>();
    }

    template <typename T>
    bool hasComponent(const Entity& entity) const;

    template <typename T>
    ecs::component_value_t<T>* try_get_index(const size_t& index);

    template <typename T>
    const ecs::component_value_t<T>* try_get_index(const size_t& index) const;

    template <typename T>
    ecs::component_value_t<T>* try_get(const Entity& entity);

    template <typename T>
    const ecs::component_value_t<T>* try_get(const Entity& entity) const;

    template <typename T, typename... Args>
    ecs::component_value_t<T>& emplaceComponent(const Entity& entity, Args&&... args);

    template <typename T, typename U>
    ecs::component_value_t<T>& setComponent(const Entity& entity, U&& newComponent);

    template <typename T>
    bool removeComponent(const Entity& entity);

    template <typename T>
    ArrayList<ecs::component_value_t<T>>& denseComponents();

    template <typename T>
    const ArrayList<ecs::component_value_t<T>>& denseComponents() const;

    template <typename... Components>
    View<Components...> view();

    template <typename... Components>
    View<Components...> render_view();

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

    const pool_t<T>* pool = storage_if_exists<T>();
    return nullptr != pool && pool->contains(entity.index);
}

template <typename T>
ecs::component_value_t<T>* ECS::try_get_index(const size_t& index)
{
    pool_t<T>* pool = storage_if_exists<T>();
    if (nullptr == pool) return nullptr;
    return pool->try_get(index);
}

template <typename T>
const ecs::component_value_t<T>* ECS::try_get_index(const size_t& index) const
{
    const pool_t<T>* pool = storage_if_exists<T>();
    if (nullptr == pool) return nullptr;
    return pool->try_get(index);
}

template <typename T>
ecs::component_value_t<T>* ECS::try_get(const Entity& entity)
{
    if (!is_valid_handle(entity))
        return nullptr;

    return this->try_get_index<T>(entity.index);
}

template <typename T>
const ecs::component_value_t<T>* ECS::try_get(const Entity& entity) const
{
    if (!is_valid_handle(entity))
        return nullptr;

    return this->try_get_index<T>(entity.index);
}

template <typename T, typename... Args>
ecs::component_value_t<T>& ECS::emplaceComponent(const Entity& entity, Args&&... args)
{
    if (!is_valid_handle(entity))
        throw std::out_of_range("Entity is not valid");

    return storage<T>().emplace(entity.index, std::forward<Args>(args)...);
}

template <typename T, typename U>
ecs::component_value_t<T>& ECS::setComponent(const Entity& entity, U&& newComponent)
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

    pool_t<T>* pool = storage_if_exists<T>();
    if (nullptr == pool || !pool->contains(entity.index))
        return false;

    pool->erase(entity.index);
    return true;
}

template <typename T>
ArrayList<ecs::component_value_t<T>>& ECS::denseComponents()
{
    return storage<T>().dense();
}

template <typename T>
const ArrayList<ecs::component_value_t<T>>& ECS::denseComponents() const
{
    const pool_t<T>* pool = storage_if_exists<T>();
    if (nullptr == pool)
        throw std::out_of_range("Component pool does not exist");

    return pool->dense();
}

template <typename... Components>
View<Components...> ECS::view()
{ return View<Components...>(*this); }

template <typename... Components>
View<Components...> ECS::render_view()
{ return View<Components...>(*this, ViewStorage::Rendering); }

template <typename Func>
void ECS::eachEntity(Func&& func)
{
    auto view = this->view<>();
    view.each([&func](const Entity& entity)
    {
        func(entity);
    });
}
