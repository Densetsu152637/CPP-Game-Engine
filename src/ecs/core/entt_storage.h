#pragma once

#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <entt/entity/storage.hpp>
#include <entt/entity/runtime_view.hpp>

namespace ecs
{
    using BackendEntity = uint64_t;
    using BackendSet = entt::basic_sparse_set<BackendEntity>;
    using BackendView = entt::basic_runtime_view<BackendSet>;

    // A compatibility facade over EnTT's paged storage. EnTT owns both the
    // component objects and the sparse/dense membership maps.
    template <typename T>
    class EnTTStorage
    {
        // Wrapping also gives empty engine components addressable storage.
        struct Value { T component; };
        entt::basic_storage<Value, BackendEntity> m_storage;

    public:
        static constexpr size_t N_POS = static_cast<size_t>(-1);
        size_t size() const { return m_storage.size(); }
        bool empty() const { return m_storage.empty(); }
        bool contains(size_t key) const { return m_storage.contains(key); }
        size_t index_of(size_t key) const { return contains(key) ? m_storage.index(key) : N_POS; }
        size_t key_at(size_t index) const { return static_cast<size_t>(m_storage.data()[index]); }
        T* try_get(size_t key) { return contains(key) ? &m_storage.get(key).component : nullptr; }
        const T* try_get(size_t key) const { return contains(key) ? &m_storage.get(key).component : nullptr; }
        T& at(size_t key)
        {
            auto* value = try_get(key);
            if (!value) throw std::out_of_range("EnTT component does not exist");
            return *value;
        }
        const T& at(size_t key) const
        {
            auto* value = try_get(key);
            if (!value) throw std::out_of_range("EnTT component does not exist");
            return *value;
        }
        T& dense_at(size_t index) { return at(key_at(index)); }
        const T& dense_at(size_t index) const { return at(key_at(index)); }
        template <typename... Args>
        T& emplace(size_t key, Args&&... args)
        {
            if (auto* value = try_get(key))
                return *value = T(std::forward<Args>(args)...);
            return m_storage.emplace(key, Value{T(std::forward<Args>(args)...)}).component;
        }
        T& insert_or_assign(size_t key, T value) { return emplace(key, std::move(value)); }
        void erase(size_t key) { m_storage.remove(key); }
        void clear() { m_storage.clear(); }
        BackendSet& backend() { return m_storage; }
        const BackendSet& backend() const { return m_storage; }

        template <bool Const>
        class DenseRange
        {
            using Owner = std::conditional_t<Const, const EnTTStorage, EnTTStorage>;
            Owner* m_owner;
        public:
            explicit DenseRange(Owner& owner) : m_owner(&owner) {}
            size_t size() const { return m_owner->size(); }
            size_t length() const { return size(); }
            bool empty() const { return m_owner->empty(); }
            decltype(auto) operator[](size_t index) const { return m_owner->dense_at(index); }
            struct Iterator
            {
                Owner* owner;
                size_t index;
                decltype(auto) operator*() const { return owner->dense_at(index); }
                Iterator& operator++() { ++index; return *this; }
                bool operator==(const Iterator&) const = default;
            };
            Iterator begin() const { return {m_owner, 0}; }
            Iterator end() const { return {m_owner, size()}; }
        };
        DenseRange<false> dense_values() { return DenseRange<false>(*this); }
        DenseRange<true> dense_values() const { return DenseRange<true>(*this); }
    };
}
