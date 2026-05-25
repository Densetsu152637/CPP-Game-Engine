//
// Created by Nicholas on 07/05/26.
//

#include "ecs_sim.h"

#include <stdexcept>

void ECSSimulator::_append_first_wall()
{
    m_walls.append({ FIRST_WALL, ArrayList<std::string>() });
}

void ECSSimulator::_remove_job_from_walls(const std::string& jobName)
{
    for (auto& wall : m_walls)
    {
        wall.second.remove(jobName);
    }
}

size_t ECSSimulator::_find_wall_index(const std::string& wallName) const
{
    const int hit = m_walls.find(
        [&wallName](const auto& pair) { return wallName == pair.first; }
    );

    if (hit == -1)
        throw std::runtime_error("Missing wall name: " + wallName);

    return static_cast<size_t>(hit);
}

void ECSSimulator::createWall(const std::string& wallName)
{
    if (wallName.empty())
        throw std::invalid_argument("ECSSimulator wall name cannot be empty");

    const int hit = m_walls.find(
        [&wallName](const auto& pair) { return wallName == pair.first; }
    );

    if (hit != -1)
        return;

    m_walls.append({ wallName, ArrayList<std::string>() });
}

void ECSSimulator::clear()
{
    m_jobs.clear();
    m_walls.clear();
    _append_first_wall();
    createWall(DEFAULT_WALL);
}

void ECSSimulator::simulate()
{
    for (const auto& pair : m_walls)
    {
        ArrayList<Job> jobs = pair.second.map(
            [this](const auto& name) { return m_jobs[name]; }
        );
        ecs_sim::execute_wall(m_pool, m_ecs, jobs);
    }

    m_ecs.swapBuffers();
}
