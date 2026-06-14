//
// Created by Nicholas on 05/05/26.
//

#pragma once

#include <cstddef>
#include <format>
#include <stdexcept>
#include <utility>

#include "arraylist.h"

constexpr size_t DEFAULT_PAGE_SZ = 128;

template <typename T>
struct Page
{
    T* ptr = nullptr;
    bool* valid = nullptr;
    size_t size = DEFAULT_PAGE_SZ;
    size_t elems = 0;

    Page() = default;

    Page(const Page& p)
    {
        size = p.size;
        if (!p.initialised())
            return;

        repaginate();
        elems = p.elems;
        for (size_t i = 0; i < p.size; ++i)
        {
            ptr[i] = p.ptr[i];
            valid[i] = p.valid[i];
        }
    }

    Page(Page&& p) noexcept
    {
        ptr = p.ptr;
        valid = p.valid;
        size = p.size;
        elems = p.elems;

        p.ptr = nullptr;
        p.valid = nullptr;
        p.size = DEFAULT_PAGE_SZ;
        p.elems = 0;
    }

    Page& operator=(const Page& p)
    {
        if (this == &p)
        {
            return *this;
        }

        unpaginate();
        size = p.size;
        if (!p.initialised())
        {
            return *this;
        }

        repaginate();
        elems = p.elems;
        for (size_t i = 0; i < p.size; ++i)
        {
            ptr[i] = p.ptr[i];
            valid[i] = p.valid[i];
        }
        return *this;
    }

    Page& operator=(Page&& p) noexcept
    {
        if (this == &p)
        {
            return *this;
        }

        unpaginate();
        ptr = p.ptr;
        valid = p.valid;
        size = p.size;
        elems = p.elems;

        p.ptr = nullptr;
        p.valid = nullptr;
        p.size = DEFAULT_PAGE_SZ;
        p.elems = 0;
        return *this;
    }

    ~Page()
    { unpaginate(); }

    void repaginate()
    {
        delete[] ptr;
        delete[] valid;
        ptr = new T[size] {};
        valid = new bool[size] {};
        elems = 0;
    }

    void unpaginate()
    {
        delete[] ptr;
        delete[] valid;
        ptr = nullptr;
        valid = nullptr;
        elems = 0;
    }

    T& operator[](const size_t index) { return ptr[index]; }
    const T& operator[](const size_t index) const { return ptr[index]; }

    bool initialised() const { return nullptr != ptr && nullptr != valid; }

    using iterator = T*;
    using const_iterator = const T*;

    iterator begin() { return ptr; }
    iterator end() { return ptr + size; }
    const_iterator begin() const { return ptr; }
    const_iterator end() const { return ptr + size; }
    const_iterator cbegin() const { return ptr; }
    const_iterator cend() const { return ptr + size; }
};

template <typename T>
struct PageStorage
{
    ArrayList<Page<T>> pages;
    size_t pageSize = DEFAULT_PAGE_SZ;

    PageStorage() = default;
    explicit PageStorage(const size_t ps) : pageSize(ps) {}
    PageStorage(const size_t ps, const size_t initialCapacity)
        : pages(initialCapacity),
          pageSize(ps)
    {}

    size_t page_count() const
    { return pages.length(); }

    bool empty() const
    { return pages.empty(); }

    bool has_page(const size_t pageIndex) const
    {
        if (pageIndex >= pages.length())
            return false;

        return pages[pageIndex].initialised();
    }

    Page<T>* try_page(const size_t pageIndex)
    {
        if (pageIndex >= pages.length())
            return nullptr;

        Page<T>& page = pages[pageIndex];
        return page.initialised() ? &page : nullptr;
    }

    const Page<T>* try_page(const size_t pageIndex) const
    {
        if (pageIndex >= pages.length())
            return nullptr;

        const Page<T>& page = pages[pageIndex];
        return page.initialised() ? &page : nullptr;
    }

    Page<T>& guarantee_page(const size_t pageIndex)
    {
        while (pageIndex >= pages.length())
            pages.appendGhost();

        return pages[pageIndex];
    }

    Page<T>& ensure_page(const size_t pageIndex)
    {
        Page<T>& page = guarantee_page(pageIndex);
        if (!page.initialised())
        {
            page.size = pageSize;
            page.repaginate();
        }

        return page;
    }

    void destroy_page(const size_t pageIndex)
    {
        if (pageIndex < pages.length())
            pages[pageIndex].unpaginate();
    }

    void clear()
    { pages.clear(); }
};

template <typename T>
class PaginatedSet
{
    PageStorage<T> m_pages;
    size_t m_elems = 0;

    Page<T>& _guarantee_page(size_t index);
    Page<T>& _ensure_initialised(size_t index);

    void validate(Page<T>& page, size_t localIndex);
    void invalidate(Page<T>& page, size_t localIndex);

public:
    PaginatedSet() = default;
    explicit PaginatedSet(const size_t ps) : m_pages(ps) {}

