#pragma once
#include <cstdint>
#include <optional>
namespace rendering
{
    struct PixelRect
    {
        uint32_t x = 0, y = 0, width = 0, height = 0;
    };
    enum class BlendMode
    {
        Opaque,
        StraightAlpha
    };
    // Defaults retain the mesh renderer's depth behavior. 2D painter draws disable depth.
    struct DrawState
    {
        BlendMode blend = BlendMode::Opaque;
        bool depthTest = true;
        bool depthWrite = true;
        std::optional<PixelRect> viewport;
        std::optional<PixelRect> scissor;
    };
} // namespace rendering
