// Standalone GPU smoke test; requires compiled sample shaders and a Vulkan device.
#include "rendering/camera.h"
#include "rendering/material.h"
#include "rendering/render_device.h"
#include "rendering/sprite2d.h"
#include "rendering/text2d.h"
#include "vulcan/vulkan_frame_backend.h"
#include "vulcan/vulkan_renderer.h"

#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

#ifdef CPP_GAME_ENGINE_USE_VULKAN
namespace
{
    std::atomic<unsigned> validationErrors = 0;
    VKAPI_ATTR VkBool32 VKAPI_CALL report(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT, uint64_t,
                                          size_t, int32_t, const char*, const char* text, void*)
    {
        if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
            ++validationErrors;
        std::cerr << "Vulkan validation: " << text << '\n';
        return VK_FALSE;
    }

    template <class Function> void expectFailure(Function&& function)
    {
        try
        {
            function();
        }
        catch (const std::exception&)
        {
            return;
        }
        throw std::runtime_error("Expected invalid Vulkan operation to fail");
    }

    vulkan::VulkanShaderProgram makeShader(const std::filesystem::path& directory)
    {
        vulkan::VulkanShaderProgram shader("smoke");
        shader.addSpirv(rendering::ShaderStage::Vertex, directory / "triangle.vert.spv");
        shader.addSpirv(rendering::ShaderStage::Fragment, directory / "triangle.frag.spv");
        return shader;
    }

    vulkan::VulkanShaderProgram makeMeshShader(const std::filesystem::path& directory)
    {
        vulkan::VulkanShaderProgram shader("mesh-smoke");
        shader.addSpirv(rendering::ShaderStage::Vertex, directory / "mesh_textured.vert.spv");
        shader.addSpirv(rendering::ShaderStage::Fragment, directory / "mesh_textured.frag.spv");
        return shader;
    }

    vulkan::VulkanShaderProgram makeBufferMeshShader(const std::filesystem::path& directory)
    {
        vulkan::VulkanShaderProgram shader("mesh-buffer-smoke");
        shader.addSpirv(rendering::ShaderStage::Vertex, directory / "mesh.vert.spv");
        shader.addSpirv(rendering::ShaderStage::Fragment, directory / "triangle.frag.spv");
        return shader;
    }

    void check(VkResult result, const char* operation)
    {
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
            throw std::runtime_error(std::string(operation) + ": " + std::to_string(result));
    }

