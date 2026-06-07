//
// Created by Nicholas on 05/06/26.
//

#include "processor.h"

#include <format>
#include <ranges>
#include <string>

void ECSProcessor::_append_default_walls()
{
    createWall(FIRST_WALL, 0);
    createWall(DEFAULT_WALL, 1);
    createWall(FINAL_WALL, 999);
}

void ECSProcessor::_remove_job_from_walls(const std::string& jobName)
{
    for (auto& arr : m_wall_jobs | std::views::values)
        arr.remove(jobName);
}

size_t ECSProcessor::_find_wall_index(const std::string& wallName) const
{ return m_wall_order.at(wallName); }

void ECSProcessor::createWall(const std::string& wallName, const size_t position)
{
    if (wallName.empty())
        throw std::invalid_argument("ECSProcessor wall name cannot be empty");

    if (m_wall_order.contains(wallName))
        throw std::runtime_error(std::format("Wall \"{}\" already exists in position {}", wallName, _find_wall_index(wallName)));

    m_wall_order.insert({ wallName, position });
    m_wall_jobs.insert({ wallName, ArrayList<std::string>() });
}

void ECSProcessor::clear()
{
    m_sim_jobs.clear();
    m_render_jobs.clear();
    m_wall_order.clear();
    m_wall_jobs.clear();
    _append_default_walls();
}

void ECSProcessor::setSchedulerLogger(Logger* logger)
{
    m_schedulerLogger = logger;
}

void ECSProcessor::clearSchedulerLogger()
{
    m_schedulerLogger = nullptr;
}

ArrayList<std::string> ECSProcessor::getWallOrdering() const
{
    ArrayList<std::pair<std::string, size_t>> wall_ordering{m_wall_order.size()};
    for (const auto& wall_order_pair : m_wall_order)
    {
        wall_ordering.append(wall_order_pair);
    }

    wall_ordering.sort(
        [](auto& p1, auto& p2)
        { return p1.second < p2.second; }
    );

    return wall_ordering.map([](auto& p) { return p.first; });
}

void ECSProcessor::simulate()
{
    for (const auto& wallName : getWallOrdering())
    {
        ecs_processor_detail::log_scheduler_event(
            m_schedulerLogger,
            "running simulation wall \"" + wallName + "\" with " +
                std::to_string(m_wall_jobs[wallName].length()) + " jobs"
        );

        ArrayList<SimulationJob> sim_jobs = m_wall_jobs[wallName]
            .map(
            [this](const auto& name)
                { return m_sim_jobs[name]; }
        );
        ecs_sim::execute_jobs_concurrently(
            m_pool,
            m_ecs,
            sim_jobs,
            ecs_sim::BufferSwap::Simulation
        );
    }

    // copy read data into rendering pipeline

    ArrayList<Promise<bool>> transfer_promises{ m_ecs.m_renderComponentPools.size() };
    ArrayList<IComponentPool*> transferred_sources{ m_ecs.m_renderComponentPools.size() };

    for (auto& [type, renderPool] : m_ecs.m_renderComponentPools)
    {
        const auto simPoolHit = m_ecs.m_componentPools.find(type);
        if (simPoolHit == m_ecs.m_componentPools.end())
            continue;

        IComponentPool* renderPoolPtr = renderPool.get();
        IComponentPool* simPoolPtr = simPoolHit->second.get();

        if (!simPoolPtr->isDirty())
            continue;

        ecs_processor_detail::log_scheduler_event(
            m_schedulerLogger,
            std::string("transferring dirty render component pool type=") +
                renderPoolPtr->type_name() +
                (simPoolPtr->isFullyDirty()
                    ? std::string(" mode=full")
                    : std::string(" mode=entities count=") + std::to_string(simPoolPtr->dirtyEntities().length()))
        );

        transferred_sources.append(simPoolPtr);
        transfer_promises.append(m_pool.submit([renderPoolPtr, simPoolPtr]()
        {
            renderPoolPtr->writeFrom(*simPoolPtr);
            return true;
        }));
    }

    ecs_sim::await_promises(transfer_promises);

    for (IComponentPool* pool : transferred_sources)
        pool->clearDirty();
}

void ECSProcessor::render()
{
    ArrayList<RenderJob> render_jobs{m_render_jobs.size()};
    for (const auto& [name, job] : m_render_jobs)
    {
        render_jobs.append(job);
    }

    ecs_processor_detail::log_scheduler_event(
        m_schedulerLogger,
        "running rendering batch with " + std::to_string(render_jobs.length()) + " jobs"
    );

    ecs_sim::execute_jobs_concurrently(
        m_pool,
        m_ecs,
        render_jobs,
        ecs_sim::BufferSwap::Rendering
    );
}
