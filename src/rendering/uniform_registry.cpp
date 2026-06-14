//
// Per-shader uniform declarations and dirty-value tracking.
//

#include "uniform_registry.h"

#include <algorithm>

namespace rendering
{
    UniformValue& UniformRegistry::uniform_at(const std::string& name)
    {
        const auto it = m_indices.find(name);
        if (it == m_indices.end())
            throw std::out_of_range("Uniform was not declared: " + name);

        return m_uniforms[it->second];
    }

    const UniformValue& UniformRegistry::uniform_at(const std::string& name) const
    {
        const auto it = m_indices.find(name);
        if (it == m_indices.end())
            throw std::out_of_range("Uniform was not declared: " + name);

        return m_uniforms[it->second];
    }

    UniformValue& UniformRegistry::declare(
        std::string name,
        const UniformBinding binding,
        const size_t byteSize,
        const UniformKind kind
    ) {
        if (name.empty())
            throw std::invalid_argument("Uniform name cannot be empty");

        if (0 == byteSize)
            throw std::invalid_argument("Uniform byte size cannot be zero");

        const auto existing = m_indices.find(name);
        if (existing != m_indices.end())
        {
            UniformValue& uniform = m_uniforms[existing->second];
            if (uniform.binding.set != binding.set
                || uniform.binding.binding != binding.binding
                || uniform.kind != kind
                || uniform.bytes.size() != byteSize)
            {
                throw std::invalid_argument("Uniform redeclared with incompatible layout: " + name);
            }

            return uniform;
        }

        const size_t index = m_uniforms.length();
        UniformValue& uniform = m_uniforms.emplace();
        uniform.name = std::move(name);
        uniform.binding = binding;
        uniform.kind = kind;
        uniform.bytes.resize(byteSize);
        uniform.dirty = true;
        m_indices.emplace(uniform.name, index);
        ++m_generation;
        return uniform;
    }

    bool UniformRegistry::setBytes(
        const std::string& name,
        const void* data,
        const size_t byteSize,
        const uint64_t frameIndex
    ) {
        if (nullptr == data)
            throw std::invalid_argument("Uniform byte write source cannot be null");

        UniformValue& uniform = uniform_at(name);
        if (uniform.bytes.size() != byteSize)
            throw std::invalid_argument("Uniform byte write size does not match declaration");

        const auto* raw = static_cast<const std::byte*>(data);
        const bool changed = !std::equal(
            uniform.bytes.begin(),
            uniform.bytes.end(),
            raw,
            raw + byteSize
        );
        uniform.lastTouchedFrame = frameIndex;
        if (!changed)
            return false;

        std::copy(raw, raw + byteSize, uniform.bytes.begin());
        uniform.dirty = true;
        ++uniform.valueGeneration;
        ++m_generation;
        return true;
    }

    bool UniformRegistry::dirty(const std::string& name) const
    {
        return uniform_at(name).dirty;
    }

    bool UniformRegistry::anyDirty() const
    {
        for (const UniformValue& uniform : m_uniforms)
        {
            if (uniform.dirty)
                return true;
        }

        return false;
    }

    ArrayList<UniformUpload> UniformRegistry::dirtyUploads() const
    {
        ArrayList<UniformUpload> uploads;
        uploads.reserve(m_uniforms.length());

        for (const UniformValue& uniform : m_uniforms)
        {
            if (!uniform.dirty)
                continue;

            uploads.append(UniformUpload {
                &uniform,
                uniform.bytes.data(),
                uniform.bytes.size()
            });
        }

        return uploads;
    }

    void UniformRegistry::markUploaded(const std::string& name, const uint64_t frameIndex)
    {
        UniformValue& uniform = uniform_at(name);
        if (!uniform.dirty && uniform.lastUploadedFrame == frameIndex)
            return;

        uniform.dirty = false;
        uniform.lastUploadedFrame = frameIndex;
        ++m_generation;
    }

    void UniformRegistry::markAllUploaded(const uint64_t frameIndex)
    {
        bool changed = false;
        for (UniformValue& uniform : m_uniforms)
        {
            if (!uniform.dirty && uniform.lastUploadedFrame == frameIndex)
                continue;

            uniform.dirty = false;
            uniform.lastUploadedFrame = frameIndex;
            changed = true;
        }

        if (changed)
            ++m_generation;
    }

    void UniformRegistry::markDirty(const std::string& name)
    {
        UniformValue& uniform = uniform_at(name);
        if (uniform.dirty)
            return;

        uniform.dirty = true;
        ++m_generation;
    }

    void UniformRegistry::clear()
    {
        if (m_uniforms.empty())
            return;

        m_uniforms.clear();
        m_indices.clear();
        ++m_generation;
    }
}
