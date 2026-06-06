//
// ECS job execution helpers.
//

#pragma once

#include <exception>

#include "ecs_sim_invocation.h"
#include "../async/threadpool.h"

namespace ecs_sim
{
    enum class BufferSwap
    {
        Simulation,
        Rendering
    };

    template <typename T>
    void await_promises(ArrayList<Promise<T>>& promises)
    {
        for (auto& promise : promises)
        {
            auto& result = promise.await();
            if (result.is_failure())
                std::rethrow_exception(result.exception());
        }
    }

    inline void swap_after_jobs(ECS& ecs, const BufferSwap swap)
    {
        switch (swap)
        {
            case BufferSwap::Simulation:
                ecs.swapSimBuffers();
                break;
            case BufferSwap::Rendering:
                ecs.swapRenderBuffers();
                break;
        }
    }

    inline void mark_simulation_writes_dirty(ECS& ecs, ArrayList<Job>& jobs)
    {
        for (const Job& job : jobs)
        {
            for (const TypeId componentTypeId : job.access.writes)
                ecs.markComponentDirty(componentTypeId);
        }
    }

    inline void execute_jobs_concurrently(Threadpool& pool, ECS& ecs, ArrayList<Job>& jobs, const BufferSwap swap)
    {
        if (jobs.empty())
            return;

        ArrayList<Promise<bool>> promises;
        promises.reserve(jobs.length());

        for (Job& job : jobs)
        {
            promises.append(pool.submit([&ecs, &job]()
            {
                job.run(ecs);
                return true;
            }));
        }

        await_promises(promises);

        if (BufferSwap::Simulation == swap)
            mark_simulation_writes_dirty(ecs, jobs);

        swap_after_jobs(ecs, swap);
    }
}
