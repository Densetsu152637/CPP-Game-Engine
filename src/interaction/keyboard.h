//
// Created by Nicholas on 26/04/26.
//

#pragma once
#include "../core/interfaces.h"
#include "../async/buffer.h"
#include "../structs/arraylist.h"

struct KeyState
{
    bool pressed = false;
    bool held = false;
    bool released = false;

    KeyState() {}
    KeyState(const bool p, const bool h, const bool r) : pressed(p), held(h), released(r) {}

    bool is_tapped() const { return pressed; }
    bool is_pressed() const { return pressed || held; }
    bool is_held() const { return held;}
    bool is_released() const { return released; }
};


class KeyboardListener : public IPollable
{

    static inline uint32_t NUMBER_KEY_STATES = 512;

    DoubleBuffer<ArrayList<KeyState>> states {
        []()
        {
            return ArrayList<KeyState>(NUMBER_KEY_STATES, true);
        },
        [](const ArrayList<KeyState>& src, ArrayList<KeyState>& dst)
        {
            for (int i = 0; i < src.length(); i++)
            {
                dst[i] = src[i];
            }
        }
    };

public:

    KeyboardListener() {}

    void poll() override;

    KeyState state_of(const size_t code) const;
    bool valid_code(const size_t code) const;
    void key_pressed();
    void key_released();

};

