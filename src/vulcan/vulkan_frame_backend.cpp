#include "vulkan_frame_backend.h"

#include <stdexcept>
#include <typeindex>

namespace vulkan
{
    VulkanFrameBackend::VulkanFrameBackend(IVulkanSurfaceProvider& surface,
        VulkanRendererConfig config, FrameProbe frameProbe)
        : m_surface(surface), m_config(std::move(config)), m_frameProbe(std::move(frameProbe)) {}

    rendering::RenderCapabilities VulkanFrameBackend::capabilities() const
    {
        rendering::RenderCapabilities result { "vulkan", false, 1, m_initialized && m_renderer.info().portabilitySubset,
            m_initialized && m_renderer.info().sampledTextures,
            m_initialized && m_renderer.info().depthAttachment };
        result.backendReady = m_initialized;
        return result;
    }

    void VulkanFrameBackend::initialize()
    {
        m_renderer.initialize(m_surface, m_config);
        m_initialized = true;
    }

    void VulkanFrameBackend::createShader(uint64_t id, const rendering::ShaderResource& resource)
    {
        auto shader = std::make_unique<VulkanShaderProgram>(resource.name);
        for (const auto& source : resource.sources) shader->addSource(source);
        m_shaders.emplace(id, std::move(shader));
    }

    void VulkanFrameBackend::createMesh(uint64_t id, const rendering::MeshResource& resource)
    {
        const rendering::SerializedBufferView view { resource.vertices.data(), resource.vertices.size(),
            resource.vertexCount, resource.layout.stride, std::type_index(typeid(std::byte)) };
        m_renderer.cacheMesh(id, view, resource.layout);
    }

    void VulkanFrameBackend::createTexture(uint64_t id, const rendering::TextureResource& resource)
    { m_renderer.cacheTexture(id, resource.image); }

    void VulkanFrameBackend::updateMesh(uint64_t id, const rendering::MeshResource& resource)
    {
        m_renderer.waitIdle();
        createMesh(id, resource);
    }

    void VulkanFrameBackend::updateTexture(uint64_t id, const rendering::TextureResource& resource)
    {
        m_renderer.waitIdle();
        createTexture(id, resource);
    }

    void VulkanFrameBackend::destroyResource(uint64_t id, rendering::RenderResourceKind kind)
    {
        switch (kind)
        {
            case rendering::RenderResourceKind::Shader: m_shaders.erase(id); break;
            case rendering::RenderResourceKind::Mesh: m_renderer.releaseMesh(id); break;
            case rendering::RenderResourceKind::Texture: m_renderer.releaseTexture(id); break;
        }
    }

    void VulkanFrameBackend::executeFrame(uint64_t frame,
        const std::vector<rendering::DrawCommand>& draws)
    {
        if (!m_renderer.beginRenderFrame()) return;
        try
        {
            for (const auto& draw : draws)
            {
                const auto found = m_shaders.find(draw.shader.id());
                if (found == m_shaders.end()) throw std::out_of_range("Unknown render shader");
                auto& shader = *found->second;
                for (const auto& uniform : draw.uniforms)
                    shader.uploadUniformBytes({ uniform.name, uniform.slot,
                        uniform.bytes.data(), uniform.bytes.size(),
                        std::type_index(typeid(std::byte)), frame });
                if (draw.mesh.id())
                    m_renderer.drawCached(shader, draw.mesh.id(), draw.texture.id());
                else
                    m_renderer.render(shader);
            }
            const uint32_t imageIndex = m_renderer.currentFrame().imageIndex;
            const auto swapchain = m_renderer.swapchain().handle();
            m_renderer.endRenderFrame();
            // Diagnostic probes run on the same owner thread, after presentation.
            if (m_frameProbe && m_renderer.swapchain().handle() == swapchain)
                m_frameProbe(m_renderer, imageIndex);
        }
        catch (...)
        {
            m_renderer.cancelRenderFrame();
            throw;
        }
    }

    void VulkanFrameBackend::waitIdle()
    { m_renderer.waitIdle(); }

    void VulkanFrameBackend::shutdown()
    {
        m_renderer.shutdown();
        m_shaders.clear();
        m_initialized = false;
    }
}
