#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "rendering/render_device.h"
#include "test/test_assertions.h"

namespace
{
    struct Stats
    {
        std::mutex mutex;
        std::thread::id owner;
        unsigned shaders = 0, meshes = 0, textures = 0;
        unsigned meshUpdates = 0, textureUpdates = 0;
        unsigned meshDeletes = 0, textureDeletes = 0, frames = 0;
        std::vector<uint64_t> frameTickets;
        std::vector<std::vector<rendering::DrawCommand>> draws;
        std::mutex gateMutex;
        std::condition_variable gate;
        bool holdNextFrame = false;
        bool frameEntered = false;
        bool releaseFrame = false;
        bool failNextFrame = false;
    };

    class FakeBackend final : public rendering::IRenderBackend
    {
        std::shared_ptr<Stats> stats;
        void assertOwner() const
        { test::require(std::this_thread::get_id() == stats->owner, "backend called from non-owner thread"); }
    public:
        explicit FakeBackend(std::shared_ptr<Stats> data) : stats(std::move(data)) {}
        rendering::RenderCapabilities capabilities() const override
        {
            rendering::RenderCapabilities result { "fake", false, 1, false };
            result.backendReady = true;
            return result;
        }
        void initialize() override { stats->owner = std::this_thread::get_id(); }
        void createShader(uint64_t, const rendering::ShaderResource& source) override
        { assertOwner(); test::require(!source.sources.empty(), "shader upload missing"); ++stats->shaders; }
        void createMesh(uint64_t, const rendering::MeshResource& mesh) override
        { assertOwner(); test::require(mesh.vertices.size() == 24, "mesh upload missing"); ++stats->meshes; }
        void createTexture(uint64_t, const rendering::TextureResource& image) override
        { assertOwner(); test::require(image.image.valid(), "texture upload missing"); ++stats->textures; }
        void updateMesh(uint64_t, const rendering::MeshResource&) override
        { assertOwner(); ++stats->meshUpdates; }
        void updateTexture(uint64_t, const rendering::TextureResource&) override
        { assertOwner(); ++stats->textureUpdates; }
        void destroyResource(uint64_t, rendering::RenderResourceKind kind) override
        {
            assertOwner();
            if (kind == rendering::RenderResourceKind::Mesh) ++stats->meshDeletes;
            if (kind == rendering::RenderResourceKind::Texture) ++stats->textureDeletes;
        }
        void executeFrame(uint64_t frame, const std::vector<rendering::DrawCommand>& draws) override
        {
            assertOwner();
            {
                std::unique_lock gateLock(stats->gateMutex);
                if (stats->holdNextFrame)
                {
                    stats->holdNextFrame = false;
                    stats->frameEntered = true;
                    stats->gate.notify_all();
                    stats->gate.wait(gateLock, [this] { return stats->releaseFrame; });
                }
            }
            if (stats->failNextFrame)
            {
                stats->failNextFrame = false;
                throw std::runtime_error("simulated backend failure");
            }
            ++stats->frames;
            stats->frameTickets.push_back(frame);
            stats->draws.push_back(draws);
        }
        void waitIdle() override { assertOwner(); }
        void shutdown() override { assertOwner(); }
    };

    class FakeShader final : public rendering::IShader
    {
    public:
        FakeShader() : IShader("concurrent-test")
        {
            rendering::ShaderSource source;
            source.stage = rendering::ShaderStage::Vertex;
            source.language = rendering::ShaderLanguage::Spirv;
            source.bytes = { 1, 2, 3, 4 };
            addSource(std::move(source));
        }
        std::string_view backendName() const override { return "fake"; }
    };
}

