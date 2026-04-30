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

void KeyboardListener::key_pressed()
{
    int code = -1;
    states.write([code](ArrayList<KeyState>& s) {
        KeyState& k = s[code];
        k.pressed  = true;
        k.held     = false;
        k.released = false;
    });
}

void KeyboardListener::key_released()
{
    int code = -1;
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