    // Uses only public native handles; does not alter the renderer's frame state.
    void verifyPixels(vulkan::VulkanRenderer& renderer, const std::vector<bool>& drawn,
                      const rendering::Camera2DView& view)
    {
        const auto support =
            vulkan::VulkanSwapchain::querySupport(renderer.physicalDevice(), renderer.surface());
        const auto& swapchain = renderer.swapchain();
        const auto format = swapchain.imageFormat();
        if (!(support.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ||
            (format != VK_FORMAT_B8G8R8A8_SRGB && format != VK_FORMAT_R8G8B8A8_SRGB))
        {
            throw std::runtime_error("2D GPU readback requires SRGB transfer-source surface");
        }
        struct CopyResources
        {
            VkDevice device;
            VkQueue graphicsQueue;
            VkCommandPool pool = {};
            VkSemaphore acquired = {}, copied = {};
            VkFence complete = {};
            bool acquiredPending = false;
            std::unique_ptr<vulkan::VulkanBuffer> buffer;
            ~CopyResources()
            {
                if (acquiredPending)
                {
                    const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                    VkSubmitInfo consume{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                    consume.waitSemaphoreCount = 1;
                    consume.pWaitSemaphores = &acquired;
                    consume.pWaitDstStageMask = &stage;
                    vkQueueSubmit(graphicsQueue, 1, &consume, VK_NULL_HANDLE);
                }
                // Includes presentation's semaphore wait before destroying copied.
                vkDeviceWaitIdle(device);
                if (pool)
                    vkDestroyCommandPool(device, pool, nullptr);
                if (acquired)
                    vkDestroySemaphore(device, acquired, nullptr);
                if (copied)
                    vkDestroySemaphore(device, copied, nullptr);
                if (complete)
                    vkDestroyFence(device, complete, nullptr);
            }
        } resources{renderer.device(), renderer.graphicsQueue()};
        const auto device = renderer.device();
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.queueFamilyIndex = renderer.queueFamilies().graphicsFamily;
        check(vkCreateCommandPool(device, &pool, nullptr, &resources.pool), "Readback command pool");
        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(device, &semaphore, nullptr, &resources.acquired),
              "Readback acquire semaphore");
        check(vkCreateSemaphore(device, &semaphore, nullptr, &resources.copied),
              "Readback present semaphore");
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        check(vkCreateFence(device, &fence, nullptr, &resources.complete), "Readback fence");
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = resources.pool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = 1;
        VkCommandBuffer command{};
        check(vkAllocateCommandBuffers(device, &allocation, &command), "Readback command buffer");
        const auto extent = swapchain.extent();
        rendering::GpuBufferDescription description;
        description.byteSize = static_cast<size_t>(extent.width) * extent.height * 4;
        description.usage = rendering::GpuBufferUsage::TransferDestination;
        description.memoryUsage = rendering::GpuMemoryUsage::CpuToGpu; // host-visible and coherent
        resources.buffer = renderer.memoryManager().createVulkanBuffer(description);
        auto& buffer = resources.buffer;
        for (size_t attempt = 0; attempt < swapchain.images().size() * 2; ++attempt)
        {
            uint32_t index = 0;
            check(vkAcquireNextImageKHR(device, swapchain.handle(), UINT64_MAX, resources.acquired,
                                        VK_NULL_HANDLE, &index),
                  "Readback acquire");
            resources.acquiredPending = true;
            const bool known = index < drawn.size() && drawn[index];
            check(vkResetCommandPool(device, resources.pool, 0), "Readback reset");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            check(vkBeginCommandBuffer(command, &begin), "Readback begin");
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.oldLayout = known ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout =
                known ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            barrier.dstAccessMask = known ? VK_ACCESS_TRANSFER_READ_BIT : 0;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = swapchain.images()[index];
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                 0, nullptr, 0, nullptr, 1, &barrier);
            if (known)
            {
                VkBufferImageCopy region{};
                region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                region.imageExtent = {extent.width, extent.height, 1};
                vkCmdCopyImageToBuffer(command, barrier.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                       buffer->handle(), 1, &region);
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barrier.dstAccessMask = 0;
                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                     &barrier);
                VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                                     1, &host, 0, nullptr, 0, nullptr);
            }
            check(vkEndCommandBuffer(command), "Readback end");
            const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.waitSemaphoreCount = submit.signalSemaphoreCount = submit.commandBufferCount = 1;
            submit.pWaitSemaphores = &resources.acquired;
            submit.pWaitDstStageMask = &waitStage;
            submit.pCommandBuffers = &command;
            submit.pSignalSemaphores = &resources.copied;
            check(vkResetFences(device, 1, &resources.complete), "Readback reset fence");
            check(vkQueueSubmit(renderer.graphicsQueue(), 1, &submit, resources.complete), "Readback submit");
            resources.acquiredPending = false;
            const auto handle = swapchain.handle();
            VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
            present.waitSemaphoreCount = present.swapchainCount = 1;
            present.pWaitSemaphores = &resources.copied;
            present.pSwapchains = &handle;
            present.pImageIndices = &index;
            check(vkQueuePresentKHR(renderer.presentQueue(), &present), "Readback present");
            check(vkWaitForFences(device, 1, &resources.complete, VK_TRUE, UINT64_MAX), "Readback wait");
            check(vkDeviceWaitIdle(device), "Readback presentation wait");
            if (!known)
                continue; // Never infer contents of an untracked/recreated/cancelled image.

