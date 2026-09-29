//
// Created by Nicholas on 26/04/26.
//

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "aliases/component_alias.h"
#include "core/component_storage_registry.h"
#include "core/dynamic_component_storage.h"
#include "core/entity.h"
#include "core/entity_registry.h"
#include "core/structural_command_buffer.h"
#include "pools/tag_pool.h"
#include "views/view_cache.h"
#include "../structs/arraylist.h"
#include "../structs/templates.h"

class ECS;

template <typename... Components>
class View;

template <typename... Filters>
class FilteredView;

class ECSRenderBridge;

namespace ecs
{
    namespace query_detail
    {
        enum class EntityFilterMode
        {
            Include,
            Exclude
        };

        struct EntityFilter
        {
            EntityFilterMode mode = EntityFilterMode::Include;
            size_t size = 0;
            void (*appendEntities)(const ECS&, ArrayList<Entity>&) = nullptr;
            bool (*matches)(const ECS&, const Entity&) = nullptr;
        };

        template <typename T>
        struct is_tag : std::false_type
        {};

        template <typename T>
        struct is_tag<Tag<T>> : std::true_type
        {};

        template <typename T>
        inline constexpr bool is_tag_v = is_tag<std::remove_cvref_t<T>>::value;

        template <typename T>
        struct is_exclude : std::false_type
        {};

        template <typename T>
        struct is_exclude<Exclude<T>> : std::true_type
        {};

        template <typename T>
        inline constexpr bool is_exclude_v = is_exclude<std::remove_cvref_t<T>>::value;

        template <typename T>
        struct is_shared : std::false_type
        {};

        template <typename T>
        struct is_shared<Shared<T>> : std::true_type
        {};

        template <typename T>
        inline constexpr bool is_shared_v = is_shared<std::remove_cvref_t<T>>::value;

        template <typename T>
        struct shared_component
        {
            using type = void;
        };

        template <typename T>
        struct shared_component<Shared<T>>
        {
            static_assert(
                is_shared_component_alias_v<std::remove_cvref_t<T>>,
                "ecs::Shared<T> can only wrap ecs::SharedAlias component types"
            );

            using type = std::remove_cvref_t<T>;
        };

        template <typename T>
        using shared_component_t = typename shared_component<std::remove_cvref_t<T>>::type;

        template <typename T>
        struct is_view_of : std::false_type
        {};

        template <typename... Components>
        struct is_view_of<ViewOf<Components...>> : std::true_type
        {};

        template <typename T>
        inline constexpr bool is_view_of_v = is_view_of<std::remove_cvref_t<T>>::value;

        template <typename T>
        struct dirty_component
        {
            using type = void;
        };

        template <typename T>
        struct dirty_component<Dirty<T>>
        {
            using type = std::remove_cvref_t<T>;
        };

        template <typename T>
        using dirty_component_t = typename dirty_component<std::remove_cvref_t<T>>::type;

        template <typename T>
        inline constexpr bool is_dirty_v = !std::is_void_v<dirty_component_t<T>>;

        template <typename T>
        inline constexpr bool is_query_component_v =
            !std::is_same_v<std::remove_cvref_t<T>, Entity> &&
            !is_tag_v<T> &&
            !is_exclude_v<T> &&
            !is_shared_v<T> &&
            !is_view_of_v<T> &&
            !is_dirty_v<T>;
    }
}

class ECS
{
    template <typename...>
    friend class View;
    template <typename...>
    friend class FilteredView;
    friend class ECSProcessor;
    friend class ECSRenderBridge;

    EntityRegistry m_entities;
    ComponentStorageRegistry m_components;
    ecs::DynamicComponentStorage m_dynamicComponents;
    std::unordered_map<ecs::ComponentTypeId, TagPool> m_tags;
    size_t m_componentQueryGeneration = 0;
    bool m_applyingDeferredStructural = false;
    bool m_deferredComponentQueryDirty = false;
    size_t m_renderQueryGeneration = 0;
    size_t m_tagGeneration = 0;
    StructuralCommandBuffer m_deferredStructuralCommands;
    mutable std::mutex m_structuralMutex;
    size_t m_structuralDeferralDepth = 0;
    size_t m_nextDeferredEntityIndex = 0;
    bool m_validatingDeferredStructural = false;
    std::vector<Entity> m_deferredReservedEntities;

