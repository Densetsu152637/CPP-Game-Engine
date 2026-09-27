// CPU-side material values with an explicit std140-compatible tint layout.
#pragma once

#include <array>
#include <type_traits>

namespace rendering
{
    // One vec4 at set 0, binding 1. GPU code uploads these named fields only.
    struct MaterialUniform
    {
        std::array<float, 4> baseColor { 1.0f, 1.0f, 1.0f, 1.0f };
    };

    static_assert(sizeof(MaterialUniform) == sizeof(float) * 4);
    static_assert(std::is_trivially_copyable_v<MaterialUniform>);
}
