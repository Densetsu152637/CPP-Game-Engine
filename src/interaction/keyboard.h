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

    KeyState() = default;
    KeyState(const bool p, const bool h, const bool r) : pressed(p), held(h), released(r) {}

    [[nodiscard]] bool is_tapped() const { return pressed; }
    [[nodiscard]] bool is_pressed() const { return pressed || held; }
    [[nodiscard]] bool is_held() const { return held;}
    [[nodiscard]] bool is_released() const { return released; }
};


class KeyboardListener : public IPollable, public WindowEventListener
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

    KeyboardListener() = default;
    ~KeyboardListener() override = default;

    void poll() override;
    void processWindowEvent(const GLFWCallback& e) override;

    KeyState state_of(size_t code) const;
    bool valid_code(size_t code) const;
    void key_pressed(int code);
    void key_released(int code);



};

