// Standalone GPU smoke test; requires compiled sample shaders and a Vulkan device.
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
    VKAPI_ATTR VkBool32 VKAPI_CALL report(VkDebugReportFlagsEXT flags,
        VkDebugReportObjectTypeEXT, uint64_t, size_t, int32_t, const char*, const char* text, void*)
    {
        if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) ++validationErrors;
        std::cerr << "Vulkan validation: " << text << '\n';
        return VK_FALSE;
    }

    template<class Function> void expectFailure(Function&& function)
    {
        try { function(); }
        catch (const std::exception&) { return; }
        throw std::runtime_error("Expected invalid Vulkan operation to fail");
    }

    vulkan::VulkanShaderProgram makeShader(const std::filesystem::path& directory)
    {
        vulkan::VulkanShaderProgram shader("smoke");
        shader.addSpirv(rendering::ShaderStage::Vertex, directory / "triangle.vert.spv");
        shader.addSpirv(rendering::ShaderStage::Fragment, directory / "triangle.frag.spv");
        return shader;
    }

    void check(VkResult result, const char* operation)
    {
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
            throw std::runtime_error(std::string(operation) + ": " + std::to_string(result));
    }

    // Uses only public native handles; does not alter the renderer's frame state.
    void verifyPixels(vulkan::VulkanRenderer& renderer, const std::vector<bool>& drawn)
    {
        const auto support = vulkan::VulkanSwapchain::querySupport(renderer.physicalDevice(), renderer.surface());
        const auto& swapchain = renderer.swapchain();
        const auto format = swapchain.imageFormat();
        if (!(support.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ||
            (format != VK_FORMAT_B8G8R8A8_SRGB && format != VK_FORMAT_R8G8B8A8_SRGB))
        {
            std::cout << "Pixel verification skipped: surface lacks supported SRGB transfer-source images\n";
            return;
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
                    VkSubmitInfo consume { VK_STRUCTURE_TYPE_SUBMIT_INFO };
                    consume.waitSemaphoreCount = 1;
                    consume.pWaitSemaphores = &acquired;
                    consume.pWaitDstStageMask = &stage;
                    vkQueueSubmit(graphicsQueue, 1, &consume, VK_NULL_HANDLE);
                }
                // Includes presentation's semaphore wait before destroying copied.
                vkDeviceWaitIdle(device);
                if (pool) vkDestroyCommandPool(device, pool, nullptr);
                if (acquired) vkDestroySemaphore(device, acquired, nullptr);
                if (copied) vkDestroySemaphore(device, copied, nullptr);
                if (complete) vkDestroyFence(device, complete, nullptr);
            }
        } resources { renderer.device(), renderer.graphicsQueue() };
        const auto device = renderer.device();
        VkCommandPoolCreateInfo pool { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        pool.queueFamilyIndex = renderer.queueFamilies().graphicsFamily;
        check(vkCreateCommandPool(device, &pool, nullptr, &resources.pool), "Readback command pool");
        VkSemaphoreCreateInfo semaphore { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        check(vkCreateSemaphore(device, &semaphore, nullptr, &resources.acquired), "Readback acquire semaphore");
        check(vkCreateSemaphore(device, &semaphore, nullptr, &resources.copied), "Readback present semaphore");
        VkFenceCreateInfo fence { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        check(vkCreateFence(device, &fence, nullptr, &resources.complete), "Readback fence");
        VkCommandBufferAllocateInfo allocation { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        allocation.commandPool = resources.pool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = 1;
        VkCommandBuffer command {};
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
                VK_NULL_HANDLE, &index), "Readback acquire");
            resources.acquiredPending = true;
            const bool known = index < drawn.size() && drawn[index];
            check(vkResetCommandPool(device, resources.pool, 0), "Readback reset");
            VkCommandBufferBeginInfo begin { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            check(vkBeginCommandBuffer(command, &begin), "Readback begin");
            VkImageMemoryBarrier barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            barrier.oldLayout = known ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = known ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            barrier.dstAccessMask = known ? VK_ACCESS_TRANSFER_READ_BIT : 0;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = swapchain.images()[index];
            barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
            if (known)
            {
                VkBufferImageCopy region {};
                region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                region.imageExtent = { extent.width, extent.height, 1 };
                vkCmdCopyImageToBuffer(command, barrier.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    buffer->handle(), 1, &region);
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barrier.dstAccessMask = 0;
                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrier);
                VkMemoryBarrier host { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
                host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                    0, 1, &host, 0, nullptr, 0, nullptr);
            }
            check(vkEndCommandBuffer(command), "Readback end");
            const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            VkSubmitInfo submit { VK_STRUCTURE_TYPE_SUBMIT_INFO };
            submit.waitSemaphoreCount = submit.signalSemaphoreCount = submit.commandBufferCount = 1;
            submit.pWaitSemaphores = &resources.acquired;
            submit.pWaitDstStageMask = &waitStage;
            submit.pCommandBuffers = &command;
            submit.pSignalSemaphores = &resources.copied;
            check(vkResetFences(device, 1, &resources.complete), "Readback reset fence");
            check(vkQueueSubmit(renderer.graphicsQueue(), 1, &submit, resources.complete), "Readback submit");
            resources.acquiredPending = false;
            const auto handle = swapchain.handle();
            VkPresentInfoKHR present { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
            present.waitSemaphoreCount = present.swapchainCount = 1;
            present.pWaitSemaphores = &resources.copied;
            present.pSwapchains = &handle;
            present.pImageIndices = &index;
            check(vkQueuePresentKHR(renderer.presentQueue(), &present), "Readback present");
            check(vkWaitForFences(device, 1, &resources.complete, VK_TRUE, UINT64_MAX), "Readback wait");
            check(vkDeviceWaitIdle(device), "Readback presentation wait");
            if (!known) continue; // Never infer contents of an untracked/recreated/cancelled image.

            void* mapped = nullptr;
            check(vkMapMemory(device, buffer->memory(), 0, description.byteSize, 0, &mapped), "Readback map");
            const auto* bytes = static_cast<const unsigned char*>(mapped);
            const bool bgra = format == VK_FORMAT_B8G8R8A8_SRGB;
            auto pixel = [&](uint32_t x, uint32_t y) {
                const auto* p = bytes + (static_cast<size_t>(y) * extent.width + x) * 4;
                return std::array<unsigned, 3> { p[bgra ? 2 : 0], p[1], p[bgra ? 0 : 2] };
            };
            const auto left = pixel(extent.width * 3 / 10, extent.height / 2);
            const auto right = pixel(extent.width * 7 / 10, extent.height / 2);
            const auto clear = pixel(extent.width / 2, extent.height / 2);
            std::ofstream artifact("build/vulkan-readback.ppm", std::ios::binary);
            artifact << "P6\n" << extent.width << ' ' << extent.height << "\n255\n";
            for (uint32_t y = 0; y < extent.height; ++y)
                for (uint32_t x = 0; x < extent.width; ++x)
                    for (const auto channel : pixel(x, y)) artifact.put(static_cast<char>(channel));
            vkUnmapMemory(device, buffer->memory());
            if (!(left[0] > 200 && left[1] < 180 && left[2] < 150 &&
                right[0] < 150 && right[1] > 160 && right[2] > 200 &&
                clear[0] < 100 && clear[1] < 100 && clear[2] < 120))
                throw std::runtime_error("GPU pixels do not contain independent orange/cyan triangles on clear background");
            std::cout << "GPU pixel verification passed on image " << index << ": left RGB "
                << left[0] << ',' << left[1] << ',' << left[2] << "; right RGB "
                << right[0] << ',' << right[1] << ',' << right[2] << "; clear RGB "
                << clear[0] << ',' << clear[1] << ',' << clear[2] << '\n';
            return;
        }
        throw std::runtime_error("Readback did not reacquire a tracked rendered image");
    }

    void run(vulkan::VulkanRenderer& renderer, GLFWwindow* window, const std::filesystem::path& directory,
        bool visual)
    {
        std::array<float, 16> transform { 0.5f,0,0,0, 0,0.5f,0,0, 0,0,1,0, -0.4f,0,0,1 };
        std::array<float, 4> color { 1,0.2f,0.1f,1 };
        auto shader = makeShader(directory);
        std::vector<bool> drawn;
        VkSwapchainKHR trackedSwapchain = {};
        VkExtent2D trackedExtent = {};
        expectFailure([&] { renderer.render(shader); });
        unsigned frames = 0;
        for (unsigned attempt = 0; attempt < 100 && frames < 12; ++attempt)
        {
            glfwPollEvents();
            if (!renderer.beginRenderFrame()) continue;
            const auto frameSwapchain = renderer.swapchain().handle();
            const auto frameImage = renderer.currentFrame().imageIndex;
            const auto frameExtent = renderer.swapchain().extent();
            if (trackedSwapchain != frameSwapchain || trackedExtent.width != frameExtent.width ||
                trackedExtent.height != frameExtent.height)
            {
                trackedSwapchain = frameSwapchain;
                trackedExtent = frameExtent;
                drawn.assign(renderer.swapchain().images().size(), false);
            }
            expectFailure([&] { renderer.beginRenderFrame(); });
            renderer.upload(shader, "transform", transform, frames, {0,0});
            renderer.upload(shader, "color", color, frames, {0,1});
            renderer.render(shader);
            // A second draw shares the pipeline but must keep independent UBO bytes.
            transform[12] = 0.4f;
            color = {0.1f,0.7f,1,1};
            renderer.upload(shader, "transform", transform, frames, {0,0});
            renderer.upload(shader, "color", color, frames, {0,1});
            renderer.render(shader);
            if (frames == 2)
            {
                auto temporary = makeShader(directory);
                renderer.upload(temporary, "transform", transform, frames, {0,0});
                renderer.upload(temporary, "color", color, frames, {0,1});
                renderer.render(temporary); // Destroy CPU shader before GPU submission.
            }
            if (frames == 3)
            {
                // Sparse sets and layout changes must not invalidate previous draws.
                renderer.upload(shader, "unused", color, frames, {1,5});
                renderer.render(shader);
            }
            if (frames == 4)
            {
                vulkan::VulkanShaderProgram invalid("missing-stages");
                expectFailure([&] { renderer.render(invalid); });
                auto duplicate = makeShader(directory);
                renderer.upload(duplicate, "first", color, frames, {0,0});
                renderer.upload(duplicate, "second", color, frames, {0,0});
                expectFailure([&] { renderer.render(duplicate); });
                auto oversized = makeShader(directory);
                VkPhysicalDeviceProperties properties {};
                vkGetPhysicalDeviceProperties(renderer.physicalDevice(), &properties);
                oversized.uniforms.declare("oversized", {0,0},
                    static_cast<size_t>(properties.limits.maxUniformBufferRange) + 1);
                expectFailure([&] { renderer.render(oversized); });
                renderer.cancelRenderFrame();
            }
            else renderer.endRenderFrame();
            if (renderer.swapchain().handle() == frameSwapchain && renderer.swapchain().extent().width == frameExtent.width &&
                renderer.swapchain().extent().height == frameExtent.height) drawn.at(frameImage) = frames != 4;
            else { drawn.clear(); trackedSwapchain = {}; }
            if (frames == 5) { glfwSetWindowSize(window, 400, 260); drawn.assign(drawn.size(), false); }
            if (frames == 7)
            {
                glfwIconifyWindow(window);
                glfwPollEvents();
                if (glfwGetWindowAttrib(window, GLFW_ICONIFIED))
                {
                    if (renderer.beginRenderFrame())
                        throw std::runtime_error("A minimized window acquired a render frame");
                    std::cout << "Minimized frame acquisition skipped\n";
                }
                else std::cout << "Window system did not iconify the hidden smoke window; minimize check skipped\n";
                glfwRestoreWindow(window);
                glfwPollEvents();
                drawn.assign(drawn.size(), false); // Native window changes can invalidate image contents.
            }
            transform[12] = -0.4f;
            color = {1,0.2f,0.1f,1};
            ++frames;
        }
        if (frames != 12 || renderer.renderCallCount() < 24)
            throw std::runtime_error("Vulkan smoke did not complete its draw frames");
        renderer.waitIdle();
        verifyPixels(renderer, drawn);
        expectFailure([&] { renderer.endRenderFrame(); });
        if (visual)
        {
            glfwShowWindow(window);
            std::cout << "Visual check: separate orange left and cyan right triangles" << std::endl;
            const double until = glfwGetTime() + 20.0;
            while (glfwGetTime() < until && !glfwWindowShouldClose(window))
                glfwWaitEventsTimeout(0.1);
        }
    }
}
#endif