    PaginatedSet(const PaginatedSet& set) noexcept = default;
    PaginatedSet(PaginatedSet&& set) noexcept;
    PaginatedSet& operator=(const PaginatedSet& set) noexcept = default;
    PaginatedSet& operator=(PaginatedSet&& set) noexcept;

    size_t size() const { return m_elems; }
    bool empty() const { return 0 == m_elems; }

    T& operator[](const size_t index) { return at(index); }
    const T& operator[](const size_t index) const { return at(index); }

    bool contains(size_t index) const;
    T* try_get(size_t index);
    const T* try_get(size_t index) const;
    T& at(size_t index);
    const T& at(size_t index) const;

    void set(size_t index, const T& elem);
    void set(size_t index, T&& elem);
    template <typename... Args>
    T& emplace(size_t index, Args&&... args);
    void erase(size_t index);
    void clear();
};

template <typename T>
PaginatedSet<T>::PaginatedSet(PaginatedSet&& set) noexcept
{
    this->m_pages = std::move(set.m_pages);
    this->m_elems = set.m_elems;

    set.m_elems = 0;
}

template <typename T>
PaginatedSet<T>& PaginatedSet<T>::operator=(PaginatedSet&& set) noexcept
{
    if (this == &set)
    {
        return *this;
    }

    this->m_pages = std::move(set.m_pages);
    this->m_elems = set.m_elems;

    set.m_elems = 0;
    return *this;
}

template <typename T>
Page<T>& PaginatedSet<T>::_guarantee_page(const size_t index)
{
    return m_pages.guarantee_page(index);
}

template <typename T>
Page<T>& PaginatedSet<T>::_ensure_initialised(const size_t index)
{
    return m_pages.ensure_page(index);
}

template <typename T>
bool PaginatedSet<T>::contains(const size_t index) const
{
    const size_t pageIndex = index / m_pages.pageSize;
    const Page<T>* page = m_pages.try_page(pageIndex);
    if (nullptr == page)
        return false;

    return page->valid[index % m_pages.pageSize];
}

template <typename T>
T* PaginatedSet<T>::try_get(const size_t index)
{
    const size_t pageIndex = index / m_pages.pageSize;
    Page<T>* page = m_pages.try_page(pageIndex);
    if (nullptr == page)
        return nullptr;

    const size_t localIndex = index % m_pages.pageSize;
    if (!page->valid[localIndex])
        return nullptr;

    return &(*page)[localIndex];
}

template <typename T>
const T* PaginatedSet<T>::try_get(const size_t index) const
{
    const size_t pageIndex = index / m_pages.pageSize;
    const Page<T>* page = m_pages.try_page(pageIndex);
    if (nullptr == page)
        return nullptr;

    const size_t localIndex = index % m_pages.pageSize;
    if (!page->valid[localIndex])
        return nullptr;

    return &(*page)[localIndex];
}

template <typename T>
T& PaginatedSet<T>::at(const size_t index)
{
    T* value = try_get(index);
    if (nullptr == value)
        throw std::out_of_range(std::format("PaginatedSet does not have value at: {}", index));

    return *value;
}

template <typename T>
const T& PaginatedSet<T>::at(const size_t index) const
{
    const T* value = try_get(index);
    if (nullptr == value)
        throw std::out_of_range(std::format("PaginatedSet does not have value at: {}", index));

    return *value;
}

template <typename T>
void PaginatedSet<T>::set(const size_t index, const T& elem)
{
    this->set(index, T(elem));
}

template <typename T>
void PaginatedSet<T>::set(const size_t index, T&& elem)
{
    Page<T>& page = _ensure_initialised(index / m_pages.pageSize);
    const size_t localIndex = index % m_pages.pageSize;

    this->validate(page, localIndex);
    page[localIndex] = std::move(elem);
}

template <typename T>
template <typename... Args>
T& PaginatedSet<T>::emplace(const size_t index, Args&&... args)
{
    this->set(index, T(std::forward<Args>(args)...));
    return at(index);
}

template <typename T>
void PaginatedSet<T>::erase(const size_t index)
{
    const size_t pageIndex = index / m_pages.pageSize;
    Page<T>* page = m_pages.try_page(pageIndex);
    if (nullptr == page)
        return;

    const size_t localIndex = index % m_pages.pageSize;
    if (!page->valid[localIndex])
        return;

    this->invalidate(*page, localIndex);
}

template <typename T>
void PaginatedSet<T>::invalidate(Page<T>& page, const size_t localIndex)
{
    if (!page.valid[localIndex])
        return;

    page.valid[localIndex] = false;
    --page.elems;
    --m_elems;

    if (page.elems == 0)
        page.unpaginate();
}

template <typename T>
void PaginatedSet<T>::validate(Page<T>& page, const size_t localIndex)
{
    if (page.valid[localIndex])
        return;

    page.valid[localIndex] = true;
    ++page.elems;
    ++m_elems;
}

template <typename T>
void PaginatedSet<T>::clear()
{
    m_pages.clear();
    m_elems = 0;
}
