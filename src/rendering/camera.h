// CPU-side camera data with an explicit shader upload layout.
#pragma once

#include <array>
#include <type_traits>

namespace rendering
{
    // One column-major mat4 matching a std140 GLSL block at set 0, binding 2.
    struct CameraUniform
    {
        std::array<float, 16> viewProjection {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
    };

    static_assert(sizeof(CameraUniform) == sizeof(float) * 16);
    static_assert(std::is_trivially_copyable_v<CameraUniform>);
}
