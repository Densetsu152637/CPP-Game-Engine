//
// Created by Nicholas on 11/05/26.
//

#include "packed_flags.h"

PackedFlags PackedFlags::operator&(const PackedFlags& other) const
{
    const size_t andLength = std::max(m_packed.length(), other.length());
    const size_t minLength = std::min(m_packed.length(), other.length());
    PackedFlags newFlags {andLength};

    for (size_t i = 0; i < minLength; i++)
        newFlags.m_packed[i] = m_packed[i] & other.m_packed[i];

    for (size_t i = minLength; i < andLength; i++)
        newFlags.m_packed[i] = 0;

    return newFlags;
}

PackedFlags PackedFlags::operator|(const PackedFlags& other) const
{
    const size_t andLength = std::max(m_packed.length(), other.length());
    const size_t minLength = std::min(m_packed.length(), other.length());
    PackedFlags newFlags {andLength};

    for (size_t i = 0; i < minLength; i++)
        newFlags.m_packed[i] = m_packed[i] | other.m_packed[i];

    if (andLength != minLength)
    {
        const PackedFlags& longer = this->length() > other.length() ? *this : other;

        for (size_t i = minLength; i < andLength; i++)
            newFlags.m_packed[i] = longer.m_packed[i];
    }

    return newFlags;
}

bool PackedFlags::at(const size_t index) const
{
    const PACKED_TYPE& packed = m_packed[index / S_SIZE];
    const PACKED_TYPE shifted = 1 << (index % S_SIZE);
    return (packed & shifted) > 0;
}

PackedFlags::PACKED_TYPE& PackedFlags::at_packed(const size_t index)
{ return m_packed[index]; }

void PackedFlags::set(const size_t index, const bool value)
{
    PACKED_TYPE& packed = m_packed[index / S_SIZE];
    const PACKED_TYPE shifted = 1 << (index % S_SIZE);
    if (value)  packed |= shifted;
    else        packed &= ~shifted;
}

bool PackedFlags::isTrue() const
{
    PACKED_TYPE acc = 0;
    for (size_t i = 0; i < length(); i++)
        acc |= m_packed[i];

    return acc > 0;
}

