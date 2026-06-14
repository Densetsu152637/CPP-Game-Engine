//
// Per-shader uniform declarations and dirty-value tracking.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../structs/arraylist.h"

namespace rendering
{
    enum class UniformKind
    {
        Bytes,
        Float,
        Vec2,
        Vec3,
        Vec4,
        Mat4,
        Int,
        UInt,
        Bool
    };

    struct UniformBinding
    {
        uint32_t set = 0;
        uint32_t binding = 0;
    };

    struct UniformValue
    {
        std::string name;
        UniformBinding binding;
        UniformKind kind = UniformKind::Bytes;
        std::vector<std::byte> bytes;
        bool dirty = true;
        uint64_t valueGeneration = 0;
        uint64_t lastTouchedFrame = 0;
        uint64_t lastUploadedFrame = 0;

        size_t size() const
        { return bytes.size(); }
    };

    struct UniformUpload
    {
        const UniformValue* uniform = nullptr;
        const std::byte* data = nullptr;
        size_t size = 0;
    };

    class UniformRegistry
    {
        ArrayList<UniformValue> m_uniforms;
        std::unordered_map<std::string, size_t> m_indices;
        uint64_t m_generation = 0;

        UniformValue& uniform_at(const std::string& name);
        const UniformValue& uniform_at(const std::string& name) const;

    public:
        UniformRegistry() = default;

        size_t size() const
        { return m_uniforms.length(); }

        bool empty() const
        { return m_uniforms.empty(); }

        uint64_t generation() const
        { return m_generation; }

        bool contains(const std::string& name) const
        { return m_indices.contains(name); }

        const ArrayList<UniformValue>& uniforms() const
        { return m_uniforms; }

        UniformValue& declare(
            std::string name,
            UniformBinding binding,
            size_t byteSize,
            UniformKind kind = UniformKind::Bytes
        );

        template <typename T>
        UniformValue& declare(
            std::string name,
            const UniformBinding binding,
            const UniformKind kind = UniformKind::Bytes
        ) {
            static_assert(std::is_trivially_copyable_v<T>, "Uniform values must be trivially copyable");
            return declare(std::move(name), binding, sizeof(T), kind);
        }

        template <typename T>
        bool set(const std::string& name, const T& value, const uint64_t frameIndex = 0)
        {
            static_assert(std::is_trivially_copyable_v<T>, "Uniform values must be trivially copyable");

            UniformValue& uniform = uniform_at(name);
            if (uniform.bytes.size() != sizeof(T))
                throw std::invalid_argument("Uniform write size does not match declaration");

            const auto* raw = reinterpret_cast<const std::byte*>(&value);
            const bool changed = 0 != std::memcmp(uniform.bytes.data(), raw, sizeof(T));
            uniform.lastTouchedFrame = frameIndex;
            if (!changed)
                return false;

            std::memcpy(uniform.bytes.data(), raw, sizeof(T));
            uniform.dirty = true;
            ++uniform.valueGeneration;
            ++m_generation;
            return true;
        }

        bool setBytes(
            const std::string& name,
            const void* data,
            size_t byteSize,
            uint64_t frameIndex = 0
        );

        template <typename T>
        const T* try_get(const std::string& name) const
        {
            static_assert(std::is_trivially_copyable_v<T>, "Uniform values must be trivially copyable");

            const auto it = m_indices.find(name);
            if (it == m_indices.end())
                return nullptr;

            const UniformValue& uniform = m_uniforms[it->second];
            if (uniform.bytes.size() != sizeof(T))
                return nullptr;

            return reinterpret_cast<const T*>(uniform.bytes.data());
        }

        bool dirty(const std::string& name) const;
        bool anyDirty() const;
        ArrayList<UniformUpload> dirtyUploads() const;
        void markUploaded(const std::string& name, uint64_t frameIndex);
        void markAllUploaded(uint64_t frameIndex);
        void markDirty(const std::string& name);
        void clear();
    };
}
