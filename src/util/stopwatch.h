//
// Created by Nicholas on 18/04/26.
//

#ifndef CPP_GAME_ENGINE_STOPWATCH_H
#define CPP_GAME_ENGINE_STOPWATCH_H

#include <chrono>

class StopWatch {

    long vStart;
    long vStop;
    bool stopped;

    long get_time_ms()
    {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                  now.time_since_epoch()
              ).count();;
    }

public:

    StopWatch() { restart(); }

    void start()
    {
        vStart = get_time_ms();
        stopped = false;
    }

    void stop()
    {
        vStop = get_time_ms();
        stopped = true;
    }

    void restart()
    {
        vStop = -1;
        start();
    }

    long delta_ns()
    {
        return stopped ? (vStop - vStart) : get_time_ms() - vStart;
    }

    bool isStopped() { return stopped; }

    float delta_us() { return delta_ns() / 1000.0f; }
    float delta_ms() { return delta_ns() / 1000000.0f; }
    float delta_s() { return delta_ns() / 1000000000.0f; }

};



#endif //CPP_GAME_ENGINE_STOPWATCH_H
