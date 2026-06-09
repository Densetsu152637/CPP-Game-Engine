//
// ECS job scheduling, wall ordering, conflict checks, and cached executable batches.
//

#include "ecs_job_scheduler.h"

#include <format>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace ecs_job_scheduler_detail
{
    std::string join_names(const ArrayList<std::string>& names)
    {
        if (names.empty())
            return "none";

        std::string output;
        for (size_t i = 0; i < names.length(); ++i)
        {
            if (i > 0)
                output += ", ";
            output += names[i];
        }
        return output;
    }

    std::string access_summary(const ecs_sim::AccessSpec& access)
    {
        return "reads=[" + join_names(access.readNames) +
            "] writes=[" + join_names(access.writeNames) +
            "] exclusive=[" + join_names(access.exclusiveWriteNames) + "]";
    }

    const char* conflict_reason(const ecs_sim::AccessConflict conflict)
    {
        switch (conflict)
        {
            case ecs_sim::AccessConflict::MutableMutable:
                return "both jobs mutably write the same component type";
            case ecs_sim::AccessConflict::NonBufferedMutableRead:
                return "one job mutably writes a non-buffered component read by the other job";
            case ecs_sim::AccessConflict::None:
                return "no conflict";
        }

        return "unknown conflict";
    }

    void throw_if_wall_conflicts(
        const std::string& jobName,
        const std::string& wallName,
        const ecs_sim::SimulationJob& job,
        const ArrayList<std::string>& wallJobs,
        const std::unordered_map<std::string, ecs_sim::SimulationJob>& simJobs
    ) {
        for (const std::string& existingJobName : wallJobs)
        {
            if (existingJobName == jobName)
                continue;

            const auto existingJob = simJobs.find(existingJobName);
            if (existingJob == simJobs.end())
                continue;

            const ecs_sim::AccessConflict conflict = ecs_sim::conflict_between(existingJob->second.access, job.access);
            if (ecs_sim::AccessConflict::None == conflict)
                continue;

            throw std::runtime_error(
                "ECSProcessor job \"" + jobName + "\" conflicts with job \"" +
                existingJobName + "\" already queued into wall \"" + wallName +
                "\" because " + conflict_reason(conflict)
            );
        }
    }
}

ECSJobScheduler::ECSJobScheduler()
{
    appendDefaultWalls();
}

void ECSJobScheduler::appendDefaultWalls()
{
    createWall(FIRST_WALL, 0);
    createWall(DEFAULT_WALL, 1);
    createWall(FINAL_WALL, 999);
}

void ECSJobScheduler::removeJobFromWalls(const std::string& jobName)
{
    for (auto& arr : m_wallJobs | std::views::values)
        arr.remove(jobName);
}

size_t ECSJobScheduler::findWallIndex(const std::string& wallName) const
{
    return m_wallOrder.at(wallName);
}

void ECSJobScheduler::invalidateCache()
{
    m_cacheDirty = true;
}

void ECSJobScheduler::rebuildCache()
{
    if (!m_cacheDirty)
        return;

    m_cachedWallOrdering = getWallOrdering();
    m_cachedWallJobs.clear();

    for (const std::string& wallName : m_cachedWallOrdering)
    {
        const ArrayList<std::string>& wallJobNames = m_wallJobs.at(wallName);
        ArrayList<ecs_sim::Job*> jobs{wallJobNames.length()};

        for (const std::string& jobName : wallJobNames)
            jobs.append(&m_simJobs.at(jobName));

        m_cachedWallJobs.insert_or_assign(wallName, std::move(jobs));
    }

    m_cachedRenderJobs.clear();
    m_cachedRenderJobs.reserve(m_renderJobs.size());
    for (auto& job : m_renderJobs | std::views::values)
        m_cachedRenderJobs.append(&job);

    m_cacheDirty = false;
}

void ECSJobScheduler::createWall(const std::string& wallName, const size_t position)
{
    if (wallName.empty())
        throw std::invalid_argument("ECSProcessor wall name cannot be empty");

    if (m_wallOrder.contains(wallName))
        throw std::runtime_error(std::format("Wall \"{}\" already exists in position {}", wallName, findWallIndex(wallName)));

    m_wallOrder.insert({ wallName, position });
    m_wallJobs.insert({ wallName, ArrayList<std::string>() });
    invalidateCache();
}

void ECSJobScheduler::clear()
{
    m_simJobs.clear();
    m_renderJobs.clear();
    m_wallOrder.clear();
    m_wallJobs.clear();
    appendDefaultWalls();
    invalidateCache();
}

void ECSJobScheduler::setLogger(Logger* logger)
{
    m_logger = logger;
}

void ECSJobScheduler::clearLogger()
{
    m_logger = nullptr;
}

void ECSJobScheduler::log(std::string message) const
{
    if (nullptr != m_logger)
        m_logger->push(std::move(message));
}

void ECSJobScheduler::queueSimulationJob(const std::string& name, std::string wall, ecs_sim::SimulationJob job)
{
    if (name.empty())
        throw std::invalid_argument("ECSProcessor job name cannot be empty");

    if (wall.empty())
        wall = DEFAULT_WALL;
    else if (!m_wallOrder.contains(wall))
        throw std::out_of_range("ECSProcessor wall does not exist: " + wall);

    ecs_job_scheduler_detail::throw_if_wall_conflicts(
        name,
        wall,
        job,
        m_wallJobs.at(wall),
        m_simJobs
    );

    removeJobFromWalls(name);
    m_simJobs.insert_or_assign(name, std::move(job));
    m_wallJobs[wall].append(name);
    invalidateCache();

    log(
        "queued simulation job \"" + name + "\" into wall \"" + wall + "\" " +
        ecs_job_scheduler_detail::access_summary(m_simJobs.at(name).access)
    );
}

void ECSJobScheduler::queueRenderJob(const std::string& name, ecs_sim::RenderJob job)
{
    if (name.empty())
        throw std::invalid_argument("ECSProcessor job name cannot be empty");

    m_renderJobs.insert_or_assign(name, std::move(job));
    invalidateCache();
    log("queued rendering job \"" + name + "\"");
}

ArrayList<std::string> ECSJobScheduler::getWallOrdering() const
{
    ArrayList<std::pair<std::string, size_t>> wallOrdering{m_wallOrder.size()};
    for (const auto& wallOrderPair : m_wallOrder)
        wallOrdering.append(wallOrderPair);

    wallOrdering.sort(
        [](auto& p1, auto& p2)
        { return p1.second < p2.second; }
    );

    return wallOrdering.map([](auto& p) { return p.first; });
}

const ArrayList<std::string>& ECSJobScheduler::simulationWallOrdering()
{
    rebuildCache();
    return m_cachedWallOrdering;
}

ArrayList<ecs_sim::Job*>& ECSJobScheduler::simulationJobsForWall(const std::string& wallName)
{
    rebuildCache();
    return m_cachedWallJobs.at(wallName);
}

ArrayList<ecs_sim::Job*>& ECSJobScheduler::renderJobs()
{
    rebuildCache();
    return m_cachedRenderJobs;
}
