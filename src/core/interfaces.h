//
// Created by Nicholas on 26/04/26.
//

#pragma once

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
