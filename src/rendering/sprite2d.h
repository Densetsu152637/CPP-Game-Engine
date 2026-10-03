#pragma once
#include "camera.h"
#include "content.h"
#include "draw_state.h"
namespace rendering
{
    struct Camera2DDescription
    {
        std::array<uint32_t, 2> logicalSize{};
        float pixelsPerUnit = 1;
        std::array<float, 2> center{};
        bool pixelSnap = true;
    };
    struct Camera2DView
    {
        CameraUniform camera;
        PixelRect framebufferViewport;
        float scale = 0;
    };
    // Small windows use uniform fractional fit; zero framebuffer extent returns scale zero.
    Camera2DView makeCamera2DView(const Camera2DDescription&, uint32_t width, uint32_t height);
    CameraUniform makeLogicalUiCamera(uint32_t width, uint32_t height);
    struct Sprite2DDescription
    {
        std::array<float, 2> position{};
        std::array<float, 2> size{1, 1};        // World units; source rectangle is in atlas pixels.
        std::array<float, 2> pivot{0.5f, 0.5f}; // Normalized, measured from left/bottom.
        PixelRect source;
        std::array<float, 4> tint{1, 1, 1, 1};
        bool visible = true;
    };
    MeshAsset makeSpriteQuad(const Sprite2DDescription&, uint32_t textureWidth, uint32_t textureHeight,
                             float pixelsPerUnit = 1, bool pixelSnap = false);
    size_t animationFrame(double elapsedSeconds, float framesPerSecond, size_t frameCount, bool loop);
    DrawState painter2DState(const Camera2DView& view);
} // namespace rendering
