//
// ECS simulation job data structures.
//

#pragma once

#include <cstddef>
#include <functional>

#include "../structs/arraylist.h"

class ECS;

namespace ecs_sim
{
    using TypeId = size_t;

    struct AccessSpec
    {
        ArrayList<TypeId> reads{4};
        ArrayList<TypeId> writes{4};
        ArrayList<TypeId> exclusiveWrites{4};
    };

    struct Job
    {
        std::function<void(ECS&)> run;
        AccessSpec access;
    };

    using SimulationJob = Job;
    using RenderJob = Job;
}
