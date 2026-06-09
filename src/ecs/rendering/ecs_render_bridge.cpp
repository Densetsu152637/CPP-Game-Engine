//
// Simulation-to-render component transfer for dirty render-registered pools.
//

#include "ecs_render_bridge.h"

#include <string>

#include "../ecs.h"
#include "../jobs/ecs_job_scheduler.h"
#include "../jobs/ecs_sim_execution.h"
#include "../../async/threadpool.h"

void ECSRenderBridge::transferDirtyPools(ECS& ecs, Threadpool& pool, ECSJobScheduler& scheduler)
{
    auto& renderPools = ecs.m_components.renderComponentPools();
    auto& componentPools = ecs.m_components.componentPools();

    ArrayList<Promise<bool>> transferPromises{ renderPools.size() };
    ArrayList<IComponentPool*> transferredSources{ renderPools.size() };

    for (auto& [type, renderPool] : renderPools)
    {
        const auto simPoolHit = componentPools.find(type);
        if (simPoolHit == componentPools.end())
            continue;

        IComponentPool* renderPoolPtr = renderPool.get();
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
    }

    ecs_sim::await_promises(transferPromises);

    for (IComponentPool* transferredSource : transferredSources)
        transferredSource->clearDirty();
}