    static Entity make_handle(const EntityRecord& record, const size_t& index);
    Entity make_handle(const size_t& index) const;
    bool is_alive_index(size_t index) const;
    bool is_known_handle(const Entity& entity) const;
    bool is_valid_handle(const Entity& entity) const;

    template <typename T>
    using component_key_t = ecs::component_key_t<T>;

    template <typename T>
    using component_value_t = ecs::component_value_t<T>;

    template <typename T>
    using tag_name_t = ecs::tag_name_t<T>;

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

    template <typename T>
    ecs::IArchetypePool* mutable_render_archetype_pool_for_component()
    {
        return m_components.template renderArchetypePoolForComponent<T>();
    }

    size_t alive_entity_count() const;
    const ArrayList<EntityRecord>& entity_records() const;
    bool structural_changes_deferred() const;
    void bumpComponentQueryGeneration()
    {
        if (m_applyingDeferredStructural) m_deferredComponentQueryDirty = true;
        else ++m_componentQueryGeneration;
    }
    Entity createEntityImmediate();
    Entity reserveEntityForDeferredCreate();
    bool activateDeferredEntity(const Entity& entity);
    void destroyEntityImmediate(const Entity& entity);
    void clearImmediate();
    void remove_tags_for_entity(size_t entityIndex);

    template <typename T>
    static ecs::ComponentTypeId tag_type_id()
    {
        return ecs::component_type_id<ecs::Tag<tag_name_t<T>>>();
    }

    template <typename T>
    TagPool* tag_pool_if_exists()
    {
        const auto it = m_tags.find(tag_type_id<T>());
        if (it == m_tags.end())
            return nullptr;

        return &it->second;
    }

    template <typename T>
    const TagPool* tag_pool_if_exists() const
    {
        const auto it = m_tags.find(tag_type_id<T>());
        if (it == m_tags.end())
            return nullptr;

        return &it->second;
    }

    template <typename T>
    TagPool& tag_pool()
    { return m_tags[tag_type_id<T>()]; }

    template <typename Component>
    static void append_component_entities_for_filter(const ECS& ecs, ArrayList<Entity>& entities)
    { ecs.template appendComponentEntities<Component>(entities); }

    template <typename Component>
    static bool component_matches_filter(const ECS& ecs, const Entity& entity)
    { return ecs.template hasComponent<Component>(entity); }

    template <typename Component>
    static void append_render_component_entities_for_filter(const ECS& ecs, ArrayList<Entity>& entities)
    { ecs.template appendRenderComponentEntities<Component>(entities); }

    template <typename Component>
    static bool render_component_matches_filter(const ECS& ecs, const Entity& entity)
    { return ecs.template hasRenderComponent<Component>(entity); }

    template <typename Tag>
    static void append_tag_entities_for_filter(const ECS& ecs, ArrayList<Entity>& entities)
    { ecs.template appendTagEntities<Tag>(entities); }

    template <typename Tag>
    static bool tag_matches_filter(const ECS& ecs, const Entity& entity)
    { return ecs.template hasTag<Tag>(entity); }

    template <typename Component>
    size_t componentEntityCount() const;

    template <typename T>
    size_t renderComponentEntityCount() const;

    template <typename T>
    bool hasComponentIndex(size_t entityIndex) const;

    template <typename T>
    bool hasRenderComponent(const Entity& entity) const;

    template <typename T>
    bool hasRenderComponentIndex(size_t entityIndex) const;

    template <typename T>
    void appendComponentEntities(ArrayList<Entity>& entities) const;

    template <typename T>
    void appendRenderComponentEntities(ArrayList<Entity>& entities) const;

    template <typename T>
    size_t tagEntityCount() const;

