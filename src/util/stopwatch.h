//
// Created by Nicholas on 18/04/26.
//

#pragma once

#include <chrono>
#include <cstdint>
#include <ctime>
#include <string>

inline int64_t get_time_ns()
{
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
              now.time_since_epoch()
          ).count();;
}

inline std::string formatDateTime(const int64_t timestampNs) {
    const auto rawTime = static_cast<std::time_t>(timestampNs / 1'000'000'000);
    std::tm timeInfo {};

#ifdef _WIN32
    localtime_s(&timeInfo, &rawTime);
#else
    localtime_r(&rawTime, &timeInfo);
#endif

    char buffer[128];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeInfo);

    return std::string { buffer };
}

class StopWatch {

    int64_t vStart;
    int64_t vStop;
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

    int64_t delta_ns() const
    {
        return stopped ? (vStop - vStart) : get_time_ns() - vStart;
    }

    bool isStopped() const { return stopped; }

    float delta_us() const { return static_cast<float>(delta_ns()) / 1000.0f; }
    float delta_ms() const { return static_cast<float>(delta_ns()) / 1000000.0f; }
    float delta_s() const { return static_cast<float>(delta_ns()) / 1000000000.0f; }

};
