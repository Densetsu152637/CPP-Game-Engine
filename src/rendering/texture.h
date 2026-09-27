// CPU-side RGBA8 texture data accepted by explicit Vulkan draw calls.
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace rendering
{
    struct Texture2D
    {
        uint32_t width = 0;
        uint32_t height = 0;
        // Row-major, four unsigned normalized channels per texel: R, G, B, A.
        std::vector<uint8_t> rgba8;

        bool valid() const
        {
            if (width == 0 || height == 0) return false;
            constexpr size_t channels = 4;
            const size_t max = std::numeric_limits<size_t>::max();
            if (static_cast<size_t>(width) > max / channels / static_cast<size_t>(height))
                return false;
            return rgba8.size() == static_cast<size_t>(width) * height * channels;
        }
    };
}
