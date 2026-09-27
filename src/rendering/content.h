// Small CPU-only loaders for the documented first-project mesh and texture formats.
#pragma once

#include <array>
#include <filesystem>
#include <type_traits>
#include <vector>

#include "gpu_buffer.h"
#include "texture.h"

namespace rendering
{
    // Explicit asset vertex layout: position xyz followed by texture uv.
    struct MeshVertex
    {
        std::array<float, 3> position {};
        std::array<float, 2> uv {};
    };
    static_assert(sizeof(MeshVertex) == sizeof(float) * 5);
    static_assert(std::is_trivially_copyable_v<MeshVertex>);

    struct MeshAsset
    {
        std::vector<MeshVertex> vertices;

        VertexLayout layout() const;
        SerializedBufferView vertexData() const;
        bool valid() const;
    };

    // CGMESH 1 stores a non-indexed triangle list with one `vertex x y z u v`
    // record per vertex. PPM P3 is accepted at max value 255 and becomes RGBA8.
    MeshAsset loadMeshAsset(const std::filesystem::path& path);
    Texture2D loadTexturePpm(const std::filesystem::path& path);
}
