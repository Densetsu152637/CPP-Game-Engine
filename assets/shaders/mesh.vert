#version 450
layout(location = 0) in vec3 inPosition;
layout(set = 0, binding = 0, std140) uniform Transform { mat4 model; } transform;
void main()
{
    gl_Position = transform.model * vec4(inPosition, 1.0);
}
