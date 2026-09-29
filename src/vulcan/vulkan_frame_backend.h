#pragma once

#include <map>
#include <memory>
#include <functional>

#include "../rendering/render_device.h"
#include "vulkan_renderer.h"

namespace vulkan
{
    // RenderDevice invokes this backend exclusively from its worker thread.
    // Window/platform events remain the caller's responsibility.
    class VulkanFrameBackend final : public rendering::IRenderBackend
    {
    public:
        using FrameProbe = std::function<void(VulkanRenderer&, uint32_t)>;

    private:
        IVulkanSurfaceProvider& m_surface;
        VulkanRendererConfig m_config;
        FrameProbe m_frameProbe;
        VulkanRenderer m_renderer;
        std::map<uint64_t, std::unique_ptr<VulkanShaderProgram>> m_shaders;
        bool m_initialized = false;

    public:
        explicit VulkanFrameBackend(IVulkanSurfaceProvider& surface,
            VulkanRendererConfig config = {}, FrameProbe frameProbe = {});
        rendering::RenderCapabilities capabilities() const override;
        void initialize() override;
        void createShader(uint64_t id, const rendering::ShaderResource& resource) override;
        void createMesh(uint64_t id, const rendering::MeshResource& resource) override;
        void createTexture(uint64_t id, const rendering::TextureResource& resource) override;
        void updateMesh(uint64_t id, const rendering::MeshResource& resource) override;
        void updateTexture(uint64_t id, const rendering::TextureResource& resource) override;
        void destroyResource(uint64_t id, rendering::RenderResourceKind kind) override;
        void executeFrame(uint64_t frame, const std::vector<rendering::DrawCommand>& draws) override;
        void waitIdle() override;
        void shutdown() override;
    };
}
