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

    inline bool contains_internally_parallel_jobs(const ArrayList<Job*>& jobs)
    {
        for (const Job* job : jobs)
        {
            if (job->usesInternalParallelism)
                return true;
        }

        return false;
    }

    inline bool should_run_jobs_on_caller(Threadpool& pool, const ArrayList<Job*>& jobs)
    {
        return contains_internally_parallel_jobs(jobs) &&
            jobs.length() >= pool.size();
    }

    inline void run_jobs_on_caller(Threadpool& pool, ECS& ecs, ArrayList<Job*>& jobs)
    {
        for (Job* job : jobs)
            job->execute(ecs, pool);
    }

    inline void submit_jobs(Threadpool& pool, ECS& ecs, ArrayList<Job*>& jobs, ArrayList<Promise<bool>>& promises)
    {
        promises.reserve(jobs.length());

        for (Job* job : jobs)
        {
            promises.append(pool.submit([&ecs, &pool, job]()
            {
                job->execute(ecs, pool);
                return true;
            }));
        }
    }

    inline void execute_jobs_concurrently(Threadpool& pool, ECS& ecs, ArrayList<Job*>& jobs, const BufferSwap swap)
    {
        if (jobs.empty())
        {
            swap_after_jobs(ecs, swap);
            return;
        }

        if (should_run_jobs_on_caller(pool, jobs))
        {
            run_jobs_on_caller(pool, ecs, jobs);
            swap_after_jobs(ecs, swap);
            return;
        }

        ArrayList<Promise<bool>> promises;
        submit_jobs(pool, ecs, jobs, promises);
        await_promises(promises);

        swap_after_jobs(ecs, swap);
    }
}
