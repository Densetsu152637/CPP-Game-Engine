//
// Created by Nicholas on 05/06/26.
//

#pragma once

#include <string>
#include <type_traits>
#include <utility>

#include "jobs/ecs_job_scheduler.h"
#include "jobs/ecs_processor_pool_guarantees.h"
#include "rendering/ecs_render_bridge.h"
#include "jobs/ecs_sim_detail.h"

class ECSProcessor
{
    ECS m_ecs;
    Threadpool& m_simulationPool;
    Threadpool& m_renderPool;
    ECSJobScheduler m_scheduler;
    ECSRenderBridge m_renderBridge;

public:
    static constexpr const char* FIRST_WALL = ECSJobScheduler::FIRST_WALL;
    static constexpr const char* DEFAULT_WALL = ECSJobScheduler::DEFAULT_WALL;
    static constexpr const char* FINAL_WALL = ECSJobScheduler::FINAL_WALL;

    explicit ECSProcessor(Threadpool& pool)
        : ECSProcessor(pool, pool)
    {}

    ECSProcessor(Threadpool& simulationPool, Threadpool& renderPool)
        : m_simulationPool(simulationPool),
          m_renderPool(renderPool)
    {}

    ECS& ecs()
    { return m_ecs; }

    const ECS& ecs() const
    { return m_ecs; }

    void createWall(const std::string& wallName, size_t position);
    void clear();
    void setSchedulerLogger(Logger* logger);
    void clearSchedulerLogger();

    void simulate();
    void render();

    ArrayList<std::string> getWallOrdering() const;

    template <typename... Components>
    ECSProcessor& registerArchetype()
    {
        m_ecs.template registerArchetype<Components...>();
        return *this;
    }

    template <typename... Components>
    ECSProcessor& registerRenderArchetype()
    {
        m_ecs.template registerRenderArchetype<Components...>();
        return *this;
    }

    template <typename... Args, typename Callable>
    ECSProcessor& queue_into_sim(std::string name, Callable&& callable)
    {
        return this->queue_into_sim<Args...>(
            std::move(name),
            DEFAULT_WALL,
            std::forward<Callable>(callable)
        );
    }

    template <typename... Args, typename Callable>
    ECSProcessor& queue_into_sim(const std::string& name, std::string wall, Callable&& callable);

    template <typename... Args, typename Callable>
    ECSProcessor& queue_into_rendering(const std::string& name, Callable&& callable);
};

template <typename... Args, typename Callable>
ECSProcessor& ECSProcessor::queue_into_sim(const std::string& name, std::string wall, Callable&& callable)
{
    ecs_processor_detail::guarantee_sim_pools<Args...>(m_ecs);
    m_scheduler.queueSimulationJob(
        name,
        std::move(wall),
        ecs_sim::make_sim_job<Args...>(std::forward<Callable>(callable))
    );

    return *this;
}

template <typename... Args, typename Callable>
ECSProcessor& ECSProcessor::queue_into_rendering(const std::string& name, Callable&& callable)
{
    ecs_processor_detail::guarantee_render_pools<Args...>(m_ecs);
    m_scheduler.queueRenderJob(
        name,
        ecs_sim::make_render_job<Args...>(std::forward<Callable>(callable))
    );
    return *this;
}
