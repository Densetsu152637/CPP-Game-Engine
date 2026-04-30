//
// Created by Nicholas on 26/04/26.
//

#pragma once
#include "../structs/arraylist.h"


struct WindowEvent
{

};

class WindowEventListener
{
public:
    virtual ~WindowEventListener() = default;

private:
    virtual void processWindowEvent(const WindowEvent& e) = 0;

};

class WindowEventManager
{

    ArrayList<WindowEventListener*> listeners;



};





