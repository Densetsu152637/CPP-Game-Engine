// Private renderer resources. Kept separate to make device bootstrap readable.
#pragma once

#include <algorithm>
#include <limits>
#include <map>

namespace vulkan
{
#ifdef CPP_GAME_ENGINE_USE_VULKAN
    inline void require_vk(VkResult result, const char* operation)
    {
        if (result != VK_SUCCESS)
            throw std::runtime_error(std::string(operation) + " (VkResult " + std::to_string(result) + ")");
    }

    struct VulkanRenderer::GpuState
    {
        struct Pipeline
        {
            VkDevice device;
            VkPipeline handle = {};
            VkPipelineLayout layout = {};
            std::vector<VkDescriptorSetLayout> sets;
            explicit Pipeline(VkDevice value) : device(value) {}
            ~Pipeline()
            {
                if (handle) vkDestroyPipeline(device, handle, nullptr);
                if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
                for (auto set : sets) vkDestroyDescriptorSetLayout(device, set, nullptr);
            }
        };
        struct DepthTarget
        {
            VkDevice device;
            VkImage image = {};
            VkDeviceMemory memory = {};
            VkImageView view = {};
            explicit DepthTarget(VkDevice value) : device(value) {}
            ~DepthTarget()
            {
                if (view) vkDestroyImageView(device, view, nullptr);
                if (image) vkDestroyImage(device, image, nullptr);
                if (memory) vkFreeMemory(device, memory, nullptr);
            }
        };
        struct SampledTexture
        {
            VkDevice device;
            VkImage image = {};
            VkDeviceMemory memory = {};
            VkImageView view = {};
            VkSampler sampler = {};
            explicit SampledTexture(VkDevice value) : device(value) {}
            ~SampledTexture()
            {
                if (sampler) vkDestroySampler(device, sampler, nullptr);
                if (view) vkDestroyImageView(device, view, nullptr);
                if (image) vkDestroyImage(device, image, nullptr);
                if (memory) vkFreeMemory(device, memory, nullptr);
            }
        };
        struct Draw
        {
            VkDevice device;
            VkDescriptorPool pool = {};
            std::vector<std::unique_ptr<VulkanBuffer>> buffers;
            std::vector<std::unique_ptr<SampledTexture>> textures;
            explicit Draw(VkDevice value) : device(value) {}
            ~Draw() { if (pool) vkDestroyDescriptorPool(device, pool, nullptr); }
        };
        struct UploadCommands
        {
            VkDevice device;
            VkCommandPool pool = {};
            explicit UploadCommands(VkDevice value) : device(value) {}
            ~UploadCommands() { if (pool) vkDestroyCommandPool(device, pool, nullptr); }
        };

        VkDevice device = {};
        VkPhysicalDevice gpuPhysicalDevice = {};
        VkPhysicalDeviceLimits limits {};
        VkCommandPool commands = {};
        VkCommandBuffer command = {};
        VkFence completed = {};
        VkRenderPass pass = {};
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        std::vector<VkFramebuffer> framebuffers;
        std::vector<std::unique_ptr<DepthTarget>> depthTargets;
        std::vector<VkSemaphore> rendered;
        std::map<std::string, std::unique_ptr<Pipeline>> pipelines;
        std::vector<std::unique_ptr<Draw>> draws;
        bool suboptimal = false;

        ~GpuState()
        {
            // Owner waits for the device before destroying or rebuilding this state.
            if (commands) vkDestroyCommandPool(device, commands, nullptr);
            draws.clear();
            destroyTargets();
            if (completed) vkDestroyFence(device, completed, nullptr);
        }

        void destroyTargets()
        {
            pipelines.clear();
            for (auto framebuffer : framebuffers) if (framebuffer) vkDestroyFramebuffer(device, framebuffer, nullptr);
            framebuffers.clear();
            depthTargets.clear();
            for (auto semaphore : rendered) if (semaphore) vkDestroySemaphore(device, semaphore, nullptr);
            rendered.clear();
            if (pass) vkDestroyRenderPass(device, pass, nullptr);
            pass = {};
        }

