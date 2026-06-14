//
// Backend-neutral shader source, object, and uniform upload API.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "../ecs/aliases/component_alias_traits.h"
#include "../structs/arraylist.h"
#include "uniform_registry.h"

namespace rendering
{
    inline constexpr std::string_view DEFAULT_COMPONENT_UNIFORM_NAME = "component";

    using ShaderUniformSlot = UniformBinding;

    struct ShaderUniformUpload
    {
        std::string_view uniformName = DEFAULT_COMPONENT_UNIFORM_NAME;
        ShaderUniformSlot slot;
        const void* data = nullptr;
        size_t byteSize = 0;
        std::type_index valueType { typeid(void) };
        uint64_t frameIndex = 0;
    };

    struct ShaderUniformWrite
    {
        std::string shaderName;
        std::string uniformName;
        ShaderUniformSlot slot;
        std::vector<std::byte> bytes;
        std::type_index valueType { typeid(void) };
        uint64_t frameIndex = 0;

        size_t size() const
        { return bytes.size(); }
    };

    ShaderUniformWrite capture_uniform_write(
        const ShaderUniformUpload& upload,
        std::string_view shaderName
    );

    struct ShaderComponentUniform
    {
        std::string uniformName;
        ShaderUniformSlot slot;
        uint64_t frameIndex = 0;
    };

    template <typename T>
    struct ComponentUniformName
    {
        static constexpr std::string_view value = DEFAULT_COMPONENT_UNIFORM_NAME;
    };

    template <typename T>
    constexpr std::string_view default_component_uniform_name()
    {
        return ComponentUniformName<std::remove_cvref_t<T>>::value;
    }

    template <typename T>
    using shader_upload_value_t = std::conditional_t<
        ecs::is_component_alias_v<T>,
        ecs::alias_value_t<T>,
        std::remove_cvref_t<T>
    >;

    template <typename T>
    decltype(auto) shader_upload_value(const T& value)
    {
        if constexpr (ecs::is_component_alias_v<T>)
            return static_cast<const shader_upload_value_t<T>&>(value.read());
        else
            return static_cast<const shader_upload_value_t<T>&>(value);
    }

    enum class ShaderStage
    {
        Vertex,
        Fragment,
        Compute,
        Geometry,
        TessellationControl,
        TessellationEvaluation
    };

    enum class ShaderLanguage
    {
        Glsl,
        Spirv
    };

    struct ShaderSource
    {
        ShaderStage stage = ShaderStage::Vertex;
        ShaderLanguage language = ShaderLanguage::Glsl;
        std::string entryPoint = "main";
        std::filesystem::path path;
        std::vector<char> bytes;

        bool empty() const
        { return bytes.empty(); }
    };

    ShaderSource loadGlslShader(ShaderStage stage, const std::filesystem::path& path);
    ShaderSource loadSpirvShader(ShaderStage stage, const std::filesystem::path& path);
    ShaderSource compileGlslToSpirv(
        ShaderStage stage,
        const std::filesystem::path& glslPath,
        const std::filesystem::path& spirvOutputPath,
        const std::filesystem::path& compilerExecutable = "glslc"
    );
    void validateSpirvSource(const ShaderSource& source);

    class IShader
    {
        std::string m_name;
        ArrayList<ShaderSource> m_sources;
        ArrayList<ShaderComponentUniform> m_componentUniforms;
        std::unordered_map<std::type_index, size_t> m_componentUniformIndices;
        uint64_t m_lastTouchedFrame = 0;

    public:
        UniformRegistry uniforms;

        explicit IShader(std::string name = {});
        virtual ~IShader() = default;

        IShader(const IShader&) = delete;
        IShader& operator=(const IShader&) = delete;
        IShader(IShader&&) noexcept = default;
        IShader& operator=(IShader&&) noexcept = default;

        virtual std::string_view backendName() const = 0;

        const std::string& name() const
        { return m_name; }

        const ArrayList<ShaderSource>& sources() const
        { return m_sources; }

        uint64_t lastTouchedFrame() const
        { return m_lastTouchedFrame; }

