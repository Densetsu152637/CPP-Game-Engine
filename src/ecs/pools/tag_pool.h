#pragma once

#include <cstddef>

#include "structs/sparse_bit_field.h"
#include "structs/sparse_set.h"

class TagPool
{
    size_t m_generation = 0;

public:
    SparseBitField bits;
    ArrayList<size_t> entities;
    SparseSet<size_t> denseIndices;

    size_t generation() const
    { return m_generation; }

    size_t size() const
    { return entities.length(); }

    bool empty() const
    { return entities.empty(); }

    bool contains(const size_t entityIndex) const
    { return bits.contains(entityIndex); }

    bool add(const size_t entityIndex)
    {
        if (contains(entityIndex))
            return false;

        const size_t denseIndex = entities.length();
        entities.append(entityIndex);
        denseIndices.emplace(entityIndex, denseIndex);
        bits.set(entityIndex, true);
        ++m_generation;
        return true;
    }

    bool remove(const size_t entityIndex)
    {
        if (!contains(entityIndex))
            return false;

        const size_t* denseIndexPtr = denseIndices.try_get(entityIndex);
        if (nullptr == denseIndexPtr)
            return false;

        const size_t denseIndex = *denseIndexPtr;
        const size_t lastDenseIndex = entities.length() - 1;
        const size_t lastEntityIndex = entities[lastDenseIndex];

        if (denseIndex != lastDenseIndex)
        {
            entities[denseIndex] = lastEntityIndex;
            denseIndices.insert_or_assign(lastEntityIndex, denseIndex);
        }

        entities.pop();
        denseIndices.erase(entityIndex);
        bits.set(entityIndex, false);
        ++m_generation;
        return true;
    }

    bool set(const size_t entityIndex, const bool value)
    {
        return value ? add(entityIndex) : remove(entityIndex);
    }

    void clear()
    {
        if (empty())
            return;

        bits.clear();
        entities.clear();
        denseIndices.clear();
        ++m_generation;
    }

    const ArrayList<size_t>& entity_indices() const
    { return entities; }
};