void test_render_device_concurrency()
{
    test::require_throws([] { rendering::RenderDevice device(nullptr); },
        "null render backend was accepted");
    test::require_throws([] {
        auto data = std::make_shared<Stats>();
        rendering::RenderDevice device(std::make_unique<FakeBackend>(data), 0);
    }, "zero-sized frame queue was accepted");

    auto stats = std::make_shared<Stats>();
    rendering::RenderDevice device(std::make_unique<FakeBackend>(stats), 2);
    test::require(device.capabilities().backendName == "fake", "backend capabilities not reported");
    test::require(!device.capabilities().parallelRecording &&
        device.capabilities().maxFramesInFlight == 1,
        "backend capabilities misreport GPU recording or frame capacity");
    const auto capabilities = device.capabilities();
    using rendering::RenderFeature;
    test::require(capabilities.feature(RenderFeature::BackendAvailable).supported &&
        capabilities.feature(RenderFeature::CpuParallelRecording).supported,
        "available backend or CPU recording reported unavailable");
    for (const auto feature : {RenderFeature::GpuParallelEncoding,
        RenderFeature::PortabilitySubset, RenderFeature::SampledTextures,
        RenderFeature::DepthAttachment})
    {
        const auto status = capabilities.feature(feature);
        test::require(!status.supported && !status.reason.empty() && status.name[0],
            "unsupported feature has no typed diagnostic reason");
    }
    test::require(capabilities.diagnostics().find("sampled-textures: unsupported") != std::string::npos &&
        capabilities.diagnostics().find("depth-attachment: unsupported") != std::string::npos,
        "capability report omitted unsupported rendering features");
    const rendering::RenderCapabilities disabled;
    test::require(!disabled.feature(RenderFeature::BackendAvailable).supported &&
        disabled.feature(RenderFeature::SampledTextures).reason == "No rendering backend is initialized",
        "disabled backend did not explain unavailable features");
    test::require(stats->owner != std::this_thread::get_id(), "backend initialized on caller thread");

    FakeShader shaderSource;
    auto shader = device.createShader(shaderSource);
    const rendering::VertexLayout layout { sizeof(float) * 2,
        { {0, rendering::VertexAttributeFormat::Float2, 0} } };
    const std::array<std::array<float, 2>, 3> meshValues {{{0,0}, {1,0}, {0,1}}};
    auto mesh = device.createMesh(rendering::serialized_buffer_view(meshValues.data(), meshValues.size()), layout);
    auto texture = device.createTexture({ 1, 1, {255, 255, 255, 255} });
    test::require_throws([&] { device.createTexture({1, 1, {255, 255, 255}}); },
        "incomplete texture was accepted");
    test::require_throws([&] {
        device.createMesh(rendering::serialized_buffer_view(meshValues.data(), meshValues.size()),
            rendering::VertexLayout {sizeof(float), {{0, rendering::VertexAttributeFormat::Float2, 0}}});
    }, "mesh with mismatched vertex stride was accepted");
    test::require_throws([&] { device.wait(0); }, "zero render ticket was accepted");

    auto first = device.makeFrame();
    constexpr int producers = 4, drawsPerProducer = 25;
    std::vector<std::thread> threads;
    for (int producer = 0; producer < producers; ++producer)
        threads.emplace_back([&, producer] {
            for (int index = 0; index < drawsPerProducer; ++index)
            {
                rendering::DrawCommand draw { shader, mesh, texture };
                const int value = producer * drawsPerProducer + index;
                rendering::appendUniform(draw, "value", value, {0, 0});
                first->record(std::move(draw), {static_cast<uint64_t>(producer),
                    static_cast<uint64_t>(index)});
            }
        });
    for (auto& thread : threads) thread.join();
    test::require_throws([&] { first->record({shader, mesh, texture}, {0,0}); },
        "duplicate concurrent draw key was accepted");
    test::require_throws([&] { first->record({shader, mesh, texture}); },
        "serial draw was mixed into a keyed frame");
    const auto firstTicket = device.submit(first);
    test::require_throws([&] { device.submit(first); }, "submitted frame was accepted twice");
    test::require_throws([&] { first->record({shader, mesh, texture}); },
        "submitted frame accepted another draw");
    auto second = device.makeFrame();
    second->record({ shader, mesh, texture });
    const auto secondTicket = device.submit(second);
    device.wait(secondTicket);
    test::require(firstTicket < secondTicket, "frame tickets are not monotonic");
    test::require(stats->frames == 2 && stats->frameTickets[0] == firstTicket &&
        stats->frameTickets[1] == secondTicket, "frames did not execute in ticket order");
    test::require(stats->shaders == 1 && stats->meshes == 1 && stats->textures == 1,
        "static assets were uploaded more than once");
    test::require(stats->draws[0].size() == producers * drawsPerProducer,
        "parallel producers lost draw commands");
    std::set<int> values;
    for (size_t index = 0; index < stats->draws[0].size(); ++index)
    {
        const auto& draw = stats->draws[0][index];
        test::require(draw.order == index, "concurrent draw merge lost stable order");
        int value = -1;
        std::memcpy(&value, draw.uniforms[0].bytes.data(), sizeof(value));
        test::require(value == static_cast<int>(index),
            "keyed concurrent draw merge depended on mutex acquisition order");
        values.insert(value);
    }
    test::require(values.size() == producers * drawsPerProducer, "concurrent draw payloads corrupted");
    auto mixed = device.makeFrame();
    mixed->record({shader, mesh, texture});
    test::require_throws([&] { mixed->record({shader, mesh, texture}, {0,0}); },
        "keyed draw was mixed into a serial frame");

    // Vary arrival order across rounds; producer/draw keys define one identical
    // GPU command order regardless of scheduler timing.
    for (int round = 0; round < 6; ++round)
    {
        auto repeat = device.makeFrame();
        std::vector<std::thread> repeatThreads;
        for (int producer = producers - 1; producer >= 0; --producer)
            repeatThreads.emplace_back([&, producer, round] {
                for (int index = 0; index < drawsPerProducer; ++index)
                {
                    if ((round * 13 + producer * 7 + index * 3) % 11 == 0)
                        std::this_thread::sleep_for(std::chrono::microseconds(30 + round * 7));
                    rendering::DrawCommand draw {shader, mesh, texture};
                    const int value = producer * drawsPerProducer + index;
                    rendering::appendUniform(draw, "value", value, {0,0});
                    repeat->record(std::move(draw), {static_cast<uint64_t>(producer),
                        static_cast<uint64_t>(index)});
                }
            });
        for (auto& thread : repeatThreads) thread.join();
        device.wait(device.submit(repeat));
        const auto& merged = stats->draws.back();
        test::require(merged.size() == producers * drawsPerProducer,
            "repeated concurrent frame lost commands");
        for (size_t index = 0; index < merged.size(); ++index)
        {
            int value = -1;
            std::memcpy(&value, merged[index].uniforms[0].bytes.data(), sizeof(value));
            test::require(merged[index].order == index && value == static_cast<int>(index),
                "repeated concurrent frame changed deterministic command order");
        }
    }

    // A slow backend must stop a third submission until one of two in-flight
    // frames completes; recording itself remains free to run on worker jobs.
    {
        std::lock_guard gateLock(stats->gateMutex);
        stats->holdNextFrame = true;
    }
    auto held = device.makeFrame();
    held->record({shader, mesh, texture});
    const auto heldTicket = device.submit(held);
    {
        std::unique_lock gateLock(stats->gateMutex);
        stats->gate.wait(gateLock, [&] { return stats->frameEntered; });
    }
    auto queued = device.makeFrame();
    queued->record({shader, mesh, texture});
    device.submit(queued);
    auto blocked = device.makeFrame();
    blocked->record({shader, mesh, texture});
    std::atomic<bool> started {false}, accepted {false};
    uint64_t blockedTicket = 0;
    std::thread submitter([&] {
        started.store(true, std::memory_order_release);
        blockedTicket = device.submit(blocked);
        accepted.store(true, std::memory_order_release);
    });
    while (!started.load(std::memory_order_acquire)) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const bool admittedEarly = accepted.load(std::memory_order_acquire);
    {
        std::lock_guard gateLock(stats->gateMutex);
        stats->releaseFrame = true;
        stats->gate.notify_all();
    }
    submitter.join();
    test::require(!admittedEarly, "bounded frame queue accepted a third in-flight frame");
    test::require(blockedTicket > heldTicket, "blocked submission lost ticket ordering");
    device.wait(blockedTicket);

    device.updateMesh(mesh, rendering::serialized_buffer_view(meshValues.data(), meshValues.size()), layout);
    device.updateTexture(texture, {1, 1, {0, 0, 0, 255}});
    auto third = device.makeFrame();
    third->record({shader, mesh, texture});
    const auto thirdTicket = device.submit(third);
    const auto staleMesh = mesh;
    device.destroy(mesh);
    device.destroy(texture);
    device.waitIdle();
    test::require(stats->meshUpdates == 1 && stats->textureUpdates == 1,
        "mutable assets were not uploaded exactly once per update");
    test::require(stats->meshDeletes == 1 && stats->textureDeletes == 1,
        "resources were not released after queued frames");
    test::require(stats->frameTickets.back() == thirdTicket,
        "resource destroy overtook a queued frame");
    test::require(!mesh.valid() && !texture.valid(), "destroyed resource handle remains live");
    test::require(!staleMesh.valid(), "copied handle remained live after destroy");
    test::require_throws([&] {
        device.updateMesh(staleMesh, rendering::serialized_buffer_view(meshValues.data(), meshValues.size()), layout);
    }, "destroyed mesh handle was accepted by update");
    test::require_throws([&] {
        auto invalid = device.makeFrame();
        invalid->record({shader, staleMesh, {}});
    }, "stale mesh handle was accepted by frame recorder");
    {
        auto foreignStats = std::make_shared<Stats>();
        rendering::RenderDevice foreign(std::make_unique<FakeBackend>(foreignStats));
        auto foreignShader = foreign.createShader(shaderSource);
        auto foreignFrame = device.makeFrame();
        foreignFrame->record({foreignShader, {}, {}});
        test::require_throws([&] { device.submit(foreignFrame); },
            "foreign render resource was accepted by submit");
        test::require_throws([&] { device.destroy(foreignShader); },
            "foreign resource was accepted by destroy");
        foreign.destroy(foreignShader);
        foreign.shutdown();
    }
    const auto retainedShader = shader;
    device.destroy(shader);
    device.shutdown();
    device.shutdown();
    test::require(!retainedShader.valid(), "shader remained live after shutdown");
    test::require_throws([&] { device.createShader(shaderSource); },
        "shut-down render device accepted a new resource");
    test::require_throws([&] { device.submit(device.makeFrame()); },
        "shut-down render device accepted a new frame");

    auto failingStats = std::make_shared<Stats>();
    rendering::RenderDevice failing(std::make_unique<FakeBackend>(failingStats));
    auto failingShader = failing.createShader(shaderSource);
    failingStats->failNextFrame = true;
    auto failingFrame = failing.makeFrame();
    failingFrame->record({failingShader, {}, {}});
    const auto failingTicket = failing.submit(failingFrame);
    test::require_throws([&] { failing.wait(failingTicket); },
        "backend frame failure was not reported to ticket waiter");
    test::require_throws([&] { failing.submit(failing.makeFrame()); },
        "failed backend accepted another frame");
    test::require_throws([&] { failing.shutdown(); },
        "backend failure was lost during shutdown");
}
