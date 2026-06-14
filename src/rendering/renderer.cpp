//
// Backend-neutral renderer interface used by ECS rendering jobs.
//

#include "renderer.h"

#include <stdexcept>

bool IRenderer::uploadUniformUnlocked(
    rendering::IShader& shader,
    const rendering::ShaderUniformUpload& upload
) {
    if (upload.uniformName.empty())
        throw std::invalid_argument("Shader uniform upload requires a uniform name");

    if (nullptr == upload.data)
        throw std::invalid_argument("Shader uniform upload data cannot be null");

    if (0 == upload.byteSize)
        throw std::invalid_argument("Shader uniform upload byte size cannot be zero");

    const bool needsUpload = shader.uploadUniformBytes(upload);
    if (!needsUpload)
        return false;

    return uploadUniformImpl(shader, upload);
}

bool IRenderer::uploadUniform(
    rendering::IShader& shader,
    const rendering::ShaderUniformUpload& upload
) {
    std::scoped_lock lock(m_renderMutex);
    return uploadUniformUnlocked(shader, upload);
}

bool IRenderer::uploadUniforms(
    rendering::IShader& shader,
    const ArrayList<rendering::ShaderUniformUpload>& uploads
) {
    std::scoped_lock lock(m_renderMutex);

    bool uploaded = false;
    for (const rendering::ShaderUniformUpload& upload : uploads)
        uploaded = uploadUniformUnlocked(shader, upload) || uploaded;

    return uploaded;
}

void IRenderer::render()
{
    throw std::logic_error("Renderer::render() requires a shader object");
}

void IRenderer::render(rendering::IShader& shader)
{
    std::scoped_lock lock(m_renderMutex);
    renderImpl(shader);
    shader.markUploaded();
}

bool IRenderer::render(
    rendering::IShader& shader,
    const ArrayList<rendering::ShaderUniformUpload>& uploads
) {
    std::scoped_lock lock(m_renderMutex);

    bool uploaded = false;
    for (const rendering::ShaderUniformUpload& upload : uploads)
        uploaded = uploadUniformUnlocked(shader, upload) || uploaded;

    renderImpl(shader);
    shader.markUploaded();
    return uploaded;
}
