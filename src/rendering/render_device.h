#pragma once

#include <condition_variable>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "gpu_buffer.h"
#include "shader.h"
#include "texture.h"

namespace rendering
{
    enum class RenderResourceKind { Shader, Mesh, Texture };

    struct ShaderResource
    {
        std::string name;
        std::vector<ShaderSource> sources;
    };

    struct MeshResource
    {
        std::vector<std::byte> vertices;
        VertexLayout layout;
        uint32_t vertexCount = 0;
    };

    struct TextureResource
    {
        Texture2D image;
    };

    enum class RenderFeature
    {
        BackendAvailable, CpuParallelRecording, GpuParallelEncoding,
        PortabilitySubset, SampledTextures, DepthAttachment
    };

    struct RenderFeatureStatus
    {
        RenderFeature feature;
        const char* name;
        bool supported;
        std::string reason; // Empty when supported.
    };

    struct RenderCapabilities
    {
        std::string backendName;
        bool parallelRecording = false; // Backend command encoding, beyond parallel CPU draw collection.
        uint32_t maxFramesInFlight = 1;
        bool portabilitySubset = false;
        bool sampledTextures = false;
        bool depthAttachment = false;
        bool backendReady = false;
        std::array<std::string, 6> unsupportedReasons;

        RenderFeatureStatus feature(RenderFeature requested) const;
        std::string diagnostics() const;
    };

    class RenderResourceHandle
    {
        struct State;
        std::shared_ptr<State> m_state;
        explicit RenderResourceHandle(std::shared_ptr<State> state) : m_state(std::move(state)) {}
        friend class RenderDevice;
        friend class RenderCommandBuffer;

    public:
        RenderResourceHandle() = default;
        uint64_t id() const;
        RenderResourceKind kind() const;
        bool valid() const;
    };

    struct DrawUniform
    {
        std::string name;
        ShaderUniformSlot slot;
        std::vector<std::byte> bytes;
    };

    struct DrawOrderKey
    {
        uint64_t producer = 0;
        uint64_t draw = 0;
        auto operator<=>(const DrawOrderKey&) const = default;
    };

    struct DrawCommand
    {
        RenderResourceHandle shader;
        RenderResourceHandle mesh;
        RenderResourceHandle texture;
        std::vector<DrawUniform> uniforms;
        uint64_t order = 0;
        std::optional<DrawOrderKey> stableOrder;
    };

    template <typename T>
    void appendUniform(DrawCommand& draw, std::string name, const T& value, ShaderUniformSlot slot = {})
    {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto* begin = reinterpret_cast<const std::byte*>(&value);
        draw.uniforms.push_back({ std::move(name), slot, { begin, begin + sizeof(T) } });
    }

    class RenderCommandBuffer
    {
        mutable std::mutex m_mutex;
        std::vector<DrawCommand> m_draws;
        std::set<DrawOrderKey> m_orderKeys;
        uint64_t m_nextOrder = 0;
        bool m_explicitOrder = false;
        bool m_sealed = false;
        friend class RenderDevice;

    public:
        // Serial convenience path: order follows record() calls. Concurrent callers
        // must use explicit stable keys for repeatable draw order.
        uint64_t record(DrawCommand draw);
        // Keys sort lexicographically by producer then draw. Duplicate keys or
        // mixing keyed and serial calls in one frame are rejected.
        void record(DrawCommand draw, DrawOrderKey key);
        size_t size() const;
    };

    class IRenderBackend
    {
    public:
        virtual ~IRenderBackend() = default;
        virtual RenderCapabilities capabilities() const = 0;
        virtual void initialize() = 0;
        virtual void createShader(uint64_t id, const ShaderResource& resource) = 0;
        virtual void createMesh(uint64_t id, const MeshResource& resource) = 0;
        virtual void createTexture(uint64_t id, const TextureResource& resource) = 0;
        virtual void updateMesh(uint64_t id, const MeshResource& resource) = 0;
        virtual void updateTexture(uint64_t id, const TextureResource& resource) = 0;
        virtual void destroyResource(uint64_t id, RenderResourceKind kind) = 0;
        virtual void executeFrame(uint64_t frame, const std::vector<DrawCommand>& draws) = 0;
        virtual void waitIdle() = 0;
        virtual void shutdown() = 0;
    };

    class RenderDevice
    {
        struct Job
        {
            enum class Kind { CreateShader, CreateMesh, CreateTexture, UpdateMesh, UpdateTexture,
                Destroy, Frame, Barrier } kind;
            uint64_t sequence = 0;
            uint64_t resourceId = 0;
            RenderResourceKind resourceKind = RenderResourceKind::Shader;
            std::shared_ptr<const ShaderResource> shader;
            std::shared_ptr<const MeshResource> mesh;
            std::shared_ptr<const TextureResource> texture;
            std::vector<DrawCommand> draws;
        };
        std::unique_ptr<IRenderBackend> m_backend;
        std::thread m_thread;
        std::mutex m_shutdownMutex;
        mutable std::mutex m_mutex;
        std::condition_variable m_workReady;
        std::condition_variable m_progress;
        std::deque<Job> m_jobs;
        std::vector<std::weak_ptr<RenderResourceHandle::State>> m_resources;
        std::exception_ptr m_failure;
        RenderCapabilities m_capabilities;
        uint64_t m_nextSequence = 1;
        uint64_t m_completedSequence = 0;
        uint64_t m_nextResource = 1;
        size_t m_pendingFrames = 0;
        size_t m_maxPendingFrames = 2;
        bool m_stop = false;
        bool m_started = false;

        void run();
        void rethrowFailure() const;

    public:
        explicit RenderDevice(std::unique_ptr<IRenderBackend> backend, size_t maxPendingFrames = 2);
        ~RenderDevice();
        RenderDevice(const RenderDevice&) = delete;
        RenderDevice& operator=(const RenderDevice&) = delete;

        RenderCapabilities capabilities() const;
        RenderResourceHandle createShader(const IShader& shader);
        RenderResourceHandle createMesh(const SerializedBufferView& vertices, const VertexLayout& layout);
        RenderResourceHandle createTexture(const Texture2D& texture);
        void updateMesh(const RenderResourceHandle& handle, const SerializedBufferView& vertices,
            const VertexLayout& layout);
        void updateTexture(const RenderResourceHandle& handle, const Texture2D& texture);
        void destroy(RenderResourceHandle& handle);
        std::shared_ptr<RenderCommandBuffer> makeFrame() const;
        uint64_t submit(const std::shared_ptr<RenderCommandBuffer>& frame);
        void wait(uint64_t ticket);
        void waitIdle();
        void shutdown();
    };
}
