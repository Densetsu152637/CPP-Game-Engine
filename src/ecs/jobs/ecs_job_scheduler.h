//
// ECS job scheduling, wall ordering, conflict checks, and cached executable batches.
//

#pragma once

#include <string>
#include <unordered_map>

#include "ecs_sim_access.h"
#include "ecs_sim_types.h"
#include "../../logging/logger.h"

class ECSJobScheduler
{
    std::unordered_map<std::string, size_t> m_wallOrder;
    std::unordered_map<std::string, ArrayList<std::string>> m_wallJobs;
    std::unordered_map<std::string, ecs_sim::SimulationJob> m_simJobs;
    std::unordered_map<std::string, ecs_sim::RenderJob> m_renderJobs;

    Logger* m_logger = nullptr;
    bool m_cacheDirty = true;
    ArrayList<std::string> m_cachedWallOrdering;
    std::unordered_map<std::string, ArrayList<ecs_sim::Job*>> m_cachedWallJobs;
    ArrayList<ecs_sim::Job*> m_cachedRenderJobs;

    void appendDefaultWalls();
    void removeJobFromWalls(const std::string& jobName);
    size_t findWallIndex(const std::string& wallName) const;
    void invalidateCache();
    void rebuildCache();

public:
    static constexpr const char* FIRST_WALL = "__INIT__";
    static constexpr const char* DEFAULT_WALL = "__RUNTIME__";
    static constexpr const char* FINAL_WALL = "__CLEANUP__";

    ECSJobScheduler();

    void createWall(const std::string& wallName, size_t position);
    void clear();
    void setLogger(Logger* logger);
    void clearLogger();
    void log(std::string message) const;

    void queueSimulationJob(const std::string& name, std::string wall, ecs_sim::SimulationJob job);
    void queueRenderJob(const std::string& name, ecs_sim::RenderJob job);

    ArrayList<std::string> getWallOrdering() const;
    const ArrayList<std::string>& simulationWallOrdering();
    ArrayList<ecs_sim::Job*>& simulationJobsForWall(const std::string& wallName);
    ArrayList<ecs_sim::Job*>& renderJobs();
};
