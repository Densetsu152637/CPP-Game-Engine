#pragma once

#include <cstddef>

#include "structs/sparse_bit_field.h"

class TagPool
{
    size_t m_generation = 0;

public:
    SparseBitField bits;

    size_t generation() const
    { return m_generation; }

    size_t size() const
    { return bits.size(); }

    bool empty() const
    { return bits.empty(); }

    bool contains(const size_t entityIndex) const
    { return bits.contains(entityIndex); }

    bool add(const size_t entityIndex)
    { return set(entityIndex, true); }

    bool remove(const size_t entityIndex)
    { return set(entityIndex, false); }

    bool set(const size_t entityIndex, const bool value)
    {
        if (bits.contains(entityIndex) == value)
            return false;

        bits.set(entityIndex, value);
        ++m_generation;
        return true;
    }

    void clear()
    {
        if (bits.empty())
            return;

        bits.clear();
        ++m_generation;
    }

    ArrayList<size_t> entity_indices() const
    { return bits.trueIndexes(); }
};


