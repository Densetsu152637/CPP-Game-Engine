#version 450
layout(set = 0, binding = 0, std140) uniform Transform { mat4 model; } transform;
layout(set = 0, binding = 2, std140) uniform Camera { mat4 viewProjection; } camera;
const vec2 positions[3] = vec2[](vec2(0.0, -0.6), vec2(0.6, 0.6), vec2(-0.6, 0.6));
void main()
{
    gl_Position = camera.viewProjection * transform.model * vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
