#include "sprite2d.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace rendering
{
    namespace
    {
        void positive(float value)
        {
            if (!std::isfinite(value) || value <= 0)
                throw std::invalid_argument("2D sizes must be finite and positive");
        }
        void finite(float value)
        {
            if (!std::isfinite(value))
                throw std::invalid_argument("2D coordinates must be finite");
        }
    } // namespace
    Camera2DView makeCamera2DView(const Camera2DDescription& desc, uint32_t width, uint32_t height)
    {
        if (!desc.logicalSize[0] || !desc.logicalSize[1])
            throw std::invalid_argument("Camera logical dimensions must be positive");
        positive(desc.pixelsPerUnit);
        finite(desc.center[0]);
        finite(desc.center[1]);
        Camera2DView result;
        if (!width || !height)
            return result;
        const double fit =
            std::min(double(width) / desc.logicalSize[0], double(height) / desc.logicalSize[1]);
        result.scale = static_cast<float>(fit >= 1 ? std::floor(fit) : fit);
        const uint32_t vw =
            std::max(1u, static_cast<uint32_t>(std::floor(desc.logicalSize[0] * result.scale)));
        const uint32_t vh =
            std::max(1u, static_cast<uint32_t>(std::floor(desc.logicalSize[1] * result.scale)));
        result.framebufferViewport = {(width - vw) / 2, (height - vh) / 2, vw, vh};
        const float sx = 2 * desc.pixelsPerUnit / desc.logicalSize[0],
                    sy = -2 * desc.pixelsPerUnit / desc.logicalSize[1];
        auto center = desc.center;
        if (desc.pixelSnap)
            for (auto& c : center)
                c = std::round(c * desc.pixelsPerUnit) / desc.pixelsPerUnit;
        result.camera.viewProjection = {
            sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, 1, 0, -center[0] * sx, -center[1] * sy, 0, 1};
        return result;
    }
    CameraUniform makeLogicalUiCamera(uint32_t width, uint32_t height)
    {
        if (!width || !height)
            throw std::invalid_argument("UI dimensions must be positive");
        CameraUniform result;
        result.viewProjection = {2.0f / width, 0, 0, 0, 0, 2.0f / height, 0, 0, 0, 0, 1, 0, -1, -1, 0, 1};
        return result;
    }
    MeshAsset makeSpriteQuad(const Sprite2DDescription& desc, uint32_t tw, uint32_t th, float ppu, bool snap)
    {
        positive(ppu);
        positive(desc.size[0]);
        positive(desc.size[1]);
        for (auto x : desc.position)
            finite(x);
        for (auto x : desc.pivot)
            if (!std::isfinite(x) || x < 0 || x > 1)
                throw std::invalid_argument("Sprite pivot must be normalized");
        for (auto x : desc.tint)
            if (!std::isfinite(x) || x < 0 || x > 1)
                throw std::invalid_argument("Sprite tint must be normalized");
        const auto r = desc.source;
        if (!tw || !th || !r.width || !r.height || r.x > tw || r.y > th || r.width > tw - r.x ||
            r.height > th - r.y)
            throw std::invalid_argument("Sprite atlas rectangle exceeds texture");
        MeshAsset mesh;
        if (!desc.visible)
            return mesh;
        auto pos = desc.position;
        if (snap)
            for (auto& c : pos)
                c = std::round(c * ppu) / ppu;
        const float l = pos[0] - desc.pivot[0] * desc.size[0], b = pos[1] - desc.pivot[1] * desc.size[1];
        const float right = l + desc.size[0], top = b + desc.size[1];
        const float u = float(r.x) / tw, v = float(r.y) / th, ur = float(r.x + r.width) / tw,
                    vb = float(r.y + r.height) / th;
        mesh.vertices = {{{l, top, 0}, {u, v}}, {{l, b, 0}, {u, vb}},      {{right, b, 0}, {ur, vb}},
                         {{l, top, 0}, {u, v}}, {{right, b, 0}, {ur, vb}}, {{right, top, 0}, {ur, v}}};
        return mesh;
    }
    size_t animationFrame(double time, float fps, size_t count, bool loop)
    {
        if (!std::isfinite(time) || time < 0 || !std::isfinite(fps) || fps <= 0 || !count)
            throw std::invalid_argument("Invalid animation timing or empty frame list");
        const double frame = std::floor(time * double(fps));
        if (!std::isfinite(frame))
            throw std::invalid_argument("Animation time exceeds range");
        return loop ? static_cast<size_t>(std::fmod(frame, double(count)))
                    : (frame >= double(count - 1) ? count - 1 : static_cast<size_t>(frame));
    }
    DrawState painter2DState(const Camera2DView& view)
    {
        DrawState state;
        state.blend = BlendMode::StraightAlpha;
        state.depthTest = false;
        state.depthWrite = false;
        state.viewport = view.framebufferViewport;
        state.scissor = view.framebufferViewport;
        return state;
    }
} // namespace rendering
