//
// Created by Nicholas on 14/06/26.
//

#include "sparse_bit_field.h"

#include <algorithm>
#include <bit>


size_t SparseBitField::count_page_bits(const Page<PACKED_TYPE>& page)
{
    if (!page.initialised())
        return 0;

    size_t count = 0;
    for (size_t i = 0; i < page.size; ++i)
        count += static_cast<size_t>(std::popcount(page[i]));

    return count;
}

bool SparseBitField::page_has_bits(const Page<PACKED_TYPE>& page)
{
    if (!page.initialised())
        return false;

    for (size_t i = 0; i < page.size; ++i)
    {
        if (0 != page[i])
            return true;
    }

    return false;
}

void SparseBitField::refresh_page_count(Page<PACKED_TYPE>& page)
{
    page.elems = count_page_bits(page);
}

void SparseBitField::release_page_if_empty(const size_t pageIndex)
{
    Page<PACKED_TYPE>* page = m_pages.try_page(pageIndex);
    if (nullptr == page)
        return;

    if (!page_has_bits(*page))
    {
        m_pages.destroy_page(pageIndex);
        return;
    }

    refresh_page_count(*page);
}

SparseBitField SparseBitField::operator&(const SparseBitField& other) const
{
    const size_t pageCount = std::min(m_pages.page_count(), other.m_pages.page_count());
    SparseBitField result(pageCount);

    for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex)
    {
        const Page<PACKED_TYPE>* leftPage = m_pages.try_page(pageIndex);
        const Page<PACKED_TYPE>* rightPage = other.m_pages.try_page(pageIndex);
        if (nullptr == leftPage || nullptr == rightPage)
            continue;

        for (size_t packedIndex = 0; packedIndex < PAGE_LENGTH; ++packedIndex)
        {
            const PACKED_TYPE packed = (*leftPage)[packedIndex] & (*rightPage)[packedIndex];
            if (0 != packed)
                result.setPacked(pageIndex * PAGE_LENGTH + packedIndex, packed);
        }
    }

    return result;
}

SparseBitField SparseBitField::operator|(const SparseBitField& other) const
{
    const size_t pageCount = std::max(m_pages.page_count(), other.m_pages.page_count());
    SparseBitField result(pageCount);

    for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex)
    {
        const Page<PACKED_TYPE>* leftPage = m_pages.try_page(pageIndex);
        const Page<PACKED_TYPE>* rightPage = other.m_pages.try_page(pageIndex);
        if (nullptr == leftPage && nullptr == rightPage)
            continue;

        for (size_t packedIndex = 0; packedIndex < PAGE_LENGTH; ++packedIndex)
        {
            const PACKED_TYPE left = nullptr == leftPage ? 0 : (*leftPage)[packedIndex];
            const PACKED_TYPE right = nullptr == rightPage ? 0 : (*rightPage)[packedIndex];
            const PACKED_TYPE packed = left | right;
            if (0 != packed)
                result.setPacked(pageIndex * PAGE_LENGTH + packedIndex, packed);
        }
    }

    return result;
}

size_t SparseBitField::size() const
{
    size_t count = 0;
    for (size_t pageIndex = 0; pageIndex < m_pages.page_count(); ++pageIndex)
    {
        const Page<PACKED_TYPE>* page = m_pages.try_page(pageIndex);
        if (nullptr != page)
            count += count_page_bits(*page);
    }

    return count;
}

bool SparseBitField::at(const size_t index) const
{
    const PACKED_TYPE* packed = try_get_packed(index / PACKED_SIZE);
    if (nullptr == packed)
        return false;

    return 0 != (*packed & mask_for(index));
}

SparseBitField::PACKED_TYPE* SparseBitField::try_get_packed(const size_t dense_index)
{
    Page<PACKED_TYPE>* page = m_pages.try_page(dense_index / PAGE_LENGTH);
    if (nullptr == page)
        return nullptr;

    return &(*page)[dense_index % PAGE_LENGTH];
}

const SparseBitField::PACKED_TYPE* SparseBitField::try_get_packed(const size_t dense_index) const
{
    const Page<PACKED_TYPE>* page = m_pages.try_page(dense_index / PAGE_LENGTH);
    if (nullptr == page)
        return nullptr;

    return &(*page)[dense_index % PAGE_LENGTH];
}

const SparseBitField::PACKED_TYPE& SparseBitField::at_packed(const size_t dense_index) const
{
    const PACKED_TYPE* packed = try_get_packed(dense_index);
    if (nullptr == packed)
        throw std::out_of_range("SparseBitField packed index not found");

    return *packed;
}

void SparseBitField::set(const size_t index, const bool value)
{
    const size_t denseIndex = index / PACKED_SIZE;
    const size_t pageIndex = denseIndex / PAGE_LENGTH;
    const size_t packedIndex = denseIndex % PAGE_LENGTH;
    const PACKED_TYPE mask = mask_for(index);

    if (value)
    {
        Page<PACKED_TYPE>& page = m_pages.ensure_page(pageIndex);
        PACKED_TYPE& packed = page[packedIndex];
        if (0 == (packed & mask))
        {
            packed |= mask;
            refresh_page_count(page);
        }
        return;
    }

    Page<PACKED_TYPE>* page = m_pages.try_page(pageIndex);
    if (nullptr == page)
        return;

    PACKED_TYPE& packed = (*page)[packedIndex];
    if (0 == (packed & mask))
        return;

    packed &= ~mask;
    release_page_if_empty(pageIndex);
}

void SparseBitField::setPacked(const size_t dense_index, const PACKED_TYPE value)
{
    const size_t pageIndex = dense_index / PAGE_LENGTH;
    const size_t packedIndex = dense_index % PAGE_LENGTH;

    if (0 == value)
    {
        Page<PACKED_TYPE>* page = m_pages.try_page(pageIndex);
        if (nullptr == page)
            return;

        (*page)[packedIndex] = 0;
        release_page_if_empty(pageIndex);
        return;
    }

    Page<PACKED_TYPE>& page = m_pages.ensure_page(pageIndex);
    page[packedIndex] = value;
    refresh_page_count(page);
}

void SparseBitField::clear()
{
    m_pages.clear();
}

void SparseBitField::release_empty_pages()
{
    for (size_t pageIndex = 0; pageIndex < m_pages.page_count(); ++pageIndex)
        release_page_if_empty(pageIndex);
}

ArrayList<size_t> SparseBitField::trueIndexes() const
{
    ArrayList<size_t> indexes(size());

    for (size_t pageIndex = 0; pageIndex < m_pages.page_count(); ++pageIndex)
    {
        const Page<PACKED_TYPE>* page = m_pages.try_page(pageIndex);
        if (nullptr == page)
            continue;

        for (size_t packedIndex = 0; packedIndex < PAGE_LENGTH; ++packedIndex)
        {
            PACKED_TYPE packed = (*page)[packedIndex];
            while (0 != packed)
            {
                const size_t bitIndex = static_cast<size_t>(std::countr_zero(packed));
                const size_t denseIndex = pageIndex * PAGE_LENGTH + packedIndex;
                indexes.append(denseIndex * PACKED_SIZE + bitIndex);
                packed &= packed - 1;
            }
        }
    }

    return indexes;
}
