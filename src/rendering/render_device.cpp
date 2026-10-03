#include "render_device.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace rendering
{
    struct RenderResourceHandle::State
    {
        uint64_t id;
        RenderResourceKind kind;
        const RenderDevice* owner;
        std::atomic<bool> alive { true };

        State(uint64_t resourceId, RenderResourceKind resourceKind, const RenderDevice* device)
            : id(resourceId), kind(resourceKind), owner(device) {}
    };

    uint64_t RenderResourceHandle::id() const { return m_state ? m_state->id : 0; }
    RenderResourceKind RenderResourceHandle::kind() const
    { return m_state ? m_state->kind : RenderResourceKind::Shader; }
    bool RenderResourceHandle::valid() const
    { return m_state && m_state->alive.load(std::memory_order_acquire); }

    namespace
    {
        constexpr std::array<RenderFeature, 6> all_features {
            RenderFeature::BackendAvailable, RenderFeature::CpuParallelRecording,
            RenderFeature::GpuParallelEncoding, RenderFeature::PortabilitySubset,
            RenderFeature::SampledTextures, RenderFeature::DepthAttachment
        };

        void validate_draw(const DrawCommand& draw)
        {
            if (draw.state.blend != BlendMode::Opaque && draw.state.blend != BlendMode::StraightAlpha)
                throw std::invalid_argument("Unknown draw blend mode");
            for (const auto* rect : { &draw.state.viewport, &draw.state.scissor })
                if (*rect && (!(*rect)->width || !(*rect)->height ||
                    (*rect)->x > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
                    (*rect)->y > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())))
                    throw std::invalid_argument("Draw viewport/scissor requires nonempty bounded pixel rectangle");
            if (!draw.shader.valid() || draw.shader.kind() != RenderResourceKind::Shader)
                throw std::invalid_argument("Draw requires a live shader handle");
            if (draw.mesh.id() && (!draw.mesh.valid() || draw.mesh.kind() != RenderResourceKind::Mesh))
                throw std::invalid_argument("Draw has an invalid mesh handle");
            if (draw.texture.id() && (!draw.texture.valid() || draw.texture.kind() != RenderResourceKind::Texture || !draw.mesh.id()))
                throw std::invalid_argument("Textured draw requires a live mesh and texture");
            for (const auto& uniform : draw.uniforms)
                if (uniform.name.empty() || uniform.bytes.empty())
                    throw std::invalid_argument("Draw uniforms require a name and bytes");
        }
    }

    RenderFeatureStatus RenderCapabilities::feature(RenderFeature requested) const
    {
        const auto index = static_cast<size_t>(requested);
        if (index >= all_features.size()) throw std::invalid_argument("Unknown render feature");
        bool supported = false;
        const char* name = "";
        const char* fallback = "Backend does not support this feature";
        switch (requested)
        {
            case RenderFeature::BackendAvailable:
                name = "backend"; supported = backendReady;
                fallback = "No rendering backend is initialized"; break;
            case RenderFeature::CpuParallelRecording:
                name = "cpu-parallel-recording"; supported = backendReady;
                fallback = "No rendering backend is initialized"; break;
            case RenderFeature::GpuParallelEncoding:
                name = "gpu-parallel-encoding"; supported = backendReady && parallelRecording;
                fallback = "GPU commands are encoded on one render thread"; break;
            case RenderFeature::PortabilitySubset:
                name = "portability-subset"; supported = backendReady && portabilitySubset;
                fallback = "Selected device does not expose VK_KHR_portability_subset"; break;
            case RenderFeature::SampledTextures:
                name = "sampled-textures"; supported = backendReady && sampledTextures;
                fallback = "Backend does not support sampled textures"; break;
            case RenderFeature::DepthAttachment:
                name = "depth-attachment"; supported = backendReady && depthAttachment;
                fallback = "Backend does not support depth attachments"; break;
        }
        std::string reason;
        if (!supported)
            reason = !backendReady && requested != RenderFeature::BackendAvailable
                ? "No rendering backend is initialized"
                : (unsupportedReasons[index].empty() ? fallback : unsupportedReasons[index]);
        return {requested, name, supported, std::move(reason)};
    }

    std::string RenderCapabilities::diagnostics() const
    {
        std::ostringstream output;
        for (const auto requested : all_features)
        {
            const auto status = feature(requested);
            output << status.name << ": " << (status.supported ? "supported" : "unsupported");
            if (!status.supported) output << " (" << status.reason << ')';
            output << '\n';
        }
        return output.str();
    }

    uint64_t RenderCommandBuffer::record(DrawCommand draw)
    {
        std::lock_guard lock(m_mutex);
        if (m_sealed) throw std::logic_error("Cannot record a submitted render frame");
        if (m_explicitOrder || draw.stableOrder)
            throw std::logic_error("Cannot mix serial and keyed draws in one render frame");
        validate_draw(draw);
        draw.order = m_nextOrder;
        const uint64_t order = draw.order;
        m_draws.push_back(std::move(draw));
        ++m_nextOrder;
        return order;
    }

    void RenderCommandBuffer::record(DrawCommand draw, DrawOrderKey key)
    {
        std::lock_guard lock(m_mutex);
        if (m_sealed) throw std::logic_error("Cannot record a submitted render frame");
        if (m_nextOrder != 0)
            throw std::logic_error("Cannot mix serial and keyed draws in one render frame");
        if (draw.stableOrder && *draw.stableOrder != key)
            throw std::invalid_argument("Draw has a conflicting stable order key");
        validate_draw(draw);
        if (!m_orderKeys.insert(key).second)
            throw std::invalid_argument("Duplicate draw order key");
        draw.stableOrder = key;
        try { m_draws.push_back(std::move(draw)); }
        catch (...) { m_orderKeys.erase(key); throw; }
        m_explicitOrder = true;
    }

    size_t RenderCommandBuffer::size() const
    {
        std::lock_guard lock(m_mutex);
        return m_draws.size();
    }

    namespace
    {
        MeshResource copy_mesh(const SerializedBufferView& view, const VertexLayout& layout)
        {
            if (view.empty() || !view.data || !layout.valid() || view.elementStride != layout.stride ||
                view.elementCount > std::numeric_limits<uint32_t>::max() ||
                view.elementCount > std::numeric_limits<size_t>::max() / layout.stride ||
                view.byteSize != view.elementCount * layout.stride)
                throw std::invalid_argument("Mesh bytes do not match vertex layout");
            MeshResource result;
            result.vertices.assign(view.data, view.data + view.byteSize);
            result.layout = layout;
            result.vertexCount = static_cast<uint32_t>(view.elementCount);
            return result;
        }
    }

    RenderDevice::RenderDevice(std::unique_ptr<IRenderBackend> backend, size_t maxPendingFrames)
        : m_backend(std::move(backend)), m_maxPendingFrames(maxPendingFrames)
    {
        if (!m_backend || maxPendingFrames == 0)
            throw std::invalid_argument("RenderDevice requires a backend and at least one queued frame");
        m_thread = std::thread(&RenderDevice::run, this);
        std::unique_lock lock(m_mutex);
        m_progress.wait(lock, [this] { return m_started; });
        if (m_failure)
        {
            const auto error = m_failure;
            lock.unlock();
            m_thread.join();
            std::rethrow_exception(error);
        }
    }

    RenderDevice::~RenderDevice()
    {
        try { shutdown(); } catch (...) {}
    }

    void RenderDevice::rethrowFailure() const
    {
        if (m_failure) std::rethrow_exception(m_failure);
    }

    RenderCapabilities RenderDevice::capabilities() const
    {
        std::lock_guard lock(m_mutex);
        return m_capabilities;
    }

    RenderResourceHandle RenderDevice::createShader(const IShader& shader)
    {
        auto resource = std::make_shared<ShaderResource>();
        resource->name = shader.name();
        for (const auto& source : shader.sources()) resource->sources.push_back(source);
        if (resource->sources.empty()) throw std::invalid_argument("Shader has no sources");
        std::lock_guard lock(m_mutex);
        rethrowFailure();
        if (m_stop) throw std::logic_error("RenderDevice is shut down");
        const uint64_t id = m_nextResource++;
        auto state = std::make_shared<RenderResourceHandle::State>(id, RenderResourceKind::Shader, this);
        m_resources.push_back(state);
        Job job;
        job.kind = Job::Kind::CreateShader;
        job.sequence = m_nextSequence++;
        job.resourceId = id;
        job.shader = std::move(resource);
        m_jobs.push_back(std::move(job));
        m_workReady.notify_one();
        return RenderResourceHandle(std::move(state));
    }

    RenderResourceHandle RenderDevice::createMesh(const SerializedBufferView& vertices, const VertexLayout& layout)
    {
        auto resource = std::make_shared<MeshResource>(copy_mesh(vertices, layout));
        std::lock_guard lock(m_mutex);
        rethrowFailure();
        if (m_stop) throw std::logic_error("RenderDevice is shut down");
        const uint64_t id = m_nextResource++;
        auto state = std::make_shared<RenderResourceHandle::State>(id, RenderResourceKind::Mesh, this);
        m_resources.push_back(state);
        Job job;
        job.kind = Job::Kind::CreateMesh;
        job.sequence = m_nextSequence++;
        job.resourceId = id;
        job.mesh = std::move(resource);
        m_jobs.push_back(std::move(job));
        m_workReady.notify_one();
        return RenderResourceHandle(std::move(state));
    }

    RenderResourceHandle RenderDevice::createTexture(const Texture2D& texture)
    {
        if (!texture.valid()) throw std::invalid_argument("Texture requires complete RGBA8 pixels");
        auto resource = std::make_shared<TextureResource>(TextureResource{texture});
        std::lock_guard lock(m_mutex);
        rethrowFailure();
        if (m_stop) throw std::logic_error("RenderDevice is shut down");
        const uint64_t id = m_nextResource++;
        auto state = std::make_shared<RenderResourceHandle::State>(id, RenderResourceKind::Texture, this);
        m_resources.push_back(state);
        Job job;
        job.kind = Job::Kind::CreateTexture;
        job.sequence = m_nextSequence++;
        job.resourceId = id;
        job.texture = std::move(resource);
        m_jobs.push_back(std::move(job));
        m_workReady.notify_one();
        return RenderResourceHandle(std::move(state));
    }

    void RenderDevice::updateMesh(const RenderResourceHandle& handle,
        const SerializedBufferView& vertices, const VertexLayout& layout)
    {
        auto resource = std::make_shared<MeshResource>(copy_mesh(vertices, layout));
        std::lock_guard lock(m_mutex);
        rethrowFailure();
        if (!handle.valid() || handle.kind() != RenderResourceKind::Mesh || handle.m_state->owner != this)
            throw std::invalid_argument("Mesh update requires a live handle from this device");
        if (m_stop) throw std::logic_error("RenderDevice is shut down");
        Job job;
        job.kind = Job::Kind::UpdateMesh;
        job.sequence = m_nextSequence++;
        job.resourceId = handle.id();
        job.mesh = std::move(resource);
        m_jobs.push_back(std::move(job));
        m_workReady.notify_one();
    }

    void RenderDevice::updateTexture(const RenderResourceHandle& handle, const Texture2D& texture)
    {
        if (!texture.valid()) throw std::invalid_argument("Texture requires complete RGBA8 pixels");
        auto resource = std::make_shared<TextureResource>(TextureResource{texture});
        std::lock_guard lock(m_mutex);
        rethrowFailure();
        if (!handle.valid() || handle.kind() != RenderResourceKind::Texture || handle.m_state->owner != this)
            throw std::invalid_argument("Texture update requires a live handle from this device");
        if (m_stop) throw std::logic_error("RenderDevice is shut down");
        Job job;
        job.kind = Job::Kind::UpdateTexture;
        job.sequence = m_nextSequence++;
        job.resourceId = handle.id();
        job.texture = std::move(resource);
        m_jobs.push_back(std::move(job));
        m_workReady.notify_one();
    }

    void RenderDevice::destroy(RenderResourceHandle& handle)
    {
        std::lock_guard lock(m_mutex);
        rethrowFailure();
        if (!handle.valid() || handle.m_state->owner != this)
            throw std::invalid_argument("Destroy requires a live handle from this device");
        if (m_stop) throw std::logic_error("RenderDevice is shut down");
        handle.m_state->alive.store(false, std::memory_order_release);
        Job job;
        job.kind = Job::Kind::Destroy;
        job.sequence = m_nextSequence++;
        job.resourceId = handle.id();
        job.resourceKind = handle.kind();
        m_jobs.push_back(std::move(job));
        handle = {};
        m_workReady.notify_one();
    }

    std::shared_ptr<RenderCommandBuffer> RenderDevice::makeFrame() const
    { return std::make_shared<RenderCommandBuffer>(); }

    uint64_t RenderDevice::submit(const std::shared_ptr<RenderCommandBuffer>& frame)
    {
        if (!frame) throw std::invalid_argument("Cannot submit a null render frame");
        std::lock_guard frameLock(frame->m_mutex);
        if (frame->m_sealed) throw std::logic_error("Render frame has already been submitted");
        std::unique_lock lock(m_mutex);
        m_progress.wait(lock, [this] { return m_pendingFrames < m_maxPendingFrames || m_stop || m_failure; });
        rethrowFailure();
        if (m_stop) throw std::logic_error("RenderDevice is shut down");
        for (const auto& draw : frame->m_draws)
        {
            for (const auto* handle : { &draw.shader, &draw.mesh, &draw.texture })
                if (handle->id() && (!handle->valid() || handle->m_state->owner != this))
                    throw std::invalid_argument("Render frame refers to a destroyed or foreign resource");
        }
        Job job;
        job.kind = Job::Kind::Frame;
        job.sequence = m_nextSequence++;
        job.draws = std::move(frame->m_draws);
        if (frame->m_explicitOrder)
        {
            std::sort(job.draws.begin(), job.draws.end(), [](const auto& left, const auto& right) {
                return *left.stableOrder < *right.stableOrder;
            });
            for (size_t index = 0; index < job.draws.size(); ++index)
                job.draws[index].order = static_cast<uint64_t>(index);
        }
        frame->m_sealed = true;
        ++m_pendingFrames;
        const uint64_t ticket = job.sequence;
        m_jobs.push_back(std::move(job));
        m_workReady.notify_one();
        return ticket;
    }

    void RenderDevice::wait(uint64_t ticket)
    {
        std::unique_lock lock(m_mutex);
        if (ticket == 0 || ticket >= m_nextSequence)
            throw std::invalid_argument("Unknown render ticket");
        m_progress.wait(lock, [this, ticket] { return m_completedSequence >= ticket || m_failure; });
        rethrowFailure();
    }

    void RenderDevice::waitIdle()
    {
        uint64_t ticket;
        {
            std::lock_guard lock(m_mutex);
            rethrowFailure();
            if (m_stop) return;
            Job job;
            job.kind = Job::Kind::Barrier;
            job.sequence = m_nextSequence++;
            ticket = job.sequence;
            m_jobs.push_back(std::move(job));
            m_workReady.notify_one();
        }
        wait(ticket);
    }

    void RenderDevice::shutdown()
    {
        std::lock_guard shutdownLock(m_shutdownMutex);
        if (!m_thread.joinable()) return;
        try { waitIdle(); } catch (...) {}
        {
            std::lock_guard lock(m_mutex);
            m_stop = true;
            m_workReady.notify_one();
            m_progress.notify_all();
        }
        m_thread.join();
        std::lock_guard lock(m_mutex);
        for (auto& weak : m_resources)
            if (auto state = weak.lock()) state->alive.store(false, std::memory_order_release);
        rethrowFailure();
    }

    void RenderDevice::run()
    {
        try
        {
            m_backend->initialize();
            {
                std::lock_guard lock(m_mutex);
                m_capabilities = m_backend->capabilities();
                m_started = true;
                m_progress.notify_all();
            }
            for (;;)
            {
                Job job;
                {
                    std::unique_lock lock(m_mutex);
                    m_workReady.wait(lock, [this] { return m_stop || !m_jobs.empty(); });
                    if (m_jobs.empty() && m_stop) break;
                    job = std::move(m_jobs.front());
                    m_jobs.pop_front();
                }
                switch (job.kind)
                {
                    case Job::Kind::CreateShader: m_backend->createShader(job.resourceId, *job.shader); break;
                    case Job::Kind::CreateMesh: m_backend->createMesh(job.resourceId, *job.mesh); break;
                    case Job::Kind::CreateTexture: m_backend->createTexture(job.resourceId, *job.texture); break;
                    case Job::Kind::UpdateMesh: m_backend->updateMesh(job.resourceId, *job.mesh); break;
                    case Job::Kind::UpdateTexture: m_backend->updateTexture(job.resourceId, *job.texture); break;
                    case Job::Kind::Destroy:
                        m_backend->waitIdle();
                        m_backend->destroyResource(job.resourceId, job.resourceKind);
                        break;
                    case Job::Kind::Frame: m_backend->executeFrame(job.sequence, job.draws); break;
                    case Job::Kind::Barrier: m_backend->waitIdle(); break;
                }
                {
                    std::lock_guard lock(m_mutex);
                    m_completedSequence = job.sequence;
                    if (job.kind == Job::Kind::Frame) --m_pendingFrames;
                    m_progress.notify_all();
                }
            }
        }
        catch (...)
        {
            std::lock_guard lock(m_mutex);
            m_failure = std::current_exception();
            m_started = true;
            m_stop = true;
            m_jobs.clear();
            m_progress.notify_all();
        }
        try { m_backend->shutdown(); }
        catch (...)
        {
            std::lock_guard lock(m_mutex);
            if (!m_failure) m_failure = std::current_exception();
            m_progress.notify_all();
        }
    }
}
