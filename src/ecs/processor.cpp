//
// Created by Nicholas on 05/06/26.
//

#include "processor.h"

#include <string>

void ECSProcessor::createWall(const std::string& wallName, const size_t position)
{
    m_scheduler.createWall(wallName, position);
}

void ECSProcessor::clear()
{
    m_scheduler.clear();
}

void ECSProcessor::setSchedulerLogger(Logger* logger)
{
    m_scheduler.setLogger(logger);
}

void ECSProcessor::clearSchedulerLogger()
{
    m_scheduler.clearLogger();
}

ArrayList<std::string> ECSProcessor::getWallOrdering() const
{
    return m_scheduler.getWallOrdering();
}

void ECSProcessor::simulate()
{
    for (const auto& wallName : m_scheduler.simulationWallOrdering())
    {
        ArrayList<ecs_sim::Job*>& sim_jobs = m_scheduler.simulationJobsForWall(wallName);
        m_scheduler.log(
            "running simulation wall \"" + wallName + "\" with " +
                std::to_string(sim_jobs.length()) + " jobs"
        );

        ecs_sim::execute_jobs_concurrently(
            m_pool,
            m_ecs,
            sim_jobs,
            ecs_sim::BufferSwap::Simulation
        );
    }

    m_renderBridge.transferDirtyPools(m_ecs, m_pool, m_scheduler);
}

void ECSProcessor::render()
{
    ArrayList<ecs_sim::Job*>& render_jobs = m_scheduler.renderJobs();

    m_scheduler.log(
        "running rendering batch with " + std::to_string(render_jobs.length()) + " jobs"
    );

    ecs_sim::execute_jobs_concurrently(
        m_pool,
        m_ecs,
        render_jobs,
        ecs_sim::BufferSwap::Rendering
    );
}
