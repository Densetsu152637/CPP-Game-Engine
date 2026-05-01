//
// Created by Nicholas on 18/04/26.
//

#pragma once

#include <chrono>
#include <string>

inline long get_time_ns()
{
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
              now.time_since_epoch()
          ).count();;
}

inline std::string formatDateTime(const long timestamp) {
    // 1. Convert long to time_t
    const auto rawTime = static_cast<time_t>(timestamp);

    // 2. Convert to local time structure
    const tm *timeInfo = localtime(&rawTime);

    // 3. Format the string (e.g., YYYY-MM-DD HH:MM:SS)
    char buffer[128]; // should be safe (hopefully)
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %HH:%MM:%SS", timeInfo);

    return std::string { buffer };
}

class StopWatch {

    long vStart;
    long vStop;
    bool stopped;

public:

    StopWatch() { restart(); }
    ~StopWatch() = default;

    void start()
    {
        vStart = get_time_ns();
        stopped = false;
    }

    void stop()
    {
        vStop = get_time_ns();
        stopped = true;
    }

    void restart()
    {
        vStop = -1;
        start();
    }

    long delta_ns() const
    {
        return stopped ? (vStop - vStart) : get_time_ns() - vStart;
    }

    bool isStopped() const { return stopped; }

    float delta_us() const { return static_cast<float>(delta_ns()) / 1000.0f; }
    float delta_ms() const { return static_cast<float>(delta_ns()) / 1000000.0f; }
    float delta_s() const { return static_cast<float>(delta_ns()) / 1000000000.0f; }

};
