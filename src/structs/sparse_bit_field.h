//
// Created by Nicholas on 14/06/26.
//

#pragma once

#include <climits>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "page_set.h"


class SparseBitField
{
public:
    using PACKED_TYPE = uint32_t;

    static constexpr size_t PACKED_SIZE = sizeof(PACKED_TYPE) * CHAR_BIT;
    static constexpr size_t PAGE_LENGTH = 32;
    static constexpr size_t BITS_PER_PAGE = PACKED_SIZE * PAGE_LENGTH;

private:
    PageStorage<PACKED_TYPE> m_pages;

    static constexpr PACKED_TYPE mask_for(size_t bitIndex)
    { return PACKED_TYPE {1} << (bitIndex % PACKED_SIZE); }

    static size_t count_page_bits(const Page<PACKED_TYPE>& page);
    static bool page_has_bits(const Page<PACKED_TYPE>& page);
    static void refresh_page_count(Page<PACKED_TYPE>& page);

    void release_page_if_empty(size_t pageIndex);

public:
    SparseBitField() : SparseBitField(16) {}
    explicit SparseBitField(size_t initialPageCapacity) : m_pages(PAGE_LENGTH, initialPageCapacity) {}

    SparseBitField operator&(const SparseBitField& other) const;
    SparseBitField operator|(const SparseBitField& other) const;

    bool operator[](const size_t key) const
    { return this->at(key); }

    size_t size() const;
    bool empty() const
    { return 0 == size(); }

    bool contains(const size_t index) const
    { return at(index); }

    size_t page_count() const
    { return m_pages.page_count(); }

    bool at(size_t index) const;
    PACKED_TYPE* try_get_packed(size_t dense_index);
    const PACKED_TYPE* try_get_packed(size_t dense_index) const;
    template <bool GUARANTEE = false> PACKED_TYPE& at_packed(size_t dense_index);
    const PACKED_TYPE& at_packed(size_t dense_index) const;
    void set(size_t index, bool value);
    void erase(size_t index)
    { set(index, false); }
    void setPacked(size_t dense_index, PACKED_TYPE value);
    void clear();
    void release_empty_pages();

    ArrayList<size_t> trueIndexes() const;

};

template <bool GUARANTEE>
SparseBitField::PACKED_TYPE& SparseBitField::at_packed(const size_t dense_index)
{
    const size_t pageIndex = dense_index / PAGE_LENGTH;
    Page<PACKED_TYPE>* page = nullptr;

    if constexpr (GUARANTEE)
    {
        page = &m_pages.ensure_page(pageIndex);
    } else
    {
        page = m_pages.try_page(pageIndex);
        if (nullptr == page)
            throw std::out_of_range("SparseBitField packed index not found");
    }

    return (*page)[dense_index % PAGE_LENGTH];
}
