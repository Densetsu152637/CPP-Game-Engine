//
// Backend-neutral renderer interface used by ECS rendering jobs.
//

#pragma once

#include <mutex>
#include <string_view>
#include <type_traits>
#include <typeindex>

#include "../core/interfaces.h"
#include "../structs/arraylist.h"
#include "shader.h"

using IShader = rendering::IShader;

class IRenderer : public IRenderElement
{
    mutable std::mutex m_renderMutex;

    bool uploadUniformUnlocked(rendering::IShader& shader, const rendering::ShaderUniformUpload& upload);

protected:
    virtual bool uploadUniformImpl(
        rendering::IShader& shader,
        const rendering::ShaderUniformUpload& upload
    ) = 0;
    virtual void renderImpl(rendering::IShader& shader) = 0;

public:
    ~IRenderer() override = default;

    IRenderer() = default;
    IRenderer(const IRenderer&) = delete;
    IRenderer& operator=(const IRenderer&) = delete;

    bool uploadUniform(rendering::IShader& shader, const rendering::ShaderUniformUpload& upload);
    bool uploadUniforms(
        rendering::IShader& shader,
        const ArrayList<rendering::ShaderUniformUpload>& uploads
    );

    void render() override;
    void render(rendering::IShader& shader);
    bool render(
        rendering::IShader& shader,
        const ArrayList<rendering::ShaderUniformUpload>& uploads
    );

    template <typename T>
    bool upload(
        rendering::IShader& shader,
        const T& value,
        const uint64_t frameIndex = 0
    ) {
        return upload<T>(
            shader,
            rendering::default_component_uniform_name<T>(),
            value,
            frameIndex
        );
    }

    template <typename T>
    bool upload(
        rendering::IShader& shader,
        const std::string_view uniformName,
        const T& value,
        const uint64_t frameIndex = 0,
        const rendering::ShaderUniformSlot slot = {}
    ) {
        using UploadValue = rendering::shader_upload_value_t<T>;
        static_assert(
            std::is_trivially_copyable_v<UploadValue>,
            "Shader uniform uploads require trivially copyable values"
        );

        const UploadValue& uploadValue = rendering::shader_upload_value(value);
        return uploadUniform(shader, rendering::ShaderUniformUpload {
            uniformName,
            slot,
            &uploadValue,
            sizeof(UploadValue),
            std::type_index(typeid(UploadValue)),
            frameIndex
        });
    }
};
