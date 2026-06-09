//
// ECS simulation job data structures.
//

#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "../core/component_type_id.h"
#include "../../structs/templates.h"
#include "../../structs/arraylist.h"

class ECS;
class Threadpool;

namespace ecs_sim
{
    using TypeId = ecs::ComponentTypeId;

    struct AccessSpec
    {
        ArrayList<TypeId> reads{4};
        ArrayList<TypeId> writes{4};
        ArrayList<TypeId> exclusiveWrites{4};
        ArrayList<std::string> readNames{4};
        ArrayList<std::string> writeNames{4};
        ArrayList<std::string> exclusiveWriteNames{4};
    };

    struct Job
    {
        using RunFn = void(*)(void*, ECS&, Threadpool&);

        std::shared_ptr<void> context;
        RunFn run = nullptr;
        AccessSpec access;
        bool usesInternalParallelism = false;

        void execute(ECS& ecs, Threadpool& pool) const
        {
            if (nullptr != run)
                run(context.get(), ecs, pool);
        }
    };

    using SimulationJob = Job;
    using RenderJob = Job;
}
