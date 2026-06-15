#include "test/ecs/ecs_test_fixtures.h"

#include <cstdint>
#include <typeindex>
#include <vector>

#include "rendering/gpu_buffer.h"

using namespace ecs_test;

namespace
{
    struct TestVertex
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        uint32_t colour = 0;
    };
}

void test_vulkan_uniform_registry_tracks_dirty_values()
{
    rendering::UniformRegistry uniforms;
    uniforms.declare<int>("u_mode", {0, 0}, rendering::UniformKind::Int);

    require(uniforms.anyDirty(), "new uniform declarations should start dirty");
    require(uniforms.dirtyUploads().length() == 1, "dirty uniform upload enumeration missed declaration");

    uniforms.markAllUploaded(1);
    require(!uniforms.anyDirty(), "markAllUploaded did not clear dirty state");

    require(uniforms.set("u_mode", 7, 2), "changed uniform value did not mark dirty");
    require(uniforms.dirty("u_mode"), "changed uniform was not dirty");
    require(!uniforms.set("u_mode", 7, 3), "unchanged uniform value caused a false dirty write");
    require(uniforms.dirty("u_mode"), "unchanged write should not clear existing dirty state");

    const int* mode = uniforms.try_get<int>("u_mode");
    require(nullptr != mode && *mode == 7, "uniform registry returned the wrong stored value");

    uniforms.markUploaded("u_mode", 4);
    require(!uniforms.anyDirty(), "markUploaded did not clear dirty state");
    require_throws(
        [&]()
        {
            const double wrongSize = 1.0;
            uniforms.set("u_mode", wrongSize);
        },
        "uniform registry accepted a write with the wrong byte size"
    );
}

void test_arraylist_serializes_for_gpu_buffers()
{
    ArrayList<TestVertex> vertices;
    vertices.append(TestVertex{1.0f, 2.0f, 3.0f, 0xff00ff00u});
    vertices.append(TestVertex{4.0f, 5.0f, 6.0f, 0xff0000ffu});

    const rendering::SerializedBufferView view = rendering::serialized_buffer_view(vertices);

    require(view.data == reinterpret_cast<const std::byte*>(vertices.ptr().ptr), "ArrayList GPU view did not point at contiguous storage");
    require(view.byteSize == sizeof(TestVertex) * 2, "ArrayList GPU view had the wrong byte size");
    require(view.elementCount == 2, "ArrayList GPU view had the wrong element count");
    require(view.elementStride == sizeof(TestVertex), "ArrayList GPU view had the wrong stride");
    require(view.elementType == std::type_index(typeid(TestVertex)), "ArrayList GPU view had the wrong element type");

    auto* vertex = reinterpret_cast<const TestVertex*>(view.data);
    require(vertex[1].z == 6.0f, "ArrayList GPU view did not preserve serialized bytes");
}

void test_std_vector_serializes_for_gpu_buffers()
{
    const std::vector<uint16_t> indices { 0, 1, 2, 2, 3, 0 };
    const rendering::SerializedBufferView view = rendering::serialized_buffer_view(indices);

    require(view.data == reinterpret_cast<const std::byte*>(indices.data()), "std::vector GPU view did not point at contiguous storage");
    require(view.byteSize == sizeof(uint16_t) * indices.size(), "std::vector GPU view had the wrong byte size");
    require(view.elementCount == indices.size(), "std::vector GPU view had the wrong element count");
    require(view.elementStride == sizeof(uint16_t), "std::vector GPU view had the wrong stride");
    require(view.elementType == std::type_index(typeid(uint16_t)), "std::vector GPU view had the wrong element type");

    auto* index = reinterpret_cast<const uint16_t*>(view.data);
    require(index[4] == 3, "std::vector GPU view did not preserve serialized bytes");
}

void test_renderer_template_uploads_alias_value()
{
    TestRenderer renderer;
    TestShader shader("alias-shader");

    const Position position(42);
    require(
        renderer.upload<Position>(shader, "u_position", position),
        "renderer template upload returned false"
    );

    require(renderer.writes.length() == 1, "renderer did not capture the uniform upload");
    const rendering::ShaderUniformWrite& write = renderer.writes[0];
    require(write.shaderName == "alias-shader", "renderer upload did not use the active shader");
    require(write.uniformName == "u_position", "renderer upload used the wrong uniform name");
    require(write.size() == sizeof(int), "renderer uploaded the ECS alias wrapper instead of its value");
    require(read_int_uniform(write) == 42, "renderer uploaded the wrong alias value");
}

void test_queue_shader_rendering_uploads_filtered_render_components()
{
    Threadpool pool(1, std::string("ecs-test"));
    ECSProcessor processor(pool);
    ECS& ecs = processor.ecs();

    const Entity first = ecs.createEntity();
    const Entity second = ecs.createEntity();
    ecs.emplaceComponent<Position>(first, 3);
    ecs.emplaceComponent<Position>(second, 9);
    ecs.addTag<RenderableTag>(first);

    TestRenderer renderer;
    TestShader shader("position-shader");
    shader.bindComponent<Position>("u_position");

    rendering::queue_shader_rendering<Position, ecs::Tag<RenderableTag>>(
        processor,
        "draw-position",
        &renderer,
        &shader
    );

    processor.simulate();
    processor.render();
    renderer.clear();

    processor.render();
    require(renderer.writes.length() == 1, "shader rendering helper did not filter render entities");
    require(renderer.renderCalls == 1, "shader rendering helper did not issue one render call");
    require(renderer.renderedShaders[0] == "position-shader", "shader rendering helper rendered the wrong shader");

    const rendering::ShaderUniformWrite& write = renderer.writes[0];
    require(write.shaderName == "position-shader", "shader rendering helper used the wrong shader");
    require(write.uniformName == "u_position", "shader rendering helper used the wrong uniform");
    require(read_int_uniform(write) == 3, "shader rendering helper uploaded the wrong component value");
}

void test_queue_shader_rendering_uploads_multiple_components()
{
    Threadpool pool(1, std::string("ecs-test"));
    ECSProcessor processor(pool);
    ECS& ecs = processor.ecs();

    const Entity entity = ecs.createEntity();
    ecs.emplaceComponent<Position>(entity, 4);
    ecs.emplaceComponent<Velocity>(entity, 8);
    ecs.addTag<RenderableTag>(entity);

    TestRenderer renderer;
    TestShader shader("multi-component-shader");
    shader
        .bindComponent<Position>("u_position")
        .bindComponent<Velocity>("u_velocity");

    rendering::queue_shader_rendering<Position, Velocity, ecs::Tag<RenderableTag>>(
        processor,
        "draw-position-velocity",
        &renderer,
        &shader
    );

    processor.simulate();
    processor.render();
    renderer.clear();

    processor.render();
    require(renderer.writes.length() == 2, "shader rendering helper did not upload both components");
    require(renderer.renderCalls == 1, "shader rendering helper rendered more than once for one entity");
    require(renderer.renderedShaders[0] == "multi-component-shader", "multi-component helper rendered the wrong shader");

    require(renderer.writes[0].uniformName == "u_position", "first component used the wrong uniform");
    require(renderer.writes[1].uniformName == "u_velocity", "second component used the wrong uniform");
    require(read_int_uniform(renderer.writes[0]) == 4, "first component upload had the wrong value");
    require(read_int_uniform(renderer.writes[1]) == 8, "second component upload had the wrong value");
}
