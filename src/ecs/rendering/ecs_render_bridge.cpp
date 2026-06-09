//
// Simulation-to-render component transfer for dirty render-registered pools.
//

#include "ecs_render_bridge.h"

#include <string>

#include "../ecs.h"
#include "../jobs/ecs_job_scheduler.h"
#include "../jobs/ecs_sim_execution.h"
#include "../../async/threadpool.h"

namespace
{
    void copyFullStandaloneToRenderArchetype(
        const IComponentPool& source,
        ecs::IArchetypePool& destination,
        const ecs::ComponentTypeId componentTypeId
    ) {
        destination.clearComponentStorage(componentTypeId);
        for (size_t denseIndex = 0; denseIndex < source.size(); ++denseIndex)
        {
            const size_t entityIndex = source.entityAt(denseIndex);
            void* destinationComponent = destination.ensureComponentPointer(entityIndex, componentTypeId);
            if (nullptr != destinationComponent)
                source.copyComponentTo(entityIndex, destinationComponent);
        }
    }

    void copyDirtyStandaloneToRenderArchetype(
        const IComponentPool& source,
        ecs::IArchetypePool& destination,
        const ecs::ComponentTypeId componentTypeId
    ) {
        for (const size_t entityIndex : source.dirtyEntities())
        {
            if (!source.containsEntity(entityIndex))
            {
                destination.removeComponent(entityIndex, componentTypeId);
                continue;
            }

            void* destinationComponent = destination.ensureComponentPointer(entityIndex, componentTypeId);
            if (nullptr != destinationComponent && source.copyComponentTo(entityIndex, destinationComponent))
                continue;

            destination.removeComponent(entityIndex, componentTypeId);
        }
    }

    void copyFullArchetypeToRenderArchetype(
        const ecs::IArchetypePool& source,
        ecs::IArchetypePool& destination,
        const ecs::ComponentTypeId componentTypeId
    ) {
        destination.clearComponentStorage(componentTypeId);
        const size_t componentCount = source.componentSize(componentTypeId);
        for (size_t componentDenseIndex = 0; componentDenseIndex < componentCount; ++componentDenseIndex)
        {
            const size_t entityIndex = source.componentEntityAt(componentTypeId, componentDenseIndex);
            void* destinationComponent = destination.ensureComponentPointer(entityIndex, componentTypeId);
            if (nullptr != destinationComponent && !source.copyComponentTo(entityIndex, componentTypeId, destinationComponent))
                destination.removeComponent(entityIndex, componentTypeId);
        }
    }

    void copyDirtyArchetypeToRenderArchetype(
        const ecs::IArchetypePool& source,
        ecs::IArchetypePool& destination,
        const ecs::ComponentTypeId componentTypeId
    ) {
        for (const size_t entityIndex : source.componentDirtyEntities(componentTypeId))
        {
            if (!source.hasComponent(entityIndex, componentTypeId))
            {
                destination.removeComponent(entityIndex, componentTypeId);
                continue;
            }

            void* destinationComponent = destination.ensureComponentPointer(entityIndex, componentTypeId);
            if (nullptr != destinationComponent && source.copyComponentTo(entityIndex, componentTypeId, destinationComponent))
                continue;

            destination.removeComponent(entityIndex, componentTypeId);
        }
    }
}

