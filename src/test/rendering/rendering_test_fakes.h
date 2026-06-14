#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "rendering/renderer.h"
#include "structs/arraylist.h"

class TestShader final : public rendering::IShader
{
public:
    explicit TestShader(std::string name)
        : IShader(std::move(name))
    {}

    std::string_view backendName() const override
    { return "test"; }
};

class TestRenderer final : public IRenderer
{
protected:
    bool uploadUniformImpl(
        rendering::IShader& shader,
        const rendering::ShaderUniformUpload& upload
    ) override {
        writes.append(rendering::capture_uniform_write(upload, shader.name()));
        return true;
    }

    void renderImpl(rendering::IShader& shader) override
    {
        renderedShaders.append(shader.name());
        ++renderCalls;
    }

public:
    ArrayList<rendering::ShaderUniformWrite> writes;
    ArrayList<std::string> renderedShaders;
    size_t renderCalls = 0;

    void clear()
    {
        writes.clear();
        renderedShaders.clear();
        renderCalls = 0;
    }
};
