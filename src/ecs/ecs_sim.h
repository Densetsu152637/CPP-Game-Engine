//
// Created by Nicholas on 07/05/26.
//

#pragma once

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include "ecs.h"
#include "ecs_sim_detail.h"

class Threadpool;

class ECSSimulator
{
    using Job = ecs_sim::Job;

    ECS m_ecs;
    Threadpool& m_pool;
    std::unordered_map<std::string, Job> m_jobs;
    ArrayList<std::pair<std::string, ArrayList<std::string>>> m_walls;

    void _append_first_wall();
    void _remove_job_from_walls(const std::string& jobName);
    size_t _find_wall_index(const std::string& wallName) const;

public:
    static constexpr const char* FIRST_WALL = "__INIT__";
    static constexpr const char* DEFAULT_WALL = "__RUNTIME__";

    explicit ECSSimulator(Threadpool& pool)
        : m_pool(pool)
    {
        _append_first_wall();
        createWall(DEFAULT_WALL);
    }

    ECS& ecs()
    { return m_ecs; }

    const ECS& ecs() const
    { return m_ecs; }

    void createWall(const std::string& wallName);
    void clear();
    void simulate();

    template <typename... Args, typename Callable>
    ECSSimulator& submit(std::string name, Callable&& callable)
    {
        return this->submit<Args...>(
            std::move(name),
            DEFAULT_WALL,
            std::forward<Callable>(callable)
        );
    }

    template <typename... Args, typename Callable>
    ECSSimulator& submit(const std::string& name, std::string wall, Callable&& callable);
};

template <typename... Args, typename Callable>
ECSSimulator& ECSSimulator::submit(const std::string& name, std::string wall, Callable&& callable)
{
    if (name.empty())
        throw std::invalid_argument("ECSSimulator job name cannot be empty");

    if (wall.empty())
        wall = DEFAULT_WALL;

    const Job job = ecs_sim::make_job<Args...>(std::forward<Callable>(callable));

    _remove_job_from_walls(name);
    m_jobs[name] = job;

    createWall(wall);
    const size_t wallIndex = _find_wall_index(wall);
    m_walls[wallIndex].second.append(name);

    return *this;
}
