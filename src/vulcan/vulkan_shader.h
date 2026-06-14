//
// Vulkan shader module wrappers around backend-neutral shader objects.
//

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#ifdef CPP_GAME_ENGINE_USE_VULKAN
#include <vulkan/vulkan.h>
#else
using VkDevice = void*;
using VkShaderModule = void*;
using VkShaderStageFlagBits = uint32_t;
#endif

#include "../rendering/shader.h"

namespace vulkan
{
    using ShaderStage = rendering::ShaderStage;
    using ShaderLanguage = rendering::ShaderLanguage;
    using ShaderSource = rendering::ShaderSource;

    using rendering::compileGlslToSpirv;
    using rendering::loadGlslShader;
    using rendering::loadSpirvShader;

    VkShaderStageFlagBits to_vk_stage(ShaderStage stage);

    class VulkanShaderModule
    {
        VkDevice m_device = {};
        VkShaderModule m_module = {};
        ShaderStage m_stage = ShaderStage::Vertex;
        std::string m_entryPoint = "main";

    public:
        VulkanShaderModule() = default;
        VulkanShaderModule(VkDevice device, const ShaderSource& source);
        ~VulkanShaderModule();

        VulkanShaderModule(const VulkanShaderModule&) = delete;
        VulkanShaderModule& operator=(const VulkanShaderModule&) = delete;
        VulkanShaderModule(VulkanShaderModule&& other) noexcept;
        VulkanShaderModule& operator=(VulkanShaderModule&& other) noexcept;

        VkShaderModule handle() const
        { return m_module; }

        ShaderStage stage() const
        { return m_stage; }

        const std::string& entryPoint() const
        { return m_entryPoint; }

        bool valid() const
        { return nullptr != m_module; }

        void reset();
    };

    class VulkanShaderProgram final : public rendering::IShader
    {
    public:
        explicit VulkanShaderProgram(std::string name = {});

        std::string_view backendName() const override
        { return "vulkan"; }

        VulkanShaderProgram& addSource(ShaderSource source);
        VulkanShaderProgram& addGlsl(ShaderStage stage, const std::filesystem::path& path);
        VulkanShaderProgram& addSpirv(ShaderStage stage, const std::filesystem::path& path);
        VulkanShaderProgram& addCompiledGlsl(
            ShaderStage stage,
            const std::filesystem::path& glslPath,
            const std::filesystem::path& spirvOutputPath,
            const std::filesystem::path& compilerExecutable = "glslc"
        );

        ArrayList<VulkanShaderModule> createModules(VkDevice device) const;
    };
}
