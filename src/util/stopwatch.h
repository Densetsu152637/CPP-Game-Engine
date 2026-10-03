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
