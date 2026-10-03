#pragma once
#include "runtime.h"
#include "../rendering/sprite2d.h"
#include <cmath>

namespace project::desktop
{
    inline InputSnapshot mergeInput(const interaction::ActionFrame& physical, const InputSnapshot& injected,
        std::set<std::string>& previousHeld)
    {
        InputSnapshot result;
        result.held=physical.held;result.held.insert(injected.held.begin(),injected.held.end());
        for(const auto& action:result.held) if(!previousHeld.contains(action)) result.pressed.insert(action);
        for(const auto& action:previousHeld) if(!result.held.contains(action)) result.released.insert(action);
        previousHeld=result.held;
        result.values=physical.values;for(const auto& action:injected.held) result.values[action]=1;
        result.pointerX=physical.pointerX;result.pointerY=physical.pointerY;
        result.wheelX=physical.wheelX;result.wheelY=physical.wheelY;result.focused=physical.focused;
        return result;
    }

    inline std::array<float,2> logicalPointer(double x,double y,int windowWidth,int windowHeight,
        int framebufferWidth,int framebufferHeight,const rendering::PixelRect& viewport,
        const std::array<std::uint32_t,2>& logicalSize)
    {
        if(!std::isfinite(x)||!std::isfinite(y)||windowWidth<=0||windowHeight<=0||
            framebufferWidth<=0||framebufferHeight<=0||viewport.width==0||viewport.height==0) return {-1,-1};
        return {static_cast<float>((x*framebufferWidth/windowWidth-viewport.x)*logicalSize[0]/viewport.width),
            static_cast<float>((y*framebufferHeight/windowHeight-viewport.y)*logicalSize[1]/viewport.height)};
    }
}
