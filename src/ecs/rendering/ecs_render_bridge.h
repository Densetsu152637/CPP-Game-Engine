//
// Simulation-to-render component transfer for dirty render-registered pools.
//

#pragma once

class ECS;
class ECSJobScheduler;
class Threadpool;

class ECSRenderBridge
{
public:
    void transferDirtyPools(ECS& ecs, Threadpool& pool, ECSJobScheduler& scheduler);
};
