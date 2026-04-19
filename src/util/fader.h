//
// Created by Nicholas on 19/04/26.
//

#pragma once

#include "../util/stopwatch.h"

class Fader {

    float m_start_intensity;
    int m_fade_time;
    bool m_fading_in;
    StopWatch m_timer;

public:

    Fader(int timeMs) : Fader(0.0f, timeMs) {}
    Fader(float startingOpacity, int timeMs) :
        m_timer(),
        m_start_intensity(startingOpacity),
        m_fade_time(timeMs),
        m_fading_in(true)
    {}

    void fadeIn() {
        m_start_intensity = intensity();
        m_timer.restart();
    }

    void fadeOut() {
        m_start_intensity = intensity();
        m_timer.restart();
    }

    float intensity() {
        float d = m_timer.delta_ms();
        if (d > m_fade_time) {
            if (!m_timer.isStopped()) m_timer.stop();
            return m_fading_in ? 1.0f : 0.0f;
        }

        float ratio = (float) d / m_fade_time;
        float ret = m_fading_in ? m_start_intensity + ratio : m_start_intensity - ratio;
        return Toolbox.clamp(ret, 0.0f, 1.0f);
    }

};
