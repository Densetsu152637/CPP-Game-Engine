//
// Created by Nicholas on 26/04/26.
//

#include "keyboard.h"

KeyState KeyboardListener::state_of(const size_t code) const
{
    return states.read()[code];
}

bool KeyboardListener::valid_code(const size_t code) const
{
    return code < states.read().length();
}

void KeyboardListener::key_pressed(const int code)
{
    states.write([code](ArrayList<KeyState>& s) {
        KeyState& k = s[code];
        k.pressed  = true;
        k.held     = false;
        k.released = false;
    });
}

void KeyboardListener::key_released(const int code)
{
    states.write([code](ArrayList<KeyState>& s) {
        KeyState& k = s[code];
        k.pressed  = false;
        k.held     = false;
        k.released = true;
    });
}

void KeyboardListener::poll()
{
    // Update write buffer
    states.write([](ArrayList<KeyState>& s) {
        for (KeyState& k : s) {
            if (k.pressed) {
                k.pressed = false;
                k.held = true;
            }
            else if (k.released) {
                k.released = false;
            }
        }
    });

    // Publish snapshot
    states.swap();
}

void KeyboardListener::processWindowEvent(const GLFWCallback& e)
{
    switch (e.type)
    {
        case CallbackTypes::KEYBOARD_INPUT:
        {
            // key, scancode, action, mods
            const int key = e.vector4_i.a;
            switch (int action = e.vector4_i.c)
            {
                case 0: key_pressed(key); break;
                case 1: key_released(key); break;
                default: break;
            }
            break;
        }
        default: ;
    }

};
