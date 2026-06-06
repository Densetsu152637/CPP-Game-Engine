//
// Created by Nicholas on 02/05/26.
//

#pragma once

#include <cstddef>
#include <stdexcept>
#include <utility>

#include "arraylist.h"
#include "page_set.h"

template <typename T>
class RenderComponentPool;

template <typename T>
class SparseSet
{
    friend class RenderComponentPool<T>;

    PaginatedSet<size_t> m_sparse;
    ArrayList<size_t> m_denseKeys;
    ArrayList<T> m_dense;

public:
    static constexpr size_t N_POS = static_cast<size_t>(-1);

    SparseSet() = default;

    size_t size() const
    { return m_dense.length(); }

    bool empty() const
    { return m_dense.empty(); }

    size_t index_of(const size_t key) const
    {
        const size_t* denseIndex = m_sparse.try_get(key);
        if (nullptr == denseIndex || *denseIndex >= m_denseKeys.length())
            return N_POS;

        return key == m_denseKeys[*denseIndex] ? *denseIndex : N_POS;
    }

    bool contains(const size_t key) const
    { return N_POS != index_of(key); }

    T* try_get_dense(const size_t denseIndex)
    {
        if (denseIndex >= m_dense.length())
            return nullptr;

        return &m_dense[denseIndex];
    }

    T* try_get(const size_t key)
    {
        const size_t denseIndex = index_of(key);
        if (N_POS == denseIndex)
        { return nullptr; }

        return &m_dense[denseIndex];
    }

    const T* try_get(const size_t key) const
    {
        const size_t denseIndex = index_of(key);
        if (N_POS == denseIndex)
        { return nullptr; }

        return &m_dense[denseIndex];
    }

    T& at(const size_t key)
    {
        T* value = try_get(key);
        if (nullptr == value)
            throw std::out_of_range("SparseSet key not found");

        return *value;
    }

    const T& at(const size_t key) const
    {
        const T* value = try_get(key);
        if (nullptr == value)
            throw std::out_of_range("SparseSet key not found");

        return *value;
    }

    T& operator[](const size_t key)
    { return this->at(key); }

    const T& operator[](const size_t key) const
    { return this->at(key); }

    template <typename... Args>
    T& emplace(const size_t key, Args&&... args)
    {
        const size_t existingIndex = index_of(key);
        if (N_POS != existingIndex)
        {
            m_dense[existingIndex] = T(std::forward<Args>(args)...);
            return m_dense[existingIndex];
        }

        const size_t denseIndex = m_dense.length();
        m_dense.appendGhost();
        m_denseKeys.appendGhost();

        m_sparse.set(key, denseIndex);
        m_dense[denseIndex] = T(std::forward<Args>(args)...);
        m_denseKeys[denseIndex] = key;
        return m_dense[denseIndex];
    }

    T& insert_or_assign(const size_t key, T value)
    { return this->emplace(key, std::move(value)); }

    void erase(const size_t key)
    {
        const size_t denseIndex = index_of(key);
        if (N_POS == denseIndex)
            return;

        const size_t lastIndex = m_dense.length() - 1;

        if (denseIndex != lastIndex)
        {
            std::swap(m_dense[denseIndex], m_dense[lastIndex]);
            std::swap(m_denseKeys[denseIndex], m_denseKeys[lastIndex]);
            m_sparse.set(m_denseKeys[denseIndex], denseIndex);
        }

        m_dense.pop();
        m_denseKeys.pop();
        m_sparse.erase(key);
    }

    void clear()
    {
        m_dense.clear();
        m_denseKeys.clear();
        m_sparse.clear();
    }

    size_t key_at(const size_t denseIndex) const
    { return m_denseKeys[denseIndex]; }

    ArrayList<size_t>& dense_keys()
    { return m_denseKeys; }

    const ArrayList<size_t>& dense_keys() const
    { return m_denseKeys; }

    ArrayList<T>& dense_values()
    { return m_dense; }

    const ArrayList<T>& dense_values() const
    { return m_dense; }

    T& dense_at(const size_t denseIndex)
    { return m_dense[denseIndex]; }

    const T& dense_at(const size_t denseIndex) const
    { return m_dense[denseIndex]; }

    auto begin() { return m_dense.begin(); }
    auto end() { return m_dense.end(); }
    auto begin() const { return m_dense.begin(); }
    auto end() const { return m_dense.end(); }
};
