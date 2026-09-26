#version 450
layout(set = 0, binding = 1, std140) uniform Tint { vec4 color; } tint;
layout(location = 0) out vec4 outColor;
void main()
{
    outColor = tint.color;
}