            void* mapped = nullptr;
            check(vkMapMemory(device, buffer->memory(), 0, description.byteSize, 0, &mapped), "Readback map");
            const auto* bytes = static_cast<const unsigned char*>(mapped);
            const bool bgra = format == VK_FORMAT_B8G8R8A8_SRGB;
            auto pixel = [&](uint32_t x, uint32_t y) {
                const auto* p = bytes + (static_cast<size_t>(y) * extent.width + x) * 4;
                return std::array<unsigned, 3>{p[bgra ? 2 : 0], p[1], p[bgra ? 0 : 2]};
            };
            const auto viewport = view.framebufferViewport;
            auto logicalPixel = [&](float x, float y) {
                return pixel(viewport.x + static_cast<uint32_t>(x * view.scale),
                             viewport.y + static_cast<uint32_t>(y * view.scale));
            };
            const auto blue = logicalPixel(10, 10), overlap = logicalPixel(32, 24),
                       glyph = logicalPixel(6, 6), clipped = logicalPixel(4, 4),
                       zeroAlpha = logicalPixel(55, 35);
            std::ofstream artifact("build/desktop2d/gpu-readback-" + std::to_string(extent.width) + ".ppm",
                                   std::ios::binary);
            artifact << "P6\n" << extent.width << ' ' << extent.height << "\n255\n";
            for (uint32_t y = 0; y < extent.height; ++y)
                for (uint32_t x = 0; x < extent.width; ++x)
                    for (auto channel : pixel(x, y))
                        artifact.put(static_cast<char>(channel));
            const bool passed = blue[0] < 5 && blue[1] < 5 && blue[2] > 250 && overlap[0] > 175 &&
                                overlap[0] < 200 && overlap[1] < 5 && overlap[2] > 175 && overlap[2] < 200 &&
                                glyph[0] > 250 && glyph[1] > 250 && glyph[2] > 250 && clipped[0] < 5 &&
                                clipped[2] > 250 && zeroAlpha[0] < 5 && zeroAlpha[1] < 5 &&
                                zeroAlpha[2] > 250;
            const auto bar = pixel(0, 0);
            const bool letterbox =
                !viewport.x && !viewport.y ? true : (bar[0] < 100 && bar[1] < 100 && bar[2] < 120);
            vkUnmapMemory(device, buffer->memory());
            std::cout << "2D readback " << extent.width << 'x' << extent.height << " scale=" << view.scale
                      << " overlap=" << overlap[0] << ',' << overlap[1] << ',' << overlap[2]
                      << " glyph=" << glyph[0] << ',' << glyph[1] << ',' << glyph[2] << '\n';
            if (!passed || !letterbox)
                throw std::runtime_error("2D GPU alpha/order/text clip/camera pixel assertion failed");
            return;
        }
        throw std::runtime_error("Readback did not reacquire a tracked rendered image");
    }

    void run2D(GLFWwindow* window, const std::filesystem::path& directory)
    {
        rendering::Camera2DView view;
        bool readback = false;
        unsigned trackedFrames = 0;
        VkSwapchainKHR trackedSwapchain = {};
        std::vector<bool> drawn;
        vulkan::VulkanGlfwSurfaceProvider surface(window);
        vulkan::VulkanRendererConfig config;
        config.enableValidationLayers = true;
        config.validationCallback = report;
        auto backend = std::make_unique<vulkan::VulkanFrameBackend>(
            surface, config, [&](vulkan::VulkanRenderer& renderer, uint32_t index) {
                if (trackedSwapchain != renderer.swapchain().handle())
                {
                    trackedSwapchain = renderer.swapchain().handle();
                    drawn.assign(renderer.swapchain().images().size(), false);
                    trackedFrames = 0;
                }
                drawn[index] = true;
                if (++trackedFrames >= drawn.size() * 2 && !readback)
                {
                    verifyPixels(renderer, drawn, view);
                    readback = true;
                }
            });
        rendering::RenderDevice device(std::move(backend));
        device.waitIdle();
        auto program = makeMeshShader(directory);
        auto shader = device.createShader(program);
        auto texture = [&](std::vector<uint8_t> rgba) {
            return device.createTexture({1, 1, std::move(rgba)});
        };
        auto blue = texture({0, 0, 255, 255}), red = texture({255, 0, 0, 128}),
             transparent = texture({0, 255, 0, 0}),
             white = device.createTexture({8, 8, std::vector<uint8_t>(8 * 8 * 4, 255)});
        auto mesh = [&](const rendering::MeshAsset& asset) {
            return device.createMesh(asset.vertexData(), asset.layout());
        };
        rendering::Sprite2DDescription full;
        full.size = {64, 48};
        full.pivot = {0, 0};
        full.source = {0, 0, 1, 1};
        auto background = mesh(rendering::makeSpriteQuad(full, 1, 1));
        full.position = {16, 8};
        full.size = {32, 32};
        auto middle = mesh(rendering::makeSpriteQuad(full, 1, 1));
        rendering::FontAtlas font{"asset:atlas", 10, 65, {{65, {{0, 0, 8, 8}, 9, {0, 0}}}}};
        auto geometry =
            rendering::makeTextGeometry(font, 8, 8, {"A", {4, 4}, 0, 0, rendering::LogicalRect{5, 5, 2, 2}});
        auto text = mesh(geometry.mesh);
        for (const auto size : std::array<std::array<int, 2>, 3>{{{320, 240}, {400, 300}, {32, 20}}})
        {
            device.waitIdle();
            glfwSetWindowSize(window, size[0], size[1]);
            glfwPollEvents();
            int width = 0, height = 0;
            glfwGetFramebufferSize(window, &width, &height);
            view = rendering::makeCamera2DView({{64, 48}, 1, {}, true}, width, height);
            readback = false;
            trackedFrames = 0;
            for (unsigned n = 0; n < 20 && !readback; ++n)
            {
                glfwPollEvents();
                auto frame = device.makeFrame();
                auto draw = [&](const rendering::RenderResourceHandle& m,
                                const rendering::RenderResourceHandle& t, uint64_t order) {
                    rendering::DrawCommand command;
                    command.shader = shader;
                    command.mesh = m;
                    command.texture = t;
                    command.state = rendering::painter2DState(view);
                    if (order == 0)
                    {
                        command.state.blend = rendering::BlendMode::Opaque;
                        command.state.depthTest = true;
                        command.state.depthWrite = true;
                    }
                    const rendering::CameraUniform camera = rendering::makeLogicalUiCamera(64, 48);
                    const rendering::CameraUniform identity;
                    const rendering::MaterialUniform material;
                    rendering::appendUniform(command, "transform", identity, {0, 0});
                    rendering::appendUniform(command, "color", material, {0, 1});
                    rendering::appendUniform(command, "camera", camera, {0, 2});
                    frame->record(std::move(command), {0, order});
                };
                // Reverse producer insertion deliberately; keyed painter order must win over call order.
                draw(middle, red, 1);
                draw(background, blue, 0);
                draw(background, transparent, 2);
                draw(text, white, 3);
                device.wait(device.submit(frame));
            }
            if (!readback)
                throw std::runtime_error("2D frame backend did not run GPU readback");
        }
        device.shutdown();
    }
} // namespace
#endif
int main(int argc, char** argv)
{
#ifdef CPP_GAME_ENGINE_USE_VULKAN
    GLFWwindow* window = nullptr;
    try
    {
        if (!glfwInit())
            throw std::runtime_error("GLFW failed");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        window = glfwCreateWindow(320, 240, "Desktop 2D GPU", nullptr, nullptr);
        if (!window)
            throw std::runtime_error("Window failed");
        run2D(window, argc > 1 ? argv[1] : "build/desktop2d-vk1/shaders");
        glfwDestroyWindow(window);
        window = nullptr;
        glfwTerminate();
        if (validationErrors)
            throw std::runtime_error("2D Vulkan validation errors");
        std::cout << "desktop2d GPU tests passed: alpha0, translucent overlap, deterministic keyed painter "
                     "order, font glyph geometry/clipping, integer/fractional camera resize and letterbox\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        if (window)
            glfwDestroyWindow(window);
        glfwTerminate();
        std::cerr << e.what() << '\n';
        return 1;
    }
#else
    return 1;
#endif
}
