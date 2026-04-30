//
// Created by Nicholas on 26/04/26.
//

#pragma once
#include "../structs/arraylist.h"

enum CallbackTypes
{
    WINDOW_POSITION = 0,
    WINDOW_SIZE,
    WINDOW_CLOSE,
    WINDOW_REFRESH,
    WINDOW_FOCUS,
    WINDOW_ICONIFY,
    WINDOW_MAXIMIZE,
    WINDOW_CONTENT_SCALE,
    FRAMEBUFFER_SIZE,
    KEYBOARD_INPUT,
    CHAR_INPUT,
    CHAR_WITH_MODS,
    MOUSE_BUTTON,
    MOUSE_MOVEMENT,
    CURSOR_BORDER_CROSS,
    MOUSE_SCROL_WHEEL,
    FILE_DROP,
    JOYSTICK_CONNECTION,
    MONITOR_PLUG

};

struct GLFWCallback
{

    int key;
    int scancode;
    int action;
    int mods;
    CallbackTypes type;

};

struct WindowEventListener
{

    virtual ~WindowEventListener() = default;

    virtual void processWindowEvent(const GLFWCallback& e) = 0;

};

class WindowEventManager
{

    ArrayList<WindowEventListener*> listeners;

public:

    void processWindowEvent(const GLFWCallback& e)
    {
        for (WindowEventListener* listener : listeners)
        {
            listener->processWindowEvent(e);
        }
    }

};







