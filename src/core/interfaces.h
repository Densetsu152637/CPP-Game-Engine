//
// Created by Nicholas on 26/04/26.
//

#pragma once
#include <memory>

#include "../async/promise.h"
#include "../structs/vector.h"
#include "../interaction/window_event.h"

struct IPollable
{

    virtual ~IPollable() = default;
    virtual void poll() = 0;

};


struct IRenderElement
{

    virtual ~IRenderElement() = default;
    virtual void pre_render() {}; // implemented by default
    virtual void render() = 0;
    virtual void post_render() {}; // implemented by default

};


struct IResourceManager
{

    virtual ~IResourceManager() = default;
    virtual void cleanUp() {};

};


struct IDisplayManager
{

    virtual ~IDisplayManager() = default;
    virtual bool createDisplay() = 0;
    virtual void closeDisplay() = 0;
    virtual Vector2i getScreenSize() = 0;
    virtual bool isClosed() = 0;
    virtual void show() = 0;
    virtual void hide() = 0;
    virtual void centerCursor() = 0;
    virtual int refreshRate() = 0;
    virtual WindowEventManager& getWindowEventManager() = 0;

};
