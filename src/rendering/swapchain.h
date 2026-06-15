//
// Backend-neutral swapchain descriptions.
//

#pragma once

#include <cstdint>

namespace rendering
{
    enum class PixelFormat
    {
        Unknown,
        Bgra8Srgb,
        Rgba8Srgb
    };

    enum class ColorSpace
    {
        Unknown,
        SrgbNonlinear
    };

    enum class PresentMode
    {
        Immediate,
        Mailbox,
        Fifo,
        FifoRelaxed
    };

    struct ImageExtent
    {
        uint32_t width = 0;
        uint32_t height = 0;

        bool valid() const
        { return width > 0 && height > 0; }
    };

    struct SwapchainSurfaceFormat
    {
        PixelFormat format = PixelFormat::Unknown;
        ColorSpace colorSpace = ColorSpace::Unknown;
    };

    struct SwapchainConfig
    {
        uint32_t desiredImageCount = 2;
        PresentMode preferredPresentMode = PresentMode::Mailbox;
    };

    struct SwapchainInfo
    {
        ImageExtent extent;
        SwapchainSurfaceFormat surfaceFormat;
        PresentMode presentMode = PresentMode::Fifo;
        uint32_t imageCount = 0;

        bool valid() const
        { return extent.valid() && imageCount > 0; }
    };

    class ISwapchain
    {
    public:
        virtual ~ISwapchain() = default;

        virtual bool valid() const = 0;
        virtual const SwapchainInfo& info() const = 0;
    };
}