        const ArrayList<ShaderComponentUniform>& componentUniforms() const
        { return m_componentUniforms; }

        IShader& addSource(ShaderSource source);
        IShader& addGlsl(ShaderStage stage, const std::filesystem::path& path);
        IShader& addSpirv(ShaderStage stage, const std::filesystem::path& path);
        IShader& addCompiledGlsl(
            ShaderStage stage,
            const std::filesystem::path& glslPath,
            const std::filesystem::path& spirvOutputPath,
            const std::filesystem::path& compilerExecutable = "glslc"
        );

        bool uploadUniformBytes(const ShaderUniformUpload& upload);
        void markUploaded();

        template <typename Component>
        IShader& bindComponent(
            std::string uniformName,
            const ShaderUniformSlot slot,
            const uint64_t frameIndex = 0
        ) {
            if (uniformName.empty())
                throw std::invalid_argument("Shader component uniform name cannot be empty");

            const std::type_index componentType(typeid(std::remove_cvref_t<Component>));
            const auto existing = m_componentUniformIndices.find(componentType);
            if (existing != m_componentUniformIndices.end())
            {
                ShaderComponentUniform& uniform = m_componentUniforms[existing->second];
                uniform.uniformName = std::move(uniformName);
                uniform.slot = slot;
                uniform.frameIndex = frameIndex;
                return *this;
            }

            const size_t index = m_componentUniforms.length();
            m_componentUniforms.append(ShaderComponentUniform {
                std::move(uniformName),
                slot,
                frameIndex
            });
            m_componentUniformIndices.emplace(componentType, index);
            return *this;
        }

        template <typename Component>
        IShader& bindComponent(std::string uniformName)
        {
            return bindComponent<Component>(
                std::move(uniformName),
                { 0, static_cast<uint32_t>(m_componentUniforms.length()) },
                0
            );
        }

        template <typename Component>
        const ShaderComponentUniform* componentUniform() const
        {
            const std::type_index componentType(typeid(std::remove_cvref_t<Component>));
            const auto it = m_componentUniformIndices.find(componentType);
            return it == m_componentUniformIndices.end() ? nullptr : &m_componentUniforms[it->second];
        }

        template <typename Component>
        ShaderUniformUpload uploadForComponent(const Component& component) const
        {
            using UploadValue = shader_upload_value_t<Component>;
            static_assert(
                std::is_trivially_copyable_v<UploadValue>,
                "Shader uniform uploads require trivially copyable component values"
            );

            const ShaderComponentUniform* uniform = componentUniform<Component>();
            if (nullptr == uniform)
                throw std::out_of_range("Shader has no bound uniform for component");

            const UploadValue& uploadValue = shader_upload_value(component);
            return ShaderUniformUpload {
                uniform->uniformName,
                uniform->slot,
                &uploadValue,
                sizeof(UploadValue),
                std::type_index(typeid(UploadValue)),
                uniform->frameIndex
            };
        }

        template <typename... Components>
        ArrayList<ShaderUniformUpload> uploadsForComponents(const Components&... components) const
        {
            ArrayList<ShaderUniformUpload> uploads(sizeof...(Components));
            (uploads.append(uploadForComponent<Components>(components)), ...);
            return uploads;
        }

        template <typename T>
        bool upload(
            const std::string_view uniformName,
            const T& value,
            const uint64_t frameIndex = 0,
            const ShaderUniformSlot slot = {}
        ) {
            using UploadValue = shader_upload_value_t<T>;
            static_assert(
                std::is_trivially_copyable_v<UploadValue>,
                "Shader uniform uploads require trivially copyable values"
            );

            const UploadValue& uploadValue = shader_upload_value(value);
            return uploadUniformBytes(ShaderUniformUpload {
                uniformName,
                slot,
                &uploadValue,
                sizeof(UploadValue),
                std::type_index(typeid(UploadValue)),
                frameIndex
            });
        }

        template <typename T>
        bool upload(const T& value, const uint64_t frameIndex = 0)
        {
            return upload<T>(default_component_uniform_name<T>(), value, frameIndex);
        }
    };
}