    template <typename T>
    bool hasTagIndex(size_t entityIndex) const;

    template <typename T>
    void appendTagEntities(ArrayList<Entity>& entities) const;

    void appendAliveEntities(ArrayList<Entity>& entities) const;

    ArrayList<Entity> filteredEntities(const ArrayList<ecs::query_detail::EntityFilter>& filters) const;

    template <typename Component>
    ecs::query_detail::EntityFilter component_filter(ecs::query_detail::EntityFilterMode mode) const;

    template <typename Component>
    ecs::query_detail::EntityFilter render_component_filter(ecs::query_detail::EntityFilterMode mode) const;

    template <typename Tag>
    ecs::query_detail::EntityFilter tag_filter(ecs::query_detail::EntityFilterMode mode) const;

    template <typename Arg>
    void append_query_filter(ArrayList<ecs::query_detail::EntityFilter>& filters, ViewStorage storage) const;

    template <typename Arg>
    void append_exclude_query_filter(ArrayList<ecs::query_detail::EntityFilter>& filters, ViewStorage storage) const;

public:

    ECS() = default;
    ECS(const ECS&) = delete;
    ECS(ECS&&) = delete;
    ECS& operator=(const ECS&) = delete;
    ECS& operator=(ECS&&) = delete;

    Entity createEntity();
    template <typename... Tags>
    Entity createEntityWithTags();
    template <typename... Tags>
    Entity createEntity(ecs::Tag<Tags>...);
    void destroyEntity(const Entity& entity);
    bool hasEntity(const Entity& entity) const;
    bool knowsEntityHandle(const Entity& entity) const { return is_known_handle(entity); }
    bool registerDynamicComponent(std::string_view name, const std::vector<ecs::DynamicField>& fields,
        std::uint32_t version = 1);
    bool hasDynamicComponentSchema(std::string_view name) const;
    bool unregisterDynamicComponent(std::string_view name);
    bool rollbackDynamicComponentSchema(std::string_view name);
    void pruneEmptyDynamicComponentSchemas();
    bool setDynamicComponent(const Entity& entity, std::string_view name, const ecs::DynamicValues& values);
    std::optional<ecs::DynamicValues> getDynamicComponent(const Entity& entity, std::string_view name) const;
    bool removeDynamicComponent(const Entity& entity, std::string_view name);
    std::vector<Entity> queryDynamicComponents(const std::vector<std::string>& all) const;
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

    size_t component_query_generation() const
    { return m_componentQueryGeneration; }

    size_t render_query_generation() const
    { return m_renderQueryGeneration; }

    size_t tag_generation() const
    { return m_tagGeneration; }

    template <typename T>
    void guarantee_component_pool()
    {
        if (!isArchetypedComponent<T>())
            (void)storage<T>();
    }

    template <typename T>
    void guarantee_render_component_pool()
    {
        if (!isRenderArchetypedComponent<T>())
            (void)render_storage<T>();
    }

    template <typename... Components>
    ecs::ArchetypePool<component_value_t<Components>...>& registerArchetype()
    {
        return m_components.template registerArchetype<Components...>();
    }

    template <typename... Components>
    void registerRenderArchetype()
    {
        (void)m_components.template registerRenderArchetype<Components...>();
    }

    template <typename T>
    bool isArchetypedComponent() const
    {
        return m_components.template isArchetypedComponent<T>();
    }

    template <typename T>
    bool isRenderArchetypedComponent() const
    {
        return m_components.template isRenderArchetypedComponent<T>();
    }

    template <typename... Components>
    ecs::ArchetypePool<component_value_t<Components>...>* archetypePoolIfExists()
    {
        return m_components.template archetypePoolIfExists<Components...>();
    }

    template <typename... Components>
    const ecs::ArchetypePool<component_value_t<Components>...>* renderArchetypePoolIfExists()
    {
        return m_components.template renderArchetypePoolIfExists<Components...>();
    }

    template <typename T>
    ecs::IArchetypePool* archetypePoolForComponent()
    {
        return m_components.template archetypePoolForComponent<T>();
    }

