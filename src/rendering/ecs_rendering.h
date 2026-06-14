//
// ECS-to-renderer compatibility helpers.
//

#pragma once

#include <stdexcept>
#include <string>
#include <utility>

#include "../ecs/jobs/ecs_sim_arg_traits.h"
#include "../ecs/processor.h"
#include "../structs/arraylist.h"
#include "renderer.h"

namespace rendering
{
    namespace detail
    {
        template <typename ComponentList>
        struct ShaderRenderingQueue;

        template <typename... Components>
        struct ShaderRenderingQueue<ecs_sim::type_list<Components...>>
        {
            template <typename... Args>
            static ECSProcessor& queue(
                ECSProcessor& sim,
                const std::string& name,
                IRenderer* renderer,
                IShader* shader
            ) {
                if (nullptr == renderer)
                    throw std::invalid_argument("queue_shader_rendering requires a renderer");

                if (nullptr == shader)
                    throw std::invalid_argument("queue_shader_rendering requires a shader");

                return sim.template queue_into_rendering<Args...>(
                    name,
                    [renderer, shader](const ecs::component_value_t<Components>&... components)
                    {
                        ArrayList<ShaderUniformUpload> uploads =
                            shader->uploadsForComponents<Components...>(components...);
                        renderer->render(*shader, uploads);
                    }
                );
            }
        };
    }

    template <typename... Args>
    ECSProcessor& queue_shader_rendering(
        ECSProcessor& sim,
        const std::string& name,
        IRenderer* renderer,
        IShader* shader
    ) {
        using Components = ecs_sim::component_list_t<Args...>;
        return detail::ShaderRenderingQueue<Components>::template queue<Args...>(
            sim,
            name,
            renderer,
            shader
        );
    }
}
