//
// Created by Nicholas on 26/04/26.
//

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

#include "aliases/component_alias.h"
#include "core/component_storage_registry.h"
#include "core/entity.h"
#include "core/entity_registry.h"
#include "core/structural_command_buffer.h"
#include "../structs/arraylist.h"

template <typename... Components>
class View;
class ECSRenderBridge;

class ECS
{
    template <typename...>
    friend class View;
    friend class ECSProcessor;
    friend class ECSRenderBridge;

    EntityRegistry m_entities;
    ComponentStorageRegistry m_components;
    StructuralCommandBuffer m_deferredStructuralCommands;
    mutable std::mutex m_structuralMutex;
    size_t m_structuralDeferralDepth = 0;
    size_t m_nextDeferredEntityIndex = 0;

    static Entity make_handle(const EntityRecord& record, const size_t& index);
    Entity make_handle(const size_t& index);
    bool is_alive_index(size_t index) const;
    bool is_known_handle(const Entity& entity) const;
    bool is_valid_handle(const Entity& entity) const;

    template <typename T>
    using component_key_t = ecs::component_key_t<T>;

    template <typename T>
    using component_value_t = ecs::component_value_t<T>;

    template <typename T>
    using pool_t = ComponentStorageRegistry::pool_t<T>;

    template <typename T>
    using render_pool_t = ComponentStorageRegistry::render_pool_t<T>;

    template <typename T>
    pool_t<T>* storage_if_exists()
    {
        return m_components.storageIfExists<T>();
    }

    template <typename T>
    const pool_t<T>* storage_if_exists() const
    {
        return m_components.storageIfExists<T>();
    }

    template <typename T>
    pool_t<T>& storage()
    {
        return m_components.storage<T>();
    }

    template <typename T>
    render_pool_t<T>* render_storage_if_exists()
    {
        return m_components.renderStorageIfExists<T>();
    }

    template <typename T>
    const render_pool_t<T>* render_storage_if_exists() const
    {
        return m_components.renderStorageIfExists<T>();
    }

    template <typename T>
    render_pool_t<T>& render_storage()
    {
        return m_components.renderStorage<T>();
    }

    size_t alive_entity_count() const;
    const ArrayList<EntityRecord>& entity_records() const;
    bool structural_changes_deferred() const;
    Entity createEntityImmediate();
    Entity reserveEntityForDeferredCreate();
    bool activateDeferredEntity(const Entity& entity);
    void destroyEntityImmediate(const Entity& entity);
    void clearImmediate();

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
    void markComponentDirty(ecs::ComponentTypeId componentTypeId);
    void markComponentEntityDirty(ecs::ComponentTypeId componentTypeId, size_t entityIndex);
    void beginStructuralDeferral();
    void endStructuralDeferral();
    void flushDeferredStructuralChanges();
    void discardDeferredStructuralChanges();

    size_t entity_generation() const
    { return m_entities.generation(); }

    size_t component_storage_generation() const
    { return m_components.componentStorageGeneration(); }

    size_t render_storage_generation() const
    { return m_components.renderStorageGeneration(); }

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
    void markComponentDirty()
    {
        markComponentDirty(ecs::component_type_id<component_key_t<T>>());
    }

    template <typename T>
    void markComponentEntityDirty(const size_t entityIndex)
    {
        markComponentEntityDirty(ecs::component_type_id<component_key_t<T>>(), entityIndex);
    }

    template <typename T>
    bool hasComponent(const Entity& entity) const;

    template <typename T>
    const ecs::component_value_t<T>* try_get_index(const size_t& index);

    template <typename T>
    const ecs::component_value_t<T>* try_get_index(const size_t& index) const;

    template <typename T>
    ecs::component_value_t<T>* try_get_mut_index(const size_t& index);

    template <typename T>
    const ecs::component_value_t<T>* try_get(const Entity& entity);

    template <typename T>
    const ecs::component_value_t<T>* try_get(const Entity& entity) const;

    template <typename T>
    ecs::component_value_t<T>* try_get_mut(const Entity& entity);

    template <typename T, typename... Args>
    ecs::component_value_t<T>& emplaceComponent(const Entity& entity, Args&&... args);

    template <typename T, typename U>
    ecs::component_value_t<T>& emplaceComponentImmediate(const Entity& entity, U&& component);

