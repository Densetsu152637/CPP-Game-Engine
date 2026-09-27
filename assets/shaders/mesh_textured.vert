#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;
layout(set = 0, binding = 0, std140) uniform Transform { mat4 model; } transform;
layout(set = 0, binding = 2, std140) uniform Camera { mat4 viewProjection; } camera;
layout(location = 0) out vec2 uv;
void main()
{
    uv = inUv;
    gl_Position = camera.viewProjection * transform.model * vec4(inPosition, 1.0);
}
