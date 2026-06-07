//
// ECS simulation job data structures.
//

#pragma once

#include <cstddef>
#include <functional>
#include <string>

#include "component_type_id.h"
#include "../structs/templates.h"
#include "../structs/arraylist.h"

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
        std::function<void(ECS&, Threadpool&)> run;
        AccessSpec access;
        bool usesInternalParallelism = false;
    };

    using SimulationJob = Job;
    using RenderJob = Job;
}
