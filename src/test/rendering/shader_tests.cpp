#include <cstring>
#include <string>

#include "ecs/aliases/component_alias.h"
#include "rendering/camera.h"
#include "rendering/content.h"
#include "rendering/gpu_buffer.h"
#include "rendering/material.h"
#include "rendering/texture.h"
#include "test/rendering/rendering_test_fakes.h"
#include "test/test_assertions.h"

namespace
{
    struct ShaderPositionTag {};
    struct ShaderVelocityTag {};

    using ShaderPosition = ecs::Alias<int, ShaderPositionTag>;
    using ShaderVelocity = ecs::Alias<int, ShaderVelocityTag>;

    int read_int_upload(const rendering::ShaderUniformUpload& upload)
    {
        int value = 0;
        std::memcpy(&value, upload.data, sizeof(value));
        return value;
    }
}

void test_shader_component_bindings_create_uploads()
{
    TestShader shader("shader-bindings");
    shader
        .bindComponent<ShaderPosition>("u_position")
        .bindComponent<ShaderVelocity>("u_velocity", {1, 3}, 7);

    const ShaderPosition position(11);
    const ShaderVelocity velocity(5);
    ArrayList<rendering::ShaderUniformUpload> uploads =
        shader.uploadsForComponents<ShaderPosition, ShaderVelocity>(position, velocity);

    test::require(uploads.length() == 2, "shader did not create one upload per bound component");
    test::require(uploads[0].uniformName == "u_position", "position binding used the wrong uniform");
    test::require(uploads[1].uniformName == "u_velocity", "velocity binding used the wrong uniform");
    test::require(uploads[1].slot.set == 1 && uploads[1].slot.binding == 3, "shader binding did not preserve the slot");
    test::require(uploads[1].frameIndex == 7, "shader binding did not preserve the frame index");
    test::require(read_int_upload(uploads[0]) == 11, "position upload copied the wrong value");
    test::require(read_int_upload(uploads[1]) == 5, "velocity upload copied the wrong value");
}

void test_shader_component_binding_redeclaration_updates_slot()
{
    TestShader shader("shader-rebind");
    shader.bindComponent<ShaderPosition>("u_position", {0, 0}, 1);
    shader.bindComponent<ShaderPosition>("u_position_next", {2, 4}, 9);

    const ShaderPosition position(42);
    rendering::ShaderUniformUpload upload = shader.uploadForComponent(position);

    test::require(shader.componentUniforms().length() == 1, "shader duplicate component binding created a second entry");
    test::require(upload.uniformName == "u_position_next", "shader duplicate binding did not update the name");
    test::require(upload.slot.set == 2 && upload.slot.binding == 4, "shader duplicate binding did not update the slot");
    test::require(upload.frameIndex == 9, "shader duplicate binding did not update the frame index");
}

void test_shader_component_upload_requires_binding()
{
    TestShader shader("shader-missing-binding");
    const ShaderPosition position(3);

    test::require_throws(
        [&]()
        {
            (void)shader.uploadForComponent(position);
        },
        "shader accepted a component upload without a binding"
    );

    rendering::VertexLayout positionColor;
    positionColor.stride = sizeof(float) * 7;
    positionColor.attributes = {
        { 0, rendering::VertexAttributeFormat::Float3, 0 },
        { 1, rendering::VertexAttributeFormat::Float4, sizeof(float) * 3 }
    };
    test::require(positionColor.valid(), "valid explicit position/color vertex layout was rejected");
    positionColor.attributes[1].location = 0;
    test::require(!positionColor.valid(), "duplicate vertex attribute locations were accepted");
    positionColor.attributes[1] = { 1, rendering::VertexAttributeFormat::Float4, sizeof(float) * 4 };
    test::require(!positionColor.valid(), "vertex attribute extending past the stride was accepted");
    const rendering::CameraUniform camera;
    test::require(camera.viewProjection[0] == 1.0f && camera.viewProjection[5] == 1.0f &&
        camera.viewProjection[10] == 1.0f && camera.viewProjection[15] == 1.0f,
        "camera uniform did not default to a column-major identity matrix");
    const rendering::MaterialUniform material;
    test::require(sizeof(material) == 16 && material.baseColor[3] == 1.0f,
        "material uniform did not match a std140 vec4 tint");
    rendering::Texture2D texture { 2, 1, {255, 0, 0, 255, 0, 255, 0, 255} };
    test::require(texture.valid(), "valid row-major RGBA8 texture was rejected");
    texture.rgba8.pop_back();
    test::require(!texture.valid(), "RGBA8 texture with an incomplete texel was accepted");

    const rendering::MeshAsset sampleMesh = rendering::loadMeshAsset(
        "examples/first-project/assets/player.mesh");
    test::require(sampleMesh.valid() && sampleMesh.vertices.size() == 3,
        "first-project mesh asset did not load as one triangle");
    test::require(sampleMesh.layout().valid() && sampleMesh.vertexData().elementStride == sizeof(rendering::MeshVertex),
        "first-project mesh asset did not retain its explicit vertex layout");
    const rendering::Texture2D sampleTexture = rendering::loadTexturePpm(
        "examples/first-project/assets/player.ppm");
    test::require(sampleTexture.valid() && sampleTexture.rgba8 == std::vector<uint8_t>({26, 179, 255, 255}),
        "first-project PPM did not load into row-major RGBA8 pixels");
    test::require_throws(
        [] { (void)rendering::loadMeshAsset("examples/first-project/assets/missing.mesh"); },
        "missing mesh asset did not report a loading error");
}