int main(int argc, char** argv)
{
#ifdef CPP_GAME_ENGINE_USE_VULKAN
    GLFWwindow* window = nullptr;
    try
    {
        const std::filesystem::path directory = argc > 1 ? argv[1] : "build/shaders";
        bool validation = false, visual = false;
        for (int index = 2; index < argc; ++index)
        {
            if (std::string_view(argv[index]) == "--validation") validation = true;
            else if (std::string_view(argv[index]) == "--visual") visual = true;
            else throw std::invalid_argument("Unknown Vulkan smoke option");
        }
        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, visual ? GLFW_TRUE : GLFW_FALSE);
        window = glfwCreateWindow(320, 240, "Vulkan smoke", nullptr, nullptr);
        if (!window) throw std::runtime_error("GLFW Vulkan window creation failed");
        vulkan::VulkanRendererConfig config;
        config.enableValidationLayers = validation;
        if (validation) config.validationCallback = report;
        vulkan::VulkanRenderer renderer;
        for (int round = 0; round < 2; ++round)
        {
            renderer.initialize(window, config);
            // Exercise ownership transfer of an initialized renderer.
            vulkan::VulkanRenderer moved(std::move(renderer));
            run(moved, window, directory, visual);
            renderer = std::move(moved);
            renderer.shutdown();
            renderer.shutdown();
        }
        glfwDestroyWindow(window);
        glfwTerminate();
        if (validationErrors != 0) throw std::runtime_error("Vulkan validation errors were reported");
        std::cout << "Vulkan smoke passed: draw snapshots, shader lifetime, sparse sets, invalid inputs, cancel, resize, move, reinitialize"
                  << (validation ? " (validation enabled)\n" : " (validation disabled)\n");
        return 0;
    }
    catch (const std::exception& error)
    {
        if (window) glfwDestroyWindow(window);
        glfwTerminate();
        std::cerr << error.what() << '\n';
        return 1;
    }
#else
    (void)argc; (void)argv;
    std::cerr << "Vulkan smoke requires CPP_GAME_ENGINE_USE_VULKAN\n";
    return 1;
#endif
}