    template <typename T>
    const ecs::IArchetypePool* archetypePoolForComponent() const
    {
        return m_components.template archetypePoolForComponent<T>();
    }

    template <typename T>
    const ecs::IArchetypePool* renderArchetypePoolForComponent()
    {
        return m_components.template renderArchetypePoolForComponent<T>();
    }

    template <typename T>
    const ecs::IArchetypePool* renderArchetypePoolForComponent() const
    {
        return m_components.template renderArchetypePoolForComponent<T>();
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
    bool markComponentDirtyIfEntityCountReachesThreshold(const size_t entityCount)
    {
        return m_components.markDirtyIfEntityCountReachesThreshold(
            ecs::component_type_id<component_key_t<T>>(),
            entityCount
        );
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
    bool addTag(const Entity& entity);

    template <typename T>
    bool addTagImmediate(const Entity& entity);

    template <typename T>
    bool removeTag(const Entity& entity);

    template <typename T>
    bool removeTagImmediate(const Entity& entity);

    template <typename T>
    bool hasTag(const Entity& entity) const;

    template <typename T>
    const TagPool* tagPoolIfExists() const
    { return tag_pool_if_exists<T>(); }

    template <typename... Filters>
    ArrayList<Entity> matchingEntities() const;

    template <typename... Filters>
    ArrayList<Entity> renderMatchingEntities() const;

    template <typename T>
    auto denseComponents();

    template <typename T>
    auto denseComponentsMut();

    template <typename T>
    auto denseComponents() const;

    template <typename... Components>
    View<Components...> view();

    template <typename... Components>
    View<Components...> render_view();

    template <typename... Filters>
    FilteredView<Filters...> query();

    template <typename... Filters>
    FilteredView<Filters...> render_query();

    template <typename Func>
    void eachEntity(Func&& func);
};

#include "views/view.h"
#include "views/filtered_view.h"

template <typename... Tags>
Entity ECS::createEntityWithTags()
{
    Entity entity = createEntity();
    (addTag<Tags>(entity), ...);
    return entity;
}

template <typename... Tags>
Entity ECS::createEntity(ecs::Tag<Tags>...)
{
    return createEntityWithTags<Tags...>();
}

template <typename T>
bool ECS::hasComponent(const Entity& entity) const
{
    if (!is_valid_handle(entity))
        return false;

    const ecs::IArchetypePool* archetypePool = archetypePoolForComponent<T>();
    if (nullptr != archetypePool)
    {
        return archetypePool->hasComponent(
            entity.index,
            ecs::component_type_id<component_key_t<T>>()
        );
    }

    const pool_t<T>* pool = storage_if_exists<T>();
    return nullptr != pool && pool->contains(entity.index);
}

template <typename T>
const ecs::component_value_t<T>* ECS::try_get_index(const size_t& index)
{
    const ecs::IArchetypePool* archetypePool = archetypePoolForComponent<T>();
    if (nullptr != archetypePool)
    {
        const void* component = archetypePool->componentPointer(
            index,
            ecs::component_type_id<component_key_t<T>>()
        );
        return static_cast<const ecs::component_value_t<T>*>(component);
    }

    const pool_t<T>* pool = storage_if_exists<T>();
    if (nullptr == pool) return nullptr;
    return pool->try_get(index);
}

template <typename T>
const ecs::component_value_t<T>* ECS::try_get_index(const size_t& index) const
{
    const ecs::IArchetypePool* archetypePool = archetypePoolForComponent<T>();
    if (nullptr != archetypePool)
    {
        const void* component = archetypePool->componentPointer(
            index,
            ecs::component_type_id<component_key_t<T>>()
        );
        return static_cast<const ecs::component_value_t<T>*>(component);
    }

    const pool_t<T>* pool = storage_if_exists<T>();
    if (nullptr == pool) return nullptr;
    return pool->try_get(index);
}

template <typename T>
ecs::component_value_t<T>* ECS::try_get_mut_index(const size_t& index)
{
    ecs::IArchetypePool* archetypePool = archetypePoolForComponent<T>();
    if (nullptr != archetypePool)
    {
        void* component = archetypePool->componentPointer(
            index,
            ecs::component_type_id<component_key_t<T>>()
        );
        if (nullptr != component)
            archetypePool->markComponentEntityDirty(
                ecs::component_type_id<component_key_t<T>>(),
                index
            );
        return static_cast<ecs::component_value_t<T>*>(component);
    }

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

    using Component = ecs::component_value_t<T>;
    const bool addedToQuery = !hasComponent<T>(entity);
    if (ecs::IArchetypePool* archetypePool = archetypePoolForComponent<T>())
    {
        void* componentSlot = archetypePool->ensureComponentPointer(
            entity.index,
            ecs::component_type_id<component_key_t<T>>()
        );
        if (nullptr == componentSlot)
            throw std::runtime_error("Archetype pool did not contain requested component");

        Component& stored = *static_cast<Component*>(componentSlot);
        stored = std::forward<U>(component);
        archetypePool->markComponentEntityDirty(
            ecs::component_type_id<component_key_t<T>>(),
            entity.index
        );
        if (addedToQuery)
            bumpComponentQueryGeneration();
        return stored;
    }

    Component& stored = storage<T>().emplace(entity.index, std::forward<U>(component));
    if (addedToQuery)
        bumpComponentQueryGeneration();
    return stored;
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

    using Component = ecs::component_value_t<T>;
    const bool addedToQuery = !hasComponent<T>(entity);
    if (ecs::IArchetypePool* archetypePool = archetypePoolForComponent<T>())
    {
        void* componentSlot = archetypePool->ensureComponentPointer(
            entity.index,
            ecs::component_type_id<component_key_t<T>>()
        );
        if (nullptr == componentSlot)
            throw std::runtime_error("Archetype pool did not contain requested component");

        Component& stored = *static_cast<Component*>(componentSlot);
        stored = std::forward<U>(newComponent);
        archetypePool->markComponentEntityDirty(
            ecs::component_type_id<component_key_t<T>>(),
            entity.index
        );
        if (addedToQuery)
            bumpComponentQueryGeneration();
        return stored;
    }

    Component& stored = storage<T>().emplace(entity.index, std::forward<U>(newComponent));
    if (addedToQuery)
        bumpComponentQueryGeneration();
    return stored;
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

    if (ecs::IArchetypePool* archetypePool = archetypePoolForComponent<T>())
    {
        const bool removed = archetypePool->removeComponent(
            entity.index,
            ecs::component_type_id<component_key_t<T>>()
        );
        if (removed)
            bumpComponentQueryGeneration();
        return removed;
    }

    pool_t<T>* pool = storage_if_exists<T>();
    if (nullptr == pool || !pool->contains(entity.index))
        return false;

    pool->erase(entity.index);
    bumpComponentQueryGeneration();
    return true;
}

template <typename T>
bool ECS::addTag(const Entity& entity)
{
    if (structural_changes_deferred())
    {
        if (!is_known_handle(entity))
            return false;

        m_deferredStructuralCommands.enqueue([this, entity]()
        {
            addTagImmediate<T>(entity);
        });
        return true;
    }

    return addTagImmediate<T>(entity);
}

template <typename T>
bool ECS::addTagImmediate(const Entity& entity)
{
    if (!is_valid_handle(entity))
        return false;

    const bool changed = tag_pool<T>().add(entity.index);
    if (changed)
        ++m_tagGeneration;
    return changed;
}

template <typename T>
bool ECS::removeTag(const Entity& entity)
{
    if (structural_changes_deferred())
    {
        if (!is_known_handle(entity))
            return false;

        m_deferredStructuralCommands.enqueue([this, entity]()
        {
            removeTagImmediate<T>(entity);
        });
        return true;
    }

    return removeTagImmediate<T>(entity);
}

template <typename T>
bool ECS::removeTagImmediate(const Entity& entity)
{
    if (!is_valid_handle(entity))
        return false;

    TagPool* pool = tag_pool_if_exists<T>();
    const bool changed = nullptr != pool && pool->remove(entity.index);
    if (changed)
        ++m_tagGeneration;
    return changed;
}

template <typename T>
bool ECS::hasTag(const Entity& entity) const
{
    if (!is_valid_handle(entity))
        return false;

    const TagPool* pool = tag_pool_if_exists<T>();
    return nullptr != pool && pool->contains(entity.index);
}

template <typename T>
size_t ECS::componentEntityCount() const
{
    const ecs::IArchetypePool* archetypePool = archetypePoolForComponent<T>();
    if (nullptr != archetypePool)
        return archetypePool->componentSize(ecs::component_type_id<component_key_t<T>>());

    const pool_t<T>* pool = storage_if_exists<T>();
    return nullptr == pool ? 0 : pool->size();
}

template <typename T>
size_t ECS::renderComponentEntityCount() const
{
    const ecs::IArchetypePool* archetypePool = renderArchetypePoolForComponent<T>();
    if (nullptr != archetypePool)
        return archetypePool->componentSize(ecs::component_type_id<component_key_t<T>>());

    const render_pool_t<T>* pool = render_storage_if_exists<T>();
    return nullptr == pool ? 0 : pool->size();
}

template <typename T>
bool ECS::hasComponentIndex(const size_t entityIndex) const
{
    if (!is_alive_index(entityIndex))
        return false;

    const ecs::IArchetypePool* archetypePool = archetypePoolForComponent<T>();
    if (nullptr != archetypePool)
    {
        return archetypePool->hasComponent(
            entityIndex,
            ecs::component_type_id<component_key_t<T>>()
        );
    }

    const pool_t<T>* pool = storage_if_exists<T>();
    return nullptr != pool && pool->contains(entityIndex);
}

template <typename T>
bool ECS::hasRenderComponent(const Entity& entity) const
{
    if (!is_valid_handle(entity))
        return false;

    return hasRenderComponentIndex<T>(entity.index);
}

template <typename T>
bool ECS::hasRenderComponentIndex(const size_t entityIndex) const
{
    if (!is_alive_index(entityIndex))
        return false;

    const ecs::IArchetypePool* archetypePool = renderArchetypePoolForComponent<T>();
    if (nullptr != archetypePool)
    {
        return archetypePool->hasComponent(
            entityIndex,
            ecs::component_type_id<component_key_t<T>>()
        );
    }

    const render_pool_t<T>* pool = render_storage_if_exists<T>();
    return nullptr != pool && pool->contains(entityIndex);
}

template <typename Component>
ecs::query_detail::EntityFilter ECS::component_filter(const ecs::query_detail::EntityFilterMode mode) const
{
    return ecs::query_detail::EntityFilter {
        mode,
        componentEntityCount<Component>(),
        &append_component_entities_for_filter<Component>,
        &component_matches_filter<Component>
    };
}

template <typename Component>
ecs::query_detail::EntityFilter ECS::render_component_filter(const ecs::query_detail::EntityFilterMode mode) const
{
    return ecs::query_detail::EntityFilter {
        mode,
        renderComponentEntityCount<Component>(),
        &append_render_component_entities_for_filter<Component>,
        &render_component_matches_filter<Component>
    };
}

template <typename Tag>
ecs::query_detail::EntityFilter ECS::tag_filter(const ecs::query_detail::EntityFilterMode mode) const
{
    return ecs::query_detail::EntityFilter {
        mode,
        tagEntityCount<Tag>(),
        &append_tag_entities_for_filter<Tag>,
        &tag_matches_filter<Tag>
    };
}

template <typename Arg>
void ECS::append_exclude_query_filter(
    ArrayList<ecs::query_detail::EntityFilter>& filters,
    const ViewStorage storage
) const
{
    using Target = ecs::excluded_filter_t<Arg>;

    if constexpr (std::is_void_v<Target>)
    {
        (void)filters;
        (void)storage;
    }
    else if constexpr (ecs::query_detail::is_tag_v<Target>)
    {
        filters.append(tag_filter<ecs::tag_name_t<Target>>(ecs::query_detail::EntityFilterMode::Exclude));
    }
    else if constexpr (ecs::query_detail::is_shared_v<Target>)
    {
        using Component = ecs::query_detail::shared_component_t<Target>;
        if (ViewStorage::Rendering == storage)
            filters.append(render_component_filter<Component>(ecs::query_detail::EntityFilterMode::Exclude));
        else
            filters.append(component_filter<Component>(ecs::query_detail::EntityFilterMode::Exclude));
    }
    else if constexpr (ecs::query_detail::is_dirty_v<Target>)
    {
        using Component = ecs::query_detail::dirty_component_t<Target>;
        if (ViewStorage::Rendering == storage)
            filters.append(render_component_filter<Component>(ecs::query_detail::EntityFilterMode::Exclude));
        else
            filters.append(component_filter<Component>(ecs::query_detail::EntityFilterMode::Exclude));
    }
    else if constexpr (ecs::query_detail::is_query_component_v<Target>)
    {
        using Component = std::remove_cvref_t<Target>;
        if (ViewStorage::Rendering == storage)
            filters.append(render_component_filter<Component>(ecs::query_detail::EntityFilterMode::Exclude));
        else
            filters.append(component_filter<Component>(ecs::query_detail::EntityFilterMode::Exclude));
    }
}

template <typename Arg>
void ECS::append_query_filter(ArrayList<ecs::query_detail::EntityFilter>& filters, const ViewStorage storage) const
{
    using CleanArg = std::remove_cvref_t<Arg>;

    if constexpr (ecs::query_detail::is_exclude_v<CleanArg>)
    {
        append_exclude_query_filter<CleanArg>(filters, storage);
    }
    else if constexpr (ecs::query_detail::is_tag_v<CleanArg>)
    {
        filters.append(tag_filter<ecs::tag_name_t<CleanArg>>(ecs::query_detail::EntityFilterMode::Include));
    }
    else if constexpr (ecs::query_detail::is_shared_v<CleanArg>)
    {
        using Component = ecs::query_detail::shared_component_t<CleanArg>;
        if (ViewStorage::Rendering == storage)
            filters.append(render_component_filter<Component>(ecs::query_detail::EntityFilterMode::Include));
        else
            filters.append(component_filter<Component>(ecs::query_detail::EntityFilterMode::Include));
    }
    else if constexpr (ecs::query_detail::is_dirty_v<CleanArg>)
    {
        using Component = ecs::query_detail::dirty_component_t<CleanArg>;
        if (ViewStorage::Rendering == storage)
            filters.append(render_component_filter<Component>(ecs::query_detail::EntityFilterMode::Include));
        else
            filters.append(component_filter<Component>(ecs::query_detail::EntityFilterMode::Include));
    }
    else if constexpr (ecs::query_detail::is_query_component_v<CleanArg>)
    {
        using Component = CleanArg;
        if (ViewStorage::Rendering == storage)
            filters.append(render_component_filter<Component>(ecs::query_detail::EntityFilterMode::Include));
        else
            filters.append(component_filter<Component>(ecs::query_detail::EntityFilterMode::Include));
    }
}

template <typename T>
void ECS::appendComponentEntities(ArrayList<Entity>& entities) const
{
    const ecs::IArchetypePool* archetypePool = archetypePoolForComponent<T>();
    if (nullptr != archetypePool)
    {
        const ecs::ComponentTypeId componentTypeId = ecs::component_type_id<component_key_t<T>>();
        const size_t componentCount = archetypePool->componentSize(componentTypeId);
        for (size_t denseIndex = 0; denseIndex < componentCount; ++denseIndex)
        {
            const size_t entityIndex = archetypePool->componentEntityAt(componentTypeId, denseIndex);
            if (is_alive_index(entityIndex))
                entities.append(make_handle(entityIndex));
        }
        return;
    }

    const pool_t<T>* pool = storage_if_exists<T>();
    if (nullptr == pool)
        return;

    for (size_t denseIndex = 0; denseIndex < pool->size(); ++denseIndex)
    {
        const size_t entityIndex = pool->entity_at(denseIndex);
        if (is_alive_index(entityIndex))
            entities.append(make_handle(entityIndex));
    }
}

template <typename T>
void ECS::appendRenderComponentEntities(ArrayList<Entity>& entities) const
{
    const ecs::IArchetypePool* archetypePool = renderArchetypePoolForComponent<T>();
    if (nullptr != archetypePool)
    {
        const ecs::ComponentTypeId componentTypeId = ecs::component_type_id<component_key_t<T>>();
        const size_t componentCount = archetypePool->componentSize(componentTypeId);
        for (size_t denseIndex = 0; denseIndex < componentCount; ++denseIndex)
        {
            const size_t entityIndex = archetypePool->componentEntityAt(componentTypeId, denseIndex);
            if (is_alive_index(entityIndex))
                entities.append(make_handle(entityIndex));
        }
        return;
    }

    const render_pool_t<T>* pool = render_storage_if_exists<T>();
    if (nullptr == pool)
        return;

    for (size_t denseIndex = 0; denseIndex < pool->size(); ++denseIndex)
    {
        const size_t entityIndex = pool->entity_at(denseIndex);
        if (is_alive_index(entityIndex))
            entities.append(make_handle(entityIndex));
    }
}

template <typename T>
size_t ECS::tagEntityCount() const
{
    const TagPool* pool = tag_pool_if_exists<T>();
    return nullptr == pool ? 0 : pool->size();
}

template <typename T>
bool ECS::hasTagIndex(const size_t entityIndex) const
{
    if (!is_alive_index(entityIndex))
        return false;

    const TagPool* pool = tag_pool_if_exists<T>();
    return nullptr != pool && pool->contains(entityIndex);
}

template <typename T>
void ECS::appendTagEntities(ArrayList<Entity>& entities) const
{
    const TagPool* pool = tag_pool_if_exists<T>();
    if (nullptr == pool)
        return;

    const ArrayList<size_t>& entityIndices = pool->entity_indices();
    for (const size_t entityIndex : entityIndices)
    {
        if (is_alive_index(entityIndex))
            entities.append(make_handle(entityIndex));
    }
}

template <typename... Filters>
ArrayList<Entity> ECS::matchingEntities() const
{
    ArrayList<ecs::query_detail::EntityFilter> filters(sizeof...(Filters));
    (append_query_filter<Filters>(filters, ViewStorage::Simulation), ...);
    return filteredEntities(filters);
}

template <typename... Filters>
ArrayList<Entity> ECS::renderMatchingEntities() const
{
    ArrayList<ecs::query_detail::EntityFilter> filters(sizeof...(Filters));
    (append_query_filter<Filters>(filters, ViewStorage::Rendering), ...);
    return filteredEntities(filters);
}

template <typename T>
auto ECS::denseComponents()
{
    return denseComponentsMut<T>();
}

template <typename T>
auto ECS::denseComponentsMut()
{
    if (nullptr != archetypePoolForComponent<T>())
        throw std::runtime_error("denseComponentsMut<T> is not supported for archetyped components");

    pool_t<T>& pool = storage<T>();
    pool.markDirty();
    return pool.dense();
}

template <typename T>
auto ECS::denseComponents() const
{
    if (nullptr != archetypePoolForComponent<T>())
        throw std::runtime_error("denseComponents<T> is not supported for archetyped components; use ecs::ViewOf<T>");

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

template <typename... Filters>
FilteredView<Filters...> ECS::query()
{ return FilteredView<Filters...>(*this); }

template <typename... Filters>
FilteredView<Filters...> ECS::render_query()
{ return FilteredView<Filters...>(*this, ViewStorage::Rendering); }

template <typename Func>
void ECS::eachEntity(Func&& func)
{
    auto view = this->view<>();
    view.each([&func](const Entity& entity)
    {
        func(entity);
    });
}
