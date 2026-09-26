//
// Vulkan shader module wrappers around backend-neutral shader objects.
//

#include "vulkan_shader.h"

#include <stdexcept>
#include <cstring>
#include <utility>

namespace vulkan
{
    VkShaderStageFlagBits to_vk_stage(const ShaderStage stage)
    {
        switch (stage)
        {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
            case ShaderStage::Vertex:
                return VK_SHADER_STAGE_VERTEX_BIT;
            case ShaderStage::Fragment:
                return VK_SHADER_STAGE_FRAGMENT_BIT;
            case ShaderStage::Compute:
                return VK_SHADER_STAGE_COMPUTE_BIT;
            case ShaderStage::Geometry:
                return VK_SHADER_STAGE_GEOMETRY_BIT;
            case ShaderStage::TessellationControl:
                return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
            case ShaderStage::TessellationEvaluation:
                return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
#else
            case ShaderStage::Vertex:
                return 0x00000001u;
            case ShaderStage::TessellationControl:
                return 0x00000002u;
            case ShaderStage::TessellationEvaluation:
                return 0x00000004u;
            case ShaderStage::Geometry:
                return 0x00000008u;
            case ShaderStage::Fragment:
                return 0x00000010u;
            case ShaderStage::Compute:
                return 0x00000020u;
#endif
        }

        throw std::invalid_argument("Unsupported Vulkan shader stage");
    }

    VulkanShaderModule::VulkanShaderModule(VkDevice device, const ShaderSource& source)
        : m_device(device),
          m_stage(source.stage),
          m_entryPoint(source.entryPoint)
    {
        rendering::validateSpirvSource(source);
        uint32_t magic = 0;
        if (source.bytes.size() >= sizeof(uint32_t))
            std::memcpy(&magic, source.bytes.data(), sizeof(magic));
        if (source.bytes.size() < 5 * sizeof(uint32_t) || magic != 0x07230203u)
            throw std::invalid_argument("Vulkan shader requires a complete SPIR-V header");
        if (source.entryPoint.empty() || source.entryPoint.find('\0') != std::string::npos)
            throw std::invalid_argument("Vulkan shader requires a valid entry point name");

#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (nullptr == m_device)
            throw std::invalid_argument("Cannot create a shader module with a null VkDevice");

        VkShaderModuleCreateInfo createInfo {};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = source.bytes.size();
        // ShaderSource stores bytes; Vulkan requires an aligned array of 32-bit words.
        std::vector<uint32_t> words(source.bytes.size() / sizeof(uint32_t));
        std::memcpy(words.data(), source.bytes.data(), source.bytes.size());
        createInfo.pCode = words.data();

        if (VK_SUCCESS != vkCreateShaderModule(m_device, &createInfo, nullptr, &m_module))
            throw std::runtime_error("Failed to create Vulkan shader module");
#else
        (void)device;
        m_module = nullptr;
        throw std::runtime_error("Vulkan shader modules require CPP_GAME_ENGINE_USE_VULKAN");
#endif
    }

    VulkanShaderModule::~VulkanShaderModule()
    {
        reset();
    }

    VulkanShaderModule::VulkanShaderModule(VulkanShaderModule&& other) noexcept
        : m_device(other.m_device),
          m_module(other.m_module),
          m_stage(other.m_stage),
          m_entryPoint(std::move(other.m_entryPoint))
    {
        other.m_device = {};
        other.m_module = {};
    }

    VulkanShaderModule& VulkanShaderModule::operator=(VulkanShaderModule&& other) noexcept
    {
        if (this == &other)
            return *this;

        reset();
        m_device = other.m_device;
        m_module = other.m_module;
        m_stage = other.m_stage;
        m_entryPoint = std::move(other.m_entryPoint);
        other.m_device = {};
        other.m_module = {};
        return *this;
    }

    void VulkanShaderModule::reset()
    {
#ifdef CPP_GAME_ENGINE_USE_VULKAN
        if (nullptr != m_device && nullptr != m_module)
            vkDestroyShaderModule(m_device, m_module, nullptr);
#endif
        m_device = {};
        m_module = {};
    }

    VulkanShaderProgram::VulkanShaderProgram(std::string name)
        : rendering::IShader(std::move(name))
    {}

    VulkanShaderProgram& VulkanShaderProgram::addSource(ShaderSource source)
    {
        rendering::IShader::addSource(std::move(source));
        return *this;
    }

    VulkanShaderProgram& VulkanShaderProgram::addGlsl(
        const ShaderStage stage,
        const std::filesystem::path& path
    ) {
        rendering::IShader::addGlsl(stage, path);
        return *this;
    }

    VulkanShaderProgram& VulkanShaderProgram::addSpirv(
        const ShaderStage stage,
        const std::filesystem::path& path
    ) {
        rendering::IShader::addSpirv(stage, path);
        return *this;
    }

    VulkanShaderProgram& VulkanShaderProgram::addCompiledGlsl(
        const ShaderStage stage,
        const std::filesystem::path& glslPath,
        const std::filesystem::path& spirvOutputPath,
        const std::filesystem::path& compilerExecutable
    ) {
        rendering::IShader::addCompiledGlsl(stage, glslPath, spirvOutputPath, compilerExecutable);
        return *this;
    }

    ArrayList<VulkanShaderModule> VulkanShaderProgram::createModules(VkDevice device) const
    {
        ArrayList<VulkanShaderModule> modules(sources().length());
        for (const ShaderSource& source : sources())
        {
            if (source.language != ShaderLanguage::Spirv)
                throw std::runtime_error(
                    "GLSL shader source must be compiled to SPIR-V before module creation: " +
                    source.path.string()
                );

            modules.emplace(device, source);
        }

        return modules;
    }
}
