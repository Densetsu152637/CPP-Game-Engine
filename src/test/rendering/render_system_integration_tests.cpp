#include <cstring>
#include <string>

#include "ecs/aliases/component_alias.h"
#include "ecs/processor.h"
#include "rendering/ecs_rendering.h"
#include "test/rendering/rendering_test_fakes.h"
#include "test/test_assertions.h"

namespace
{
    struct RenderPositionTag {};
    struct RenderVelocityTag {};
    struct RenderMeshTag {};

    using RenderPosition = ecs::Alias<int, RenderPositionTag>;
    using RenderVelocity = ecs::Alias<int, RenderVelocityTag>;

    struct RenderMeshValue
    {
        int id = 0;

        bool operator==(const RenderMeshValue&) const = default;
    };

    using RenderMesh = ecs::SharedAlias<RenderMeshValue, RenderMeshTag>;

    int read_int_write(const rendering::ShaderUniformWrite& write)
    {
        int value = 0;
        std::memcpy(&value, write.bytes.data(), sizeof(value));
        return value;
    }
}

void test_rendering_helper_uses_shader_owned_bindings()
{
    Threadpool pool(1, std::string("render-integration-test"));
    ECSProcessor processor(pool);
    ECS& ecs = processor.ecs();

    const Entity entity = ecs.createEntity();
    ecs.emplaceComponent<RenderPosition>(entity, 4);
    ecs.emplaceComponent<RenderVelocity>(entity, 6);

    TestRenderer renderer;
    TestShader shader("owned-bindings");
    shader
        .bindComponent<RenderPosition>("u_position")
        .bindComponent<RenderVelocity>("u_velocity");

    rendering::queue_shader_rendering<RenderPosition, RenderVelocity>(
        processor,
        "draw-owned-bindings",
        &renderer,
        &shader
    );

    processor.simulate();
    processor.render();
    renderer.clear();

    processor.render();
    test::require(renderer.renderCalls == 1, "render helper did not render the shader once");
    test::require(renderer.writes.length() == 2, "render helper did not upload all shader-owned bindings");
    test::require(renderer.writes[0].uniformName == "u_position", "render helper used the wrong position uniform");
    test::require(renderer.writes[1].uniformName == "u_velocity", "render helper used the wrong velocity uniform");
    test::require(read_int_write(renderer.writes[0]) == 4, "render helper uploaded the wrong position");
    test::require(read_int_write(renderer.writes[1]) == 6, "render helper uploaded the wrong velocity");
}

void test_rendering_helper_supports_shared_mesh_batches()
{
    Threadpool pool(2, std::string("render-shared-integration-test"));
    ECSProcessor processor(pool);
    ECS& ecs = processor.ecs();

    const Entity first = ecs.createEntity();
    const Entity second = ecs.createEntity();
    const Entity missingMesh = ecs.createEntity();

    ecs.emplaceComponent<RenderPosition>(first, 1);
    ecs.emplaceComponent<RenderPosition>(second, 2);
    ecs.emplaceComponent<RenderPosition>(missingMesh, 99);
    ecs.emplaceComponent<RenderMesh>(first, RenderMeshValue{7});
    ecs.emplaceComponent<RenderMesh>(second, RenderMeshValue{8});

    TestRenderer renderer;
    TestShader shader("shared-mesh-render");
    shader.bindComponent<RenderPosition>("u_position");

    rendering::queue_shader_rendering<RenderPosition, ecs::Shared<RenderMesh>>(
        processor,
        "draw-shared-mesh",
        &renderer,
        &shader
    );

    processor.simulate();
    processor.render();
    renderer.clear();

    processor.render();
    test::require(renderer.renderCalls == 2, "shared render helper did not render one call per matched entity");
    test::require(renderer.writes.length() == 2, "shared render helper uploaded the wrong number of entity uniforms");
    test::require(read_int_write(renderer.writes[0]) + read_int_write(renderer.writes[1]) == 3, "shared render helper rendered an entity without a mesh");
}
