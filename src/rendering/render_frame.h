//
// Backend-neutral render frame lifecycle.
//

#pragma once

#include <cstdint>

#include "swapchain.h"

namespace rendering
{
    struct RenderFrame
    {
        uint64_t frameIndex = 0;
        uint32_t imageIndex = 0;
        bool active = false;
    };

    class IRenderFrameCoordinator
    {
    public:
        virtual ~IRenderFrameCoordinator() = default;

        virtual bool beginRenderFrame() = 0;
        virtual void endRenderFrame() = 0;
        virtual void cancelRenderFrame() = 0;
        virtual void waitIdle() = 0;
        virtual const RenderFrame& currentFrame() const = 0;
        virtual const SwapchainInfo& swapchainInfo() const = 0;
    };
}
