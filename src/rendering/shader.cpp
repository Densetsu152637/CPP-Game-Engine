//
// Backend-neutral shader source, object, and uniform upload API.
//

#include "shader.h"

#include <algorithm>
#ifndef CPP_GAME_ENGINE_MOBILE
#include <cstdlib>
#endif
#include <fstream>
#include <stdexcept>
#include <utility>

namespace rendering
{
    namespace
    {
        std::vector<char> read_binary_file(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::ate | std::ios::binary);
            if (!file.is_open())
                throw std::runtime_error("Failed to open shader file: " + path.string());

            const std::streamsize fileSize = file.tellg();
            if (fileSize <= 0)
                throw std::runtime_error("Shader file is empty: " + path.string());

            std::vector<char> bytes(static_cast<size_t>(fileSize));
            file.seekg(0);
            file.read(bytes.data(), fileSize);
            if (!file)
                throw std::runtime_error("Failed to read shader file: " + path.string());

            return bytes;
        }

#ifndef CPP_GAME_ENGINE_MOBILE
        const char* glslc_stage_name(const ShaderStage stage)
        {
            switch (stage)
            {
                case ShaderStage::Vertex:
                    return "vert";
                case ShaderStage::Fragment:
                    return "frag";
                case ShaderStage::Compute:
                    return "comp";
                case ShaderStage::Geometry:
                    return "geom";
                case ShaderStage::TessellationControl:
                    return "tesc";
                case ShaderStage::TessellationEvaluation:
                    return "tese";
            }

            throw std::invalid_argument("Unsupported GLSL shader stage");
        }

        std::string quote_path(const std::filesystem::path& path)
        {
            return "\"" + path.string() + "\"";
        }
#endif
    }

    ShaderUniformWrite capture_uniform_write(
        const ShaderUniformUpload& upload,
        const std::string_view shaderName
    ) {
        if (upload.uniformName.empty())
            throw std::invalid_argument("Shader uniform upload requires a uniform name");

        if (nullptr == upload.data)
            throw std::invalid_argument("Shader uniform upload data cannot be null");

        if (0 == upload.byteSize)
            throw std::invalid_argument("Shader uniform upload byte size cannot be zero");

        ShaderUniformWrite write;
        write.shaderName = shaderName;
        write.uniformName = upload.uniformName;
        write.slot = upload.slot;
        write.bytes.resize(upload.byteSize);
        write.valueType = upload.valueType;
        write.frameIndex = upload.frameIndex;

        const auto* source = static_cast<const std::byte*>(upload.data);
        std::copy(source, source + upload.byteSize, write.bytes.begin());
        return write;
    }

    void validateSpirvSource(const ShaderSource& source)
    {
        if (source.language != ShaderLanguage::Spirv)
            throw std::invalid_argument("Shader module creation requires SPIR-V bytecode");

        if (source.bytes.empty() || 0 != source.bytes.size() % sizeof(uint32_t))
            throw std::invalid_argument("SPIR-V bytecode size must be a non-zero multiple of uint32_t");
    }

    ShaderSource loadGlslShader(const ShaderStage stage, const std::filesystem::path& path)
    {
        ShaderSource source;
        source.stage = stage;
        source.language = ShaderLanguage::Glsl;
        source.path = path;
        source.bytes = read_binary_file(path);
        return source;
    }

    ShaderSource loadSpirvShader(const ShaderStage stage, const std::filesystem::path& path)
    {
        ShaderSource source;
        source.stage = stage;
        source.language = ShaderLanguage::Spirv;
        source.path = path;
        source.bytes = read_binary_file(path);
        validateSpirvSource(source);
        return source;
    }

    ShaderSource compileGlslToSpirv(
        const ShaderStage stage,
        const std::filesystem::path& glslPath,
        const std::filesystem::path& spirvOutputPath,
        const std::filesystem::path& compilerExecutable
    ) {
#ifdef CPP_GAME_ENGINE_MOBILE
        (void)stage;
        (void)glslPath;
        (void)spirvOutputPath;
        (void)compilerExecutable;
        throw ShaderCompilerUnavailable();
#else
        const std::string command =
            quote_path(compilerExecutable) +
            " -fshader-stage=" + glslc_stage_name(stage) +
            " " + quote_path(glslPath) +
            " -o " + quote_path(spirvOutputPath);

        const int result = std::system(command.c_str());
        if (0 != result)
            throw std::runtime_error("GLSL compilation failed: " + glslPath.string());

        return loadSpirvShader(stage, spirvOutputPath);
#endif
    }

    IShader::IShader(std::string name)
        : m_name(std::move(name))
    {}

    IShader& IShader::addSource(ShaderSource source)
    {
        if (source.empty())
            throw std::invalid_argument("Cannot add an empty shader source");

        m_sources.append(std::move(source));
        return *this;
    }

    IShader& IShader::addGlsl(const ShaderStage stage, const std::filesystem::path& path)
    {
        return addSource(loadGlslShader(stage, path));
    }

    IShader& IShader::addSpirv(const ShaderStage stage, const std::filesystem::path& path)
    {
        return addSource(loadSpirvShader(stage, path));
    }

    IShader& IShader::addCompiledGlsl(
        const ShaderStage stage,
        const std::filesystem::path& glslPath,
        const std::filesystem::path& spirvOutputPath,
        const std::filesystem::path& compilerExecutable
    ) {
        return addSource(compileGlslToSpirv(stage, glslPath, spirvOutputPath, compilerExecutable));
    }

    bool IShader::uploadUniformBytes(const ShaderUniformUpload& upload)
    {
        if (upload.uniformName.empty())
            throw std::invalid_argument("Shader uniform upload requires a uniform name");

        if (nullptr == upload.data)
            throw std::invalid_argument("Shader uniform upload data cannot be null");

        if (0 == upload.byteSize)
            throw std::invalid_argument("Shader uniform upload byte size cannot be zero");

        const std::string uniformName(upload.uniformName);
        if (!uniforms.contains(uniformName))
            uniforms.declare(uniformName, upload.slot, upload.byteSize, UniformKind::Bytes);

        const bool changed = uniforms.setBytes(
            uniformName,
            upload.data,
            upload.byteSize,
            upload.frameIndex
        );
        m_lastTouchedFrame = std::max(m_lastTouchedFrame, upload.frameIndex);
        return changed || uniforms.dirty(uniformName);
    }

    void IShader::markUploaded()
    {
        uniforms.markAllUploaded(m_lastTouchedFrame);
    }
}