void ECSRenderBridge::transferDirtyPools(ECS& ecs, Threadpool& pool, ECSJobScheduler& scheduler)
{
    struct ArchetypeTransferSource
    {
        ecs::IArchetypePool* pool = nullptr;
        ecs::ComponentTypeId componentTypeId = 0;
    };

    auto& renderPools = ecs.m_components.renderComponentPools();
    auto& componentPools = ecs.m_components.componentPools();
    auto& renderArchetypes = ecs.m_components.renderArchetypes();

    ArrayList<Promise<bool>> transferPromises{ renderPools.size() };
    ArrayList<IComponentPool*> transferredSources{ renderPools.size() };
    ArrayList<ArchetypeTransferSource> transferredArchetypeSources{ renderPools.size() };

    for (auto& [type, renderPool] : renderPools)
    {
        IComponentPool* renderPoolPtr = renderPool.get();

        const auto simPoolHit = componentPools.find(type);
        if (simPoolHit != componentPools.end())
        {
            IComponentPool* simPoolPtr = simPoolHit->second.get();
            if (!simPoolPtr->isDirty())
                continue;

            scheduler.log(
                std::string("transferring dirty render component pool type=") +
                    renderPoolPtr->type_name() +
                    (simPoolPtr->isFullyDirty()
                        ? std::string(" mode=full")
                        : std::string(" mode=entities count=") + std::to_string(simPoolPtr->dirtyEntities().length()))
            );

            transferredSources.append(simPoolPtr);
            transferPromises.append(pool.submit([renderPoolPtr, simPoolPtr]()
            {
                renderPoolPtr->writeFrom(*simPoolPtr);
                return true;
            }));
            continue;
        }

        ecs::IArchetypePool* archetypePool = ecs.m_components.archetypePoolForComponent(type);
        if (nullptr == archetypePool || !archetypePool->isComponentDirty(type))
            continue;

        scheduler.log(
            std::string("transferring dirty render archetype component type=") +
                renderPoolPtr->type_name() +
                (archetypePool->isComponentFullyDirty(type)
                    ? std::string(" mode=full")
                    : std::string(" mode=entities count=") +
                        std::to_string(archetypePool->componentDirtyEntities(type).length()))
        );

        transferredArchetypeSources.append(ArchetypeTransferSource{ archetypePool, type });
        transferPromises.append(pool.submit([renderPoolPtr, archetypePool, type]()
        {
            renderPoolPtr->writeFromArchetype(*archetypePool, type);
            return true;
        }));
    }

    for (size_t poolIndex = 0; poolIndex < renderArchetypes.poolCount(); ++poolIndex)
    {
        ecs::IArchetypePool* renderArchetype = renderArchetypes.poolAt(poolIndex);
        for (const ecs::ComponentTypeId type : renderArchetype->componentTypes())
        {
            const auto simPoolHit = componentPools.find(type);
            if (simPoolHit != componentPools.end())
            {
                IComponentPool* simPoolPtr = simPoolHit->second.get();
                if (!simPoolPtr->isDirty())
                    continue;

                scheduler.log(
                    std::string("transferring dirty render archetype destination component type=") +
                        simPoolPtr->type_name() +
                        (simPoolPtr->isFullyDirty()
                            ? std::string(" mode=full")
                            : std::string(" mode=entities count=") + std::to_string(simPoolPtr->dirtyEntities().length()))
                );

                transferredSources.append(simPoolPtr);
                if (simPoolPtr->isFullyDirty())
                    copyFullStandaloneToRenderArchetype(*simPoolPtr, *renderArchetype, type);
                else
                    copyDirtyStandaloneToRenderArchetype(*simPoolPtr, *renderArchetype, type);
                continue;
            }

            ecs::IArchetypePool* simArchetype = ecs.m_components.archetypePoolForComponent(type);
            if (nullptr == simArchetype || !simArchetype->isComponentDirty(type))
                continue;

            scheduler.log(
                std::string("transferring dirty sim archetype to render archetype component type=") +
                    std::to_string(type) +
                    (simArchetype->isComponentFullyDirty(type)
                        ? std::string(" mode=full")
                        : std::string(" mode=entities count=") +
                            std::to_string(simArchetype->componentDirtyEntities(type).length()))
            );

            transferredArchetypeSources.append(ArchetypeTransferSource{ simArchetype, type });
            if (simArchetype->isComponentFullyDirty(type))
                copyFullArchetypeToRenderArchetype(*simArchetype, *renderArchetype, type);
            else
                copyDirtyArchetypeToRenderArchetype(*simArchetype, *renderArchetype, type);
        }
    }

    ecs_sim::await_promises(transferPromises);

    for (IComponentPool* transferredSource : transferredSources)
        transferredSource->clearDirty();

    for (const ArchetypeTransferSource& transferredSource : transferredArchetypeSources)
        transferredSource.pool->clearComponentDirty(transferredSource.componentTypeId);
}