    template <typename T, typename U>
    ecs::component_value_t<T>& setComponent(const Entity& entity, U&& newComponent);

    template <typename T, typename U>
    ecs::component_value_t<T>& setComponentImmediate(const Entity& entity, U&& newComponent);

    template <typename T>
    bool removeComponent(const Entity& entity);

    template <typename T>
    bool removeComponentImmediate(const Entity& entity);

    template <typename T>
    ArrayList<ecs::component_value_t<T>>& denseComponents();

    template <typename T>
    ArrayList<ecs::component_value_t<T>>& denseComponentsMut();

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

#include "views/view.h"

template <typename T>
bool ECS::hasComponent(const Entity& entity) const
{
    if (!is_valid_handle(entity))
        return false;

    const pool_t<T>* pool = storage_if_exists<T>();
    return nullptr != pool && pool->contains(entity.index);
}

template <typename T>
const ecs::component_value_t<T>* ECS::try_get_index(const size_t& index)
{
    const pool_t<T>* pool = storage_if_exists<T>();
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
ecs::component_value_t<T>* ECS::try_get_mut_index(const size_t& index)
{
    pool_t<T>* pool = storage_if_exists<T>();
    if (nullptr == pool) return nullptr;

    ecs::component_value_t<T>* component = pool->try_get(index);
    if (nullptr != component)
        pool->markEntityDirty(index);

    return component;
}

template <typename T>
const ecs::component_value_t<T>* ECS::try_get(const Entity& entity)
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

template <typename T>
ecs::component_value_t<T>* ECS::try_get_mut(const Entity& entity)
{
    if (!is_valid_handle(entity))
        return nullptr;

    return this->try_get_mut_index<T>(entity.index);
}

template <typename T, typename... Args>
ecs::component_value_t<T>& ECS::emplaceComponent(const Entity& entity, Args&&... args)
{
    using Component = ecs::component_value_t<T>;
    auto staged = std::make_shared<Component>(std::forward<Args>(args)...);

    if (structural_changes_deferred())
    {
        if (!is_known_handle(entity))
            throw std::out_of_range("Entity is not valid");

        m_deferredStructuralCommands.enqueue([this, entity, staged]()
        {
            if (is_valid_handle(entity))
                emplaceComponentImmediate<T>(entity, std::move(*staged));
        });
        return *staged;
    }

    return emplaceComponentImmediate<T>(entity, std::move(*staged));
}

template <typename T, typename U>
ecs::component_value_t<T>& ECS::emplaceComponentImmediate(const Entity& entity, U&& component)
{
    if (!is_valid_handle(entity))
        throw std::out_of_range("Entity is not valid");

    return storage<T>().emplace(entity.index, std::forward<U>(component));
}

template <typename T, typename U>
ecs::component_value_t<T>& ECS::setComponent(const Entity& entity, U&& newComponent)
{
    using Component = ecs::component_value_t<T>;
    auto staged = std::make_shared<Component>(std::forward<U>(newComponent));

    if (structural_changes_deferred())
    {
        if (!is_known_handle(entity))
            throw std::out_of_range("Entity is not valid");

        m_deferredStructuralCommands.enqueue([this, entity, staged]()
        {
            if (is_valid_handle(entity))
                setComponentImmediate<T>(entity, std::move(*staged));
        });
        return *staged;
    }

    return setComponentImmediate<T>(entity, std::move(*staged));
}

template <typename T, typename U>
ecs::component_value_t<T>& ECS::setComponentImmediate(const Entity& entity, U&& newComponent)
{
    if (!is_valid_handle(entity))
        throw std::out_of_range("Entity is not valid");

    return storage<T>().emplace(entity.index, std::forward<U>(newComponent));
}

template <typename T>
bool ECS::removeComponent(const Entity& entity)
{
    if (structural_changes_deferred())
    {
        if (!is_known_handle(entity))
            return false;

        m_deferredStructuralCommands.enqueue([this, entity]()
        {
            removeComponentImmediate<T>(entity);
        });
        return true;
    }

    return removeComponentImmediate<T>(entity);
}

template <typename T>
bool ECS::removeComponentImmediate(const Entity& entity)
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
    return denseComponentsMut<T>();
}

template <typename T>
ArrayList<ecs::component_value_t<T>>& ECS::denseComponentsMut()
{
    pool_t<T>& pool = storage<T>();
    pool.markDirty();
    return pool.dense();
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
