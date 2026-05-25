//
// Created by Nicholas on 11/05/26.
//

#pragma once
#include <cstdint>
#include <climits>

#include "../structs/arraylist.h"


class PackedFlags
{
    using PACKED_TYPE = uint32_t;

    constexpr static size_t S_SIZE = sizeof(PACKED_TYPE) * CHAR_BIT;

    Array<PACKED_TYPE, true> m_packed;

public:

    PackedFlags() : PackedFlags(2) {}
    explicit PackedFlags(const size_t init_size) : m_packed(init_size) {}

    PackedFlags operator&(const PackedFlags& other) const;
    PackedFlags operator|(const PackedFlags& other) const;

    bool operator[](const size_t key) const
    { return this->at(key); }

    size_t length() const
    { return m_packed.length() * S_SIZE; }

    size_t capacity() const
    { return m_packed.capacity() * S_SIZE; }

    bool empty() const { return length() == 0; }

    void guarantee(const size_t size)
    { m_packed.guarantee(size / S_SIZE); }

    bool at(size_t index) const;
    PACKED_TYPE& at_packed(size_t index);
    void set(size_t index, bool value);

    bool isTrue() const;
    bool isFalse() const
    { return !isTrue(); }

};