        void initialize(VkDevice logicalDevice, VkPhysicalDevice physicalDevice, uint32_t family)
        {
            device = logicalDevice;
            gpuPhysicalDevice = physicalDevice;
            VkPhysicalDeviceProperties properties {};
            vkGetPhysicalDeviceProperties(physicalDevice, &properties);
            limits = properties.limits;
            VkCommandPoolCreateInfo pool { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
            pool.queueFamilyIndex = family;
            require_vk(vkCreateCommandPool(device, &pool, nullptr, &commands), "Create command pool");
            VkCommandBufferAllocateInfo allocation { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            allocation.commandPool = commands;
            allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocation.commandBufferCount = 1;
            require_vk(vkAllocateCommandBuffers(device, &allocation, &command), "Allocate command buffer");
            VkFenceCreateInfo fence { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            require_vk(vkCreateFence(device, &fence, nullptr, &completed), "Create frame fence");
        }

        VkFormat chooseDepthFormat(VkPhysicalDevice physicalDevice) const
        {
            constexpr VkFormat candidates[] { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT,
                VK_FORMAT_D32_SFLOAT_S8_UINT };
            for (const VkFormat format : candidates)
            {
                VkFormatProperties properties {};
                vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
                if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
                    return format;
            }
            throw std::runtime_error("Vulkan device has no supported depth attachment format");
        }

        void createDepthTarget(VkPhysicalDevice physicalDevice, VkExtent2D extent)
        {
            auto target = std::make_unique<DepthTarget>(device);
            VkImageCreateInfo image { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
            image.imageType = VK_IMAGE_TYPE_2D;
            image.extent = { extent.width, extent.height, 1 };
            image.mipLevels = image.arrayLayers = 1;
            image.format = depthFormat;
            image.tiling = VK_IMAGE_TILING_OPTIMAL;
            image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            image.samples = VK_SAMPLE_COUNT_1_BIT;
            image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            require_vk(vkCreateImage(device, &image, nullptr, &target->image), "Create depth image");

            VkMemoryRequirements requirements {};
            vkGetImageMemoryRequirements(device, target->image, &requirements);
            VkPhysicalDeviceMemoryProperties memoryProperties {};
            vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
            uint32_t memoryType = UINT32_MAX;
            for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
                if ((requirements.memoryTypeBits & (1u << i)) &&
                    (memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
                { memoryType = i; break; }
            if (memoryType == UINT32_MAX)
                throw std::runtime_error("Vulkan device has no device-local memory for depth images");
            VkMemoryAllocateInfo allocation { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = memoryType;
            require_vk(vkAllocateMemory(device, &allocation, nullptr, &target->memory), "Allocate depth image memory");
            require_vk(vkBindImageMemory(device, target->image, target->memory, 0), "Bind depth image memory");

            VkImageViewCreateInfo view { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            view.image = target->image;
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = depthFormat;
            view.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
            require_vk(vkCreateImageView(device, &view, nullptr, &target->view), "Create depth image view");
            depthTargets.push_back(std::move(target));
        }

        std::unique_ptr<SampledTexture> createSampledTexture(
            const rendering::Texture2D& source,
            VulkanMemoryManager& memory,
            VkQueue queue,
            uint32_t queueFamily
        ) {
            if (!source.valid())
                throw std::invalid_argument("Vulkan textures require complete row-major RGBA8 pixels");
            if (source.width > limits.maxImageDimension2D || source.height > limits.maxImageDimension2D)
                throw std::invalid_argument("Vulkan texture dimensions exceed maxImageDimension2D");
            VkFormatProperties formatProperties {};
            vkGetPhysicalDeviceFormatProperties(gpuPhysicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &formatProperties);
            if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
                throw std::runtime_error("Vulkan device cannot sample optimal RGBA8 images");

            auto texture = std::make_unique<SampledTexture>(device);
            auto staging = memory.createVulkanBufferFrom(source.rgba8,
                rendering::GpuBufferUsage::TransferSource, rendering::GpuMemoryUsage::CpuToGpu);
            VkImageCreateInfo image { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
            image.imageType = VK_IMAGE_TYPE_2D;
            image.extent = { source.width, source.height, 1 };
            image.mipLevels = image.arrayLayers = 1;
            image.format = VK_FORMAT_R8G8B8A8_UNORM;
            image.tiling = VK_IMAGE_TILING_OPTIMAL;
            image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            image.samples = VK_SAMPLE_COUNT_1_BIT;
            image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            require_vk(vkCreateImage(device, &image, nullptr, &texture->image), "Create sampled texture image");
            VkMemoryRequirements requirements {};
            vkGetImageMemoryRequirements(device, texture->image, &requirements);
            VkPhysicalDeviceMemoryProperties memoryProperties {};
            vkGetPhysicalDeviceMemoryProperties(gpuPhysicalDevice, &memoryProperties);
            uint32_t memoryType = UINT32_MAX;
            for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
                if ((requirements.memoryTypeBits & (1u << i)) &&
                    (memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
                { memoryType = i; break; }
            if (memoryType == UINT32_MAX)
                throw std::runtime_error("Vulkan device has no device-local memory for sampled textures");
            VkMemoryAllocateInfo imageAllocation { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            imageAllocation.allocationSize = requirements.size;
            imageAllocation.memoryTypeIndex = memoryType;
            require_vk(vkAllocateMemory(device, &imageAllocation, nullptr, &texture->memory), "Allocate sampled texture memory");
            require_vk(vkBindImageMemory(device, texture->image, texture->memory, 0), "Bind sampled texture memory");

            UploadCommands upload { device };
            VkCommandPoolCreateInfo pool { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
            pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            pool.queueFamilyIndex = queueFamily;
            require_vk(vkCreateCommandPool(device, &pool, nullptr, &upload.pool), "Create texture upload command pool");
            VkCommandBufferAllocateInfo allocation { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            allocation.commandPool = upload.pool;
            allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocation.commandBufferCount = 1;
            VkCommandBuffer uploadCommand {};
            require_vk(vkAllocateCommandBuffers(device, &allocation, &uploadCommand), "Allocate texture upload command buffer");
            VkCommandBufferBeginInfo begin { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            require_vk(vkBeginCommandBuffer(uploadCommand, &begin), "Begin texture upload command buffer");
            VkImageMemoryBarrier barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = texture->image;
            barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            vkCmdPipelineBarrier(uploadCommand, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
            VkBufferImageCopy region {};
            region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            region.imageExtent = { source.width, source.height, 1 };
            vkCmdCopyBufferToImage(uploadCommand, staging->handle(), texture->image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(uploadCommand, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
            require_vk(vkEndCommandBuffer(uploadCommand), "End texture upload command buffer");
            VkFenceCreateInfo fenceInfo { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            VkFence fence {};
            require_vk(vkCreateFence(device, &fenceInfo, nullptr, &fence), "Create texture upload fence");
            try
            {
                VkSubmitInfo submit { VK_STRUCTURE_TYPE_SUBMIT_INFO };
                submit.commandBufferCount = 1;
                submit.pCommandBuffers = &uploadCommand;
                require_vk(vkQueueSubmit(queue, 1, &submit, fence), "Submit texture upload");
                require_vk(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "Wait for texture upload");
            }
            catch (...)
            {
                vkDestroyFence(device, fence, nullptr);
                throw;
            }
            vkDestroyFence(device, fence, nullptr);

            VkImageViewCreateInfo view { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            view.image = texture->image;
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = VK_FORMAT_R8G8B8A8_UNORM;
            view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            require_vk(vkCreateImageView(device, &view, nullptr, &texture->view), "Create sampled texture view");
            VkSamplerCreateInfo sampler { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
            sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
            sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sampler.maxAnisotropy = 1.0f;
            sampler.minLod = 0.0f;
            sampler.maxLod = 0.0f;
            require_vk(vkCreateSampler(device, &sampler, nullptr, &texture->sampler), "Create sampled texture sampler");
            return texture;
        }

        void createTargets(const VulkanSwapchain& swapchain, VkPhysicalDevice physicalDevice)
        {
            destroyTargets();
            depthFormat = chooseDepthFormat(physicalDevice);
            VkAttachmentDescription attachments[2] {};
            auto& color = attachments[0];
            color.format = swapchain.imageFormat();
            color.samples = VK_SAMPLE_COUNT_1_BIT;
            color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            auto& depth = attachments[1];
            depth.format = depthFormat;
            depth.samples = VK_SAMPLE_COUNT_1_BIT;
            depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            VkAttachmentReference reference { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
            VkAttachmentReference depthReference { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
            VkSubpassDescription subpass {};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &reference;
            subpass.pDepthStencilAttachment = &depthReference;
            VkSubpassDependency dependency {};
            dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            dependency.dstSubpass = 0;
            dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            VkRenderPassCreateInfo info { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
            info.attachmentCount = 2;
            info.pAttachments = attachments;
            info.subpassCount = 1;
            info.pSubpasses = &subpass;
            info.dependencyCount = 1;
            info.pDependencies = &dependency;
            require_vk(vkCreateRenderPass(device, &info, nullptr, &pass), "Create render pass");
            framebuffers.resize(swapchain.imageViews().size());
            depthTargets.reserve(framebuffers.size());
            rendered.resize(framebuffers.size());
            for (size_t index = 0; index < framebuffers.size(); ++index)
            {
                createDepthTarget(physicalDevice, swapchain.extent());
                VkImageView views[] { swapchain.imageViews()[index], depthTargets.back()->view };
                VkFramebufferCreateInfo framebuffer { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
                framebuffer.renderPass = pass;
                framebuffer.attachmentCount = 2;
                framebuffer.pAttachments = views;
                framebuffer.width = swapchain.extent().width;
                framebuffer.height = swapchain.extent().height;
                framebuffer.layers = 1;
                require_vk(vkCreateFramebuffer(device, &framebuffer, nullptr, &framebuffers[index]), "Create framebuffer");
                VkSemaphoreCreateInfo semaphore { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
                require_vk(vkCreateSemaphore(device, &semaphore, nullptr, &rendered[index]), "Create present semaphore");
            }
        }

        void begin(const VulkanSwapchain& swapchain, uint32_t imageIndex)
        {
            require_vk(vkResetCommandPool(device, commands, 0), "Reset command pool");
            draws.clear();
            VkCommandBufferBeginInfo beginInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            require_vk(vkBeginCommandBuffer(command, &beginInfo), "Begin command buffer");
            VkClearValue clear[2] {};
            clear[0].color = {{ 0.025f, 0.035f, 0.06f, 1.0f }};
            clear[1].depthStencil = { 1.0f, 0 };
            VkRenderPassBeginInfo render { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
            render.renderPass = pass;
            render.framebuffer = framebuffers.at(imageIndex);
            render.renderArea.extent = swapchain.extent();
            render.clearValueCount = 2;
            render.pClearValues = clear;
            vkCmdBeginRenderPass(command, &render, VK_SUBPASS_CONTENTS_INLINE);
            VkViewport viewport { 0, 0, static_cast<float>(swapchain.extent().width),
                static_cast<float>(swapchain.extent().height), 0, 1 };
            VkRect2D scissor { {0, 0}, swapchain.extent() };
            vkCmdSetViewport(command, 0, 1, &viewport);
            vkCmdSetScissor(command, 0, 1, &scissor);
        }

        using Uniforms = std::vector<const rendering::UniformValue*>;
        Uniforms uniformLayout(const rendering::IShader& shader) const
        {
            Uniforms uniforms;
            for (const auto& uniform : shader.uniforms.uniforms())
            {
                if (uniform.bytes.empty() || uniform.bytes.size() > limits.maxUniformBufferRange)
                    throw std::invalid_argument("Vulkan UBO size exceeds device range or is empty");
                if (uniform.binding.set >= limits.maxBoundDescriptorSets)
                    throw std::invalid_argument("Vulkan uniform set exceeds maxBoundDescriptorSets");
                uniforms.push_back(&uniform);
            }
            if (uniforms.size() > limits.maxPerStageDescriptorUniformBuffers ||
                uniforms.size() > limits.maxDescriptorSetUniformBuffers ||
                uniforms.size() > limits.maxPerStageResources)
                throw std::invalid_argument("Vulkan uniform count exceeds device descriptor limits");
            std::sort(uniforms.begin(), uniforms.end(), [](auto lhs, auto rhs) {
                return std::pair(lhs->binding.set, lhs->binding.binding) < std::pair(rhs->binding.set, rhs->binding.binding);
            });
            for (size_t i = 1; i < uniforms.size(); ++i)
                if (uniforms[i]->binding.set == uniforms[i-1]->binding.set &&
                    uniforms[i]->binding.binding == uniforms[i-1]->binding.binding)
                    throw std::invalid_argument("Vulkan uniform names cannot share a set/binding");
            return uniforms;
        }

        Pipeline& pipeline(const VulkanShaderProgram& shader, const Uniforms& uniforms,
            const rendering::VertexLayout* vertexLayout, const bool textured)
        {
            // Exact content key avoids hash collisions, dangling shader pointers and address reuse.
            std::string key;
            auto append = [&key](const auto& value) {
                key.append(reinterpret_cast<const char*>(&value), sizeof(value));
            };
            if (shader.sources().length() != 2)
                throw std::invalid_argument("Vulkan graphics draws require one vertex and one fragment SPIR-V source");
            bool vertex = false, fragment = false;
            for (const auto& source : shader.sources())
            {
                rendering::validateSpirvSource(source);
                if (source.stage == ShaderStage::Vertex && !vertex) vertex = true;
                else if (source.stage == ShaderStage::Fragment && !fragment) fragment = true;
                else throw std::invalid_argument("Vulkan graphics draws support vertex and fragment stages only");
                append(source.stage);
                append(source.entryPoint.size());
                key.append(source.entryPoint);
                append(source.bytes.size());
                key.append(source.bytes.data(), source.bytes.size());
            }
            for (auto uniform : uniforms) { append(uniform->binding.set); append(uniform->binding.binding); }
            append(textured);
            if (vertexLayout)
            {
                append(vertexLayout->stride);
                for (const auto& attribute : vertexLayout->attributes)
                {
                    append(attribute.location);
                    append(attribute.format);
                    append(attribute.offset);
                }
            }
            auto existing = pipelines.find(key);
            if (existing != pipelines.end()) return *existing->second;

            auto result = std::make_unique<Pipeline>(device);
            if (textured)
                for (auto uniform : uniforms)
                    if (uniform->binding.set == 0 && uniform->binding.binding == 3)
                        throw std::invalid_argument("Texture draws reserve set 0 binding 3 for the sampled image");
            const uint32_t setCount = std::max<uint32_t>(
                uniforms.empty() ? 0u : uniforms.back()->binding.set + 1,
                textured ? 1u : 0u);
            result->sets.resize(setCount);
            for (uint32_t set = 0; set < setCount; ++set)
            {
                std::vector<VkDescriptorSetLayoutBinding> bindings;
                for (auto uniform : uniforms)
                    if (uniform->binding.set == set)
                        bindings.push_back({ uniform->binding.binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr });
                if (textured && set == 0)
                    bindings.push_back({ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                        VK_SHADER_STAGE_FRAGMENT_BIT, nullptr });
                VkDescriptorSetLayoutCreateInfo info { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
                info.bindingCount = static_cast<uint32_t>(bindings.size());
                info.pBindings = bindings.data();
                require_vk(vkCreateDescriptorSetLayout(device, &info, nullptr, &result->sets[set]), "Create descriptor layout");
            }
            VkPipelineLayoutCreateInfo layout { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            layout.setLayoutCount = setCount;
            layout.pSetLayouts = result->sets.data();
            require_vk(vkCreatePipelineLayout(device, &layout, nullptr, &result->layout), "Create pipeline layout");
            const auto modules = shader.createModules(device);
            std::vector<VkPipelineShaderStageCreateInfo> stages;
            for (const auto& module : modules)
            {
                VkPipelineShaderStageCreateInfo stage { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
                stage.stage = to_vk_stage(module.stage());
                stage.module = module.handle();
                stage.pName = module.entryPoint().c_str();
                stages.push_back(stage);
            }
            VkVertexInputBindingDescription binding { 0, vertexLayout ? vertexLayout->stride : 0, VK_VERTEX_INPUT_RATE_VERTEX };
            std::vector<VkVertexInputAttributeDescription> attributes;
            if (vertexLayout)
                for (const auto& attribute : vertexLayout->attributes)
                {
                    const VkFormat format = attribute.format == rendering::VertexAttributeFormat::Float2 ? VK_FORMAT_R32G32_SFLOAT :
                        attribute.format == rendering::VertexAttributeFormat::Float3 ? VK_FORMAT_R32G32B32_SFLOAT :
                        VK_FORMAT_R32G32B32A32_SFLOAT;
                    attributes.push_back({ attribute.location, 0, format, attribute.offset });
                }
            VkPipelineVertexInputStateCreateInfo input { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
            if (vertexLayout)
            {
                input.vertexBindingDescriptionCount = 1;
                input.pVertexBindingDescriptions = &binding;
                input.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
                input.pVertexAttributeDescriptions = attributes.data();
            }
            VkPipelineInputAssemblyStateCreateInfo assembly { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewport { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
            viewport.viewportCount = viewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo raster { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
            raster.polygonMode = VK_POLYGON_MODE_FILL;
            raster.cullMode = VK_CULL_MODE_NONE;
            raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            raster.lineWidth = 1.0f;
            VkPipelineMultisampleStateCreateInfo samples { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
            samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineDepthStencilStateCreateInfo depth { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
            depth.depthTestEnable = VK_TRUE;
            depth.depthWriteEnable = VK_TRUE;
            depth.depthCompareOp = VK_COMPARE_OP_LESS;
            VkPipelineColorBlendAttachmentState attachment {};
            attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo blend { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
            blend.attachmentCount = 1;
            blend.pAttachments = &attachment;
            VkDynamicState dynamicStates[] { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
            VkPipelineDynamicStateCreateInfo dynamic { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
            dynamic.dynamicStateCount = 2;
            dynamic.pDynamicStates = dynamicStates;
            VkGraphicsPipelineCreateInfo info { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
            info.stageCount = static_cast<uint32_t>(stages.size());
            info.pStages = stages.data();
            info.pVertexInputState = &input;
            info.pInputAssemblyState = &assembly;
            info.pViewportState = &viewport;
            info.pRasterizationState = &raster;
            info.pMultisampleState = &samples;
            info.pDepthStencilState = &depth;
            info.pColorBlendState = &blend;
            info.pDynamicState = &dynamic;
            info.layout = result->layout;
            info.renderPass = pass;
            require_vk(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &result->handle), "Create graphics pipeline");
            auto& stored = *result;
            pipelines.emplace(std::move(key), std::move(result));
            return stored;
        }

        void draw(const VulkanShaderProgram& shader, VulkanMemoryManager& memory,
            const rendering::SerializedBufferView* vertices = nullptr,
            const rendering::VertexLayout* vertexLayout = nullptr,
            const rendering::Texture2D* texture = nullptr,
            VkQueue graphicsQueue = VK_NULL_HANDLE,
            uint32_t graphicsQueueFamily = 0)
        {
            if ((vertices == nullptr) != (vertexLayout == nullptr))
                throw std::invalid_argument("Vulkan mesh data and vertex layout must be provided together");
            if (vertices && (vertices->empty() || !vertexLayout->valid() ||
                vertices->elementStride != vertexLayout->stride ||
                vertices->elementCount > std::numeric_limits<uint32_t>::max() ||
                vertices->elementCount > std::numeric_limits<size_t>::max() / vertexLayout->stride ||
                vertices->byteSize != vertices->elementCount * vertexLayout->stride))
                throw std::invalid_argument("Vulkan mesh data does not match its explicit vertex layout");
            if (texture && !vertices)
                throw std::invalid_argument("Textured Vulkan draws require vertex-buffer mesh data");
            if (vertexLayout && (vertexLayout->stride > limits.maxVertexInputBindingStride ||
                vertexLayout->attributes.size() > limits.maxVertexInputAttributes))
                throw std::invalid_argument("Vulkan mesh layout exceeds device vertex input limits");
            if (vertexLayout)
                for (const auto& attribute : vertexLayout->attributes)
                    if (attribute.location >= limits.maxVertexInputAttributes ||
                        attribute.offset > limits.maxVertexInputAttributeOffset)
                        throw std::invalid_argument("Vulkan mesh attribute exceeds device vertex input limits");
            auto uniforms = uniformLayout(shader);
            auto& program = pipeline(shader, uniforms, vertexLayout, texture != nullptr);
            auto draw = std::make_unique<Draw>(device);
            if (texture)
                draw->textures.push_back(createSampledTexture(*texture, memory, graphicsQueue, graphicsQueueFamily));
            std::vector<VkDescriptorSet> sets(program.sets.size());
            if (!sets.empty())
            {
                std::vector<VkDescriptorPoolSize> sizes;
                if (!uniforms.empty())
                    sizes.push_back({ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(uniforms.size()) });
                if (texture)
                    sizes.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 });
                VkDescriptorPoolCreateInfo pool { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
                pool.maxSets = static_cast<uint32_t>(sets.size());
                pool.poolSizeCount = static_cast<uint32_t>(sizes.size());
                pool.pPoolSizes = sizes.data();
                require_vk(vkCreateDescriptorPool(device, &pool, nullptr, &draw->pool), "Create draw descriptor pool");
                VkDescriptorSetAllocateInfo allocation { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
                allocation.descriptorPool = draw->pool;
                allocation.descriptorSetCount = static_cast<uint32_t>(sets.size());
                allocation.pSetLayouts = program.sets.data();
                require_vk(vkAllocateDescriptorSets(device, &allocation, sets.data()), "Allocate draw descriptors");
                for (auto uniform : uniforms)
                {
                    auto buffer = memory.createVulkanBufferFrom(uniform->bytes, rendering::GpuBufferUsage::Uniform);
                    VkDescriptorBufferInfo bufferInfo { buffer->handle(), 0, static_cast<VkDeviceSize>(uniform->bytes.size()) };
                    VkWriteDescriptorSet write { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                    write.dstSet = sets[uniform->binding.set];
                    write.dstBinding = uniform->binding.binding;
                    write.descriptorCount = 1;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    write.pBufferInfo = &bufferInfo;
                    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
                    draw->buffers.push_back(std::move(buffer));
                }
                if (texture)
                {
                    VkDescriptorImageInfo imageInfo { draw->textures.back()->sampler,
                        draw->textures.back()->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
                    VkWriteDescriptorSet write { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                    write.dstSet = sets[0];
                    write.dstBinding = 3;
                    write.descriptorCount = 1;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    write.pImageInfo = &imageInfo;
                    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
                }
            }
            if (vertices)
            {
                rendering::GpuBufferDescription description;
                description.byteSize = vertices->byteSize;
                description.usage = rendering::GpuBufferUsage::Vertex;
                description.memoryUsage = rendering::GpuMemoryUsage::CpuToGpu;
                auto buffer = memory.createVulkanBuffer(description);
                memory.writeBuffer(*buffer, *vertices);
                draw->buffers.push_back(std::move(buffer));
            }
            // Retain resources before recording any references; allocation failures leave the frame valid.
            draws.push_back(std::move(draw));
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, program.handle);
            if (!sets.empty())
                vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, program.layout, 0,
                    static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);
            if (vertices)
            {
                const VkBuffer buffer = draws.back()->buffers.back()->handle();
                const VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(command, 0, 1, &buffer, &offset);
                vkCmdDraw(command, static_cast<uint32_t>(vertices->elementCount), 1, 0, 0);
            }
            else vkCmdDraw(command, 3, 1, 0, 0);
        }
    };
#else
    struct VulkanRenderer::GpuState {};
#endif
}
