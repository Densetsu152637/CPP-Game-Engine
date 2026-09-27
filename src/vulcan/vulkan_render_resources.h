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
        struct Draw
        {
            VkDevice device;
            VkDescriptorPool pool = {};
            std::vector<std::unique_ptr<VulkanBuffer>> buffers;
            explicit Draw(VkDevice value) : device(value) {}
            ~Draw() { if (pool) vkDestroyDescriptorPool(device, pool, nullptr); }
        };

        VkDevice device = {};
        VkPhysicalDeviceLimits limits {};
        VkCommandPool commands = {};
        VkCommandBuffer command = {};
        VkFence completed = {};
        VkRenderPass pass = {};
        std::vector<VkFramebuffer> framebuffers;
        std::vector<VkSemaphore> rendered;
        std::map<std::string, std::unique_ptr<Pipeline>> pipelines;
        std::vector<std::unique_ptr<Draw>> draws;
        bool suboptimal = false;

        ~GpuState()
        {
            // Owner waits for the device before destroying or rebuilding this state.
            if (commands) vkDestroyCommandPool(device, commands, nullptr);
            draws.clear();
            pipelines.clear();
            for (auto framebuffer : framebuffers) vkDestroyFramebuffer(device, framebuffer, nullptr);
            for (auto semaphore : rendered) vkDestroySemaphore(device, semaphore, nullptr);
            if (pass) vkDestroyRenderPass(device, pass, nullptr);
            if (completed) vkDestroyFence(device, completed, nullptr);
        }

        void initialize(VkDevice logicalDevice, VkPhysicalDevice physicalDevice, uint32_t family)
        {
            device = logicalDevice;
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

        void createTargets(const VulkanSwapchain& swapchain)
        {
            VkAttachmentDescription attachment {};
            attachment.format = swapchain.imageFormat();
            attachment.samples = VK_SAMPLE_COUNT_1_BIT;
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            VkAttachmentReference reference { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
            VkSubpassDescription subpass {};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &reference;
            VkSubpassDependency dependency {};
            dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            dependency.dstSubpass = 0;
            dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            VkRenderPassCreateInfo info { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
            info.attachmentCount = 1;
            info.pAttachments = &attachment;
            info.subpassCount = 1;
            info.pSubpasses = &subpass;
            info.dependencyCount = 1;
            info.pDependencies = &dependency;
            require_vk(vkCreateRenderPass(device, &info, nullptr, &pass), "Create render pass");
            framebuffers.resize(swapchain.imageViews().size());
            rendered.resize(framebuffers.size());
            for (size_t index = 0; index < framebuffers.size(); ++index)
            {
                VkFramebufferCreateInfo framebuffer { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
                framebuffer.renderPass = pass;
                framebuffer.attachmentCount = 1;
                framebuffer.pAttachments = &swapchain.imageViews()[index];
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
            VkClearValue clear {};
            clear.color = {{ 0.025f, 0.035f, 0.06f, 1.0f }};
            VkRenderPassBeginInfo render { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
            render.renderPass = pass;
            render.framebuffer = framebuffers.at(imageIndex);
            render.renderArea.extent = swapchain.extent();
            render.clearValueCount = 1;
            render.pClearValues = &clear;
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
            const rendering::VertexLayout* vertexLayout)
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
            const uint32_t setCount = uniforms.empty() ? 0 : uniforms.back()->binding.set + 1;
            result->sets.resize(setCount);
            for (uint32_t set = 0; set < setCount; ++set)
            {
                std::vector<VkDescriptorSetLayoutBinding> bindings;
                for (auto uniform : uniforms)
                    if (uniform->binding.set == set)
                        bindings.push_back({ uniform->binding.binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr });
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
            const rendering::VertexLayout* vertexLayout = nullptr)
        {
            if ((vertices == nullptr) != (vertexLayout == nullptr))
                throw std::invalid_argument("Vulkan mesh data and vertex layout must be provided together");
            if (vertices && (vertices->empty() || !vertexLayout->valid() ||
                vertices->elementStride != vertexLayout->stride ||
                vertices->elementCount > std::numeric_limits<uint32_t>::max() ||
                vertices->elementCount > std::numeric_limits<size_t>::max() / vertexLayout->stride ||
                vertices->byteSize != vertices->elementCount * vertexLayout->stride))
                throw std::invalid_argument("Vulkan mesh data does not match its explicit vertex layout");
            if (vertexLayout && (vertexLayout->stride > limits.maxVertexInputBindingStride ||
                vertexLayout->attributes.size() > limits.maxVertexInputAttributes))
                throw std::invalid_argument("Vulkan mesh layout exceeds device vertex input limits");
            if (vertexLayout)
                for (const auto& attribute : vertexLayout->attributes)
                    if (attribute.location >= limits.maxVertexInputAttributes ||
                        attribute.offset > limits.maxVertexInputAttributeOffset)
                        throw std::invalid_argument("Vulkan mesh attribute exceeds device vertex input limits");
            auto uniforms = uniformLayout(shader);
            auto& program = pipeline(shader, uniforms, vertexLayout);
            auto draw = std::make_unique<Draw>(device);
            std::vector<VkDescriptorSet> sets(program.sets.size());
            if (!sets.empty())
            {
                VkDescriptorPoolSize size { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(uniforms.size()) };
                VkDescriptorPoolCreateInfo pool { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
                pool.maxSets = static_cast<uint32_t>(sets.size());
                pool.poolSizeCount = 1;
                pool.pPoolSizes = &size;
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
