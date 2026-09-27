#version 450
layout(set = 0, binding = 1, std140) uniform Material { vec4 baseColor; } material;
layout(set = 0, binding = 3) uniform sampler2D albedoTexture;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
void main()
{
    outColor = texture(albedoTexture, uv) * material.baseColor;
}
