//
// ECS-to-renderer compatibility helpers.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <tuple>
#include <utility>

#include "../ecs/processor.h"
#include "../structs/arraylist.h"
#include "renderer.h"

namespace rendering
{
    struct ShaderComponentUniform
    {
        std::string uniformName;
        ShaderUniformSlot slot;
        uint64_t frameIndex = 0;
    };

    template <typename... Components>
    class ShaderBinding
    {
        ArrayList<ShaderComponentUniform> m_uniforms;

        template <size_t Index, typename Component>
        void append_default_uniform()
        {
            m_uniforms.append(ShaderComponentUniform {
                std::string(default_component_uniform_name<Component>()),
                { 0, static_cast<uint32_t>(Index) },
                0
            });
        }

        template <size_t... Is>
        void append_default_uniforms(std::index_sequence<Is...>)
        {
            (append_default_uniform<Is, Components>(), ...);
        }

        template <size_t Index, typename Component>
        ShaderUniformUpload upload_for(const Component& component) const
        {
            using UploadValue = shader_upload_value_t<Component>;
            static_assert(
                std::is_trivially_copyable_v<UploadValue>,
                "Shader uniform uploads require trivially copyable component values"
            );

            const ShaderComponentUniform& uniform = m_uniforms[Index];
            const UploadValue& uploadValue = shader_upload_value(component);
            return ShaderUniformUpload {
                uniform.uniformName,
                uniform.slot,
                &uploadValue,
                sizeof(UploadValue),
                std::type_index(typeid(UploadValue)),
                uniform.frameIndex
            };
        }

        template <typename ComponentTuple, size_t... Is>
        ArrayList<ShaderUniformUpload> uploads_impl(
            ComponentTuple&& components,
            std::index_sequence<Is...>
        ) const {
            ArrayList<ShaderUniformUpload> uploads(sizeof...(Components));
            (uploads.append(upload_for<Is>(std::get<Is>(components))), ...);
            return uploads;
        }

    public:
        ShaderBinding()
        {
            m_uniforms.reserve(sizeof...(Components));
            append_default_uniforms(std::index_sequence_for<Components...>{});
        }

        explicit ShaderBinding(std::initializer_list<std::string_view> uniformNames)
        {
            if (uniformNames.size() != sizeof...(Components))
                throw std::invalid_argument("ShaderBinding uniform count must match component count");

            m_uniforms.reserve(sizeof...(Components));
            size_t index = 0;
            for (const std::string_view uniformName : uniformNames)
            {
                if (uniformName.empty())
                    throw std::invalid_argument("ShaderBinding uniform names cannot be empty");

                m_uniforms.append(ShaderComponentUniform {
                    std::string(uniformName),
                    { 0, static_cast<uint32_t>(index) },
                    0
                });
                ++index;
            }
        }

        size_t size() const
        { return m_uniforms.length(); }

        ShaderComponentUniform& operator[](const size_t index)
        { return m_uniforms[index]; }

        const ShaderComponentUniform& operator[](const size_t index) const
        { return m_uniforms[index]; }

        template <typename... Values>
        ArrayList<ShaderUniformUpload> uploads(const Values&... values) const
        {
            static_assert(
                sizeof...(Values) == sizeof...(Components),
                "ShaderBinding upload component count mismatch"
            );

            auto componentTuple = std::forward_as_tuple(values...);
            return uploads_impl(componentTuple, std::index_sequence_for<Components...>{});
        }
    };

    template <typename... Args, typename... Components>
    ECSProcessor& queue_shader_rendering(
        ECSProcessor& sim,
        const std::string& name,
        IRenderer* renderer,
        IShader* shader,
        ShaderBinding<Components...> binding
    ) {
        if (nullptr == renderer)
            throw std::invalid_argument("queue_shader_rendering requires a renderer");

        if (nullptr == shader)
            throw std::invalid_argument("queue_shader_rendering requires a shader");

        return sim.template queue_into_rendering<Args...>(
            name,
            [renderer, shader, binding = std::move(binding)](const Components&... components) mutable
            {
                ArrayList<ShaderUniformUpload> uploads = binding.uploads(components...);
                renderer->render(*shader, uploads);
            }
        );
    }
}
