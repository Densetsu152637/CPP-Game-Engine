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
        }
    }

    Page(Page&& p) noexcept
    {
        ptr = p.ptr;
        size = p.size;
        elems = p.elems;

        p.ptr = nullptr;
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
        size = p.size;
        elems = p.elems;

        p.ptr = nullptr;
        p.size = DEFAULT_PAGE_SZ;
        p.elems = 0;
        return *this;
    }

    ~Page()
    { unpaginate(); }

    void repaginate()
    {
        delete[] ptr;
        ptr = new T[size] {};
        elems = 0;
    }

    void unpaginate()
    {
        delete[] ptr;
        ptr = nullptr;
        elems = 0;
    }

    T& operator[](const size_t index) { return ptr[index]; }
    const T& operator[](const size_t index) const { return ptr[index]; }

    bool initialised() const { return nullptr != ptr; }

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
class PaginatedSet
{
    ArrayList<Page<T>> pages;
    const size_t pageSize = DEFAULT_PAGE_SZ;
    size_t m_elems = 0;

    Page<T>& _guarantee_page(size_t index);
    Page<T>& _ensure_initialised(size_t index);

    void validate(Page<T>& page, size_t localIndex);
    void invalidate(Page<T>& page, size_t localIndex);

public:
    PaginatedSet() = default;
    explicit PaginatedSet(const size_t ps) : pageSize(ps) {}

    size_t size() const { return m_elems; }
    bool empty() const { return 0 == m_elems; }

    T& operator[](const size_t index) { return at(index); }
    const T& operator[](const size_t index) const { return at(index); }

    bool contains(size_t index) const;
    T* try_get(size_t index);
    const T* try_get(size_t index) const;
    T& at(size_t index);
    const T& at(size_t index) const;

    void set( size_t index, T elem);
    void set(size_t index, T&& elem);
    template <typename... Args>
    T& emplace(size_t index, Args&&... args);
    void erase(size_t index);
    void clear();
};

template <typename T>
Page<T>& PaginatedSet<T>::_guarantee_page(const size_t index)
{
    pages.guarantee(index);
    return pages[index];
}

template <typename T>
Page<T>& PaginatedSet<T>::_ensure_initialised(const size_t index)
{
    Page<T>& page = _guarantee_page(index);
    if (!page.initialised())
    {
        page.size = pageSize;
        page.repaginate();
    }

    return page;
}

template <typename T>
bool PaginatedSet<T>::contains(const size_t index) const
{
    if (index >= pages.length()) return false;
    Page<T>& page = pages[index / pageSize];
    return page.initialised();
}

template <typename T>
T* PaginatedSet<T>::try_get(const size_t index)
{
    if (!contains(index))
        return nullptr;

    return &pages[index / pageSize][index % pageSize];
}

template <typename T>
const T* PaginatedSet<T>::try_get(const size_t index) const
{
    if (!contains(index))
        return nullptr;

    return &pages[index / pageSize][index % pageSize];
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
void PaginatedSet<T>::set(const size_t index, T elem)
{
    return this->set(index, std::move(elem));
}

template <typename T>
void PaginatedSet<T>::set(const size_t index, T&& elem)
{
    Page<T>& page = _ensure_initialised(index / pageSize);
    const size_t locaIndex = index % pageSize;

    this->validate(page, locaIndex);
    page[locaIndex] = std::move(elem);
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
    const size_t pageIndex = index / pageSize;
    if (pageIndex >= pages.length())
        return;

    Page<T>& page = pages[pageIndex];
    if (!page.initialised())
        return;

    const size_t localIndex = index % pageSize;

    this->invalidate(page, localIndex);

    page.valid[localIndex] = false;
    --page.elems;
    --m_elems;

    if (page.elems == 0)
        page.unpaginate();

}

template <typename T>
void PaginatedSet<T>::invalidate(Page<T>& page, const size_t localIndex)
{
    --page.elems;
    --m_elems;

    if (page.elems == 0)
        page.unpaginate();
}

template <typename T>
void PaginatedSet<T>::validate(Page<T>& page, const size_t localIndex)
{
    ++page.elems;
    ++m_elems;
}

template <typename T>
void PaginatedSet<T>::clear()
{
    pages.clear();
    m_elems = 0;
}
