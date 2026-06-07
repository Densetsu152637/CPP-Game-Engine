//
// Created by Nicholas on 05/06/26.
//

#pragma once

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include "ecs_sim_detail.h"
#include "../logging/logger.h"

namespace ecs_processor_detail
{
    inline std::string join_names(const ArrayList<std::string>& names)
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

    inline std::string access_summary(const ecs_sim::AccessSpec& access)
    {
        return "reads=[" + join_names(access.readNames) +
            "] writes=[" + join_names(access.writeNames) +
            "] exclusive=[" + join_names(access.exclusiveWriteNames) + "]";
    }

    inline const char* conflict_reason(const ecs_sim::AccessConflict conflict)
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

    inline void log_scheduler_event(Logger* logger, std::string message)
    {
        if (nullptr != logger)
            logger->push(std::move(message));
    }

    inline void throw_if_wall_conflicts(
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
            if (ecs_sim::AccessConflict::None != conflict)
            {
                throw std::runtime_error(
                    "ECSProcessor job \"" + jobName + "\" conflicts with job \"" +
                    existingJobName + "\" already queued into wall \"" + wallName +
                    "\" because " + conflict_reason(conflict)
                );
            }
        }
    }

    template <typename Arg>
    void guarantee_sim_pool(ECS& ecs)
    {
        using Component = ecs_sim::component_for_arg_t<Arg>;
        using ArrayComponent = ecs_sim::array_component_for_arg_t<Arg>;

        if constexpr (!std::is_void_v<Component>)
        {
            ecs.template guarantee_component_pool<Component>();
        }

        if constexpr (!std::is_void_v<ArrayComponent>)
        {
            ecs.template guarantee_component_pool<ArrayComponent>();
        }
    }

    template <typename... Args>
    void guarantee_sim_pools(ECS& ecs)
    {
        (guarantee_sim_pool<Args>(ecs), ...);
    }

    template <typename Component>
    void guarantee_render_pool(ECS& ecs)
    {
        ecs.template guarantee_component_pool<Component>();
        ecs.template guarantee_render_component_pool<Component>();
    }

    template <typename... Components>
    void guarantee_render_pools(ECS& ecs)
    {
        (guarantee_render_pool<Components>(ecs), ...);
    }
}

class ECSProcessor
{
    using SimulationJob = ecs_sim::SimulationJob;
    using RenderJob = ecs_sim::RenderJob;

    ECS m_ecs;
    Threadpool& m_pool;
    std::unordered_map<std::string, size_t> m_wall_order;
    std::unordered_map<std::string, ArrayList<std::string>> m_wall_jobs;

    std::unordered_map<std::string, SimulationJob> m_sim_jobs;
    std::unordered_map<std::string, RenderJob> m_render_jobs;
    Logger* m_schedulerLogger = nullptr;

    void _append_default_walls();
    void _remove_job_from_walls(const std::string& jobName);
    size_t _find_wall_index(const std::string& wallName) const;

public:
    static constexpr auto FIRST_WALL = "__INIT__";
    static constexpr auto DEFAULT_WALL = "__RUNTIME__";
    static constexpr auto FINAL_WALL = "__CLEANUP__";

    explicit ECSProcessor(Threadpool& pool)
        : m_pool(pool)
    {
        _append_default_walls();
    }

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
    if (name.empty())
        throw std::invalid_argument("ECSProcessor job name cannot be empty");

    if (wall.empty())
        wall = DEFAULT_WALL;
    else if (!m_wall_order.contains(wall))
        throw std::out_of_range("ECSProcessor wall does not exist: " + wall);

    ecs_processor_detail::guarantee_sim_pools<Args...>(m_ecs);
    const SimulationJob job = ecs_sim::make_sim_job<Args...>(std::forward<Callable>(callable));

    ecs_processor_detail::throw_if_wall_conflicts(
        name,
        wall,
        job,
        m_wall_jobs.at(wall),
        m_sim_jobs
    );

    _remove_job_from_walls(name);
    m_sim_jobs[name] = job;
    m_wall_jobs[wall].append(name);
    ecs_processor_detail::log_scheduler_event(
        m_schedulerLogger,
        "queued simulation job \"" + name + "\" into wall \"" + wall + "\" " +
            ecs_processor_detail::access_summary(job.access)
    );

    return *this;
}

template <typename... Args, typename Callable>
ECSProcessor& ECSProcessor::queue_into_rendering(const std::string& name, Callable&& callable)
{
    if (name.empty())
        throw std::invalid_argument("ECSProcessor job name cannot be empty");

    ecs_processor_detail::guarantee_render_pools<Args...>(m_ecs);
    const RenderJob job = ecs_sim::make_render_job<Args...>(std::forward<Callable>(callable));

    m_render_jobs[name] = job;
    ecs_processor_detail::log_scheduler_event(
        m_schedulerLogger,
        "queued rendering job \"" + name + "\""
    );
    return *this;
}
