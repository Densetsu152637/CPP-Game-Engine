//
// Created by Nicholas on 05/05/26.
//

#pragma once
#include "arraylist.h"

constexpr size_t DEFAULT_PAGE_SZ = 128;

template <typename T>
struct Page
{
    T* ptr = nullptr;
    bool* valid = nullptr;
    size_t size = 0;
    size_t elems = 0;
    bool initialised = false;

    Page() : size(DEFAULT_PAGE_SZ) {}
    Page(const Page& p)
    {
        unpaginate();
        size = p.size;

        if (!p.initialised) return;

        repaginate();
        elems = p.elems;

        for (size_t i = 0; i < p.size; i++)
        {
            ptr[i] = p.ptr[i];
        }
    }

    Page(Page&& p) noexcept
    {
        delete[] ptr;
        delete[] valid;
        initialised = p.initialised;
        size = p.size;
        elems = p.elems;
        ptr = p.ptr;
        valid = p.valid;

        p.ptr = nullptr;
        p.valid = nullptr;
        p.initialised = false;
        p.elems = 0;
    }

    Page& operator=(const Page& p)
    {
        unpaginate();
        size = p.size;

        if (!p.initialised) return *this;

        repaginate();
        elems = p.elems;

        for (size_t i = 0; i < p.size; i++)
        {
            ptr[i] = p.ptr[i];
            valid[i] = p.valid[i];
        }
        return *this;
    }

    Page& operator=(Page&& p) noexcept
    {
        delete[] ptr;
        delete [] valid;
        initialised = p.initialised;
        size = p.size;
        elems = p.elems;
        ptr = p.ptr;
        valid = p.valid;

        p.ptr = nullptr;
        p.valid = nullptr;
        p.initialised = false;
        p.elems = 0;
        return *this;
    }

    ~Page() { unpaginate(); }

    void repaginate()
    {
        delete[] ptr;
        ptr = new T[size];
        valid = new bool[size];
        initialised = true;
        elems = 0;
    }

    void unpaginate()
    {
        elems = 0;
        initialised = false;

        delete[] ptr;
        delete[] valid;


        ptr = nullptr;
        valid = nullptr;
    }

    T& operator[](const size_t index) { return ptr[index]; };

    using iterator = T*;
    using const_iterator = const T*;

    iterator begin()
    { return ptr; }

    iterator end()
    { return ptr + size; }

    const_iterator begin() const
    { return ptr; }

    const_iterator end() const
    { return ptr + size; }

    const_iterator cbegin() const
    { return ptr; }

    const_iterator cend() const
    { return ptr + size; }

};

template <typename T>
class PaginatedSet
{

    ArrayList<Page<T>> pages;
    const size_t pageSize = DEFAULT_PAGE_SZ;

    Page<T>& _guarantee_page(size_t index);
    Page<T>& _ensure_initialised(size_t index);

    static void validate(Page<T>& page, size_t pageIndex);
    static void invalidate(Page<T>& page, size_t pageIndex);

public:

    PaginatedSet() = default;
    explicit PaginatedSet(const size_t ps) : pageSize(ps) {}

    T& operator[](const size_t index) { return at(index); };
    const T& operator[](const size_t index) const { return at(index); };

    bool valid(size_t index);
    bool paginated(size_t index);
    bool contains(const T& target);
    T& at(size_t index);

    void append(size_t index, T&& elem);
    T pop(size_t index);
    void clear();

    template<typename... Args>
    T& emplace(const size_t index, Args&&... args)
    {
        const size_t searchIndex = index / pageSize;
        const Page<T>& page = _guarantee_page(searchIndex);
        const size_t pageIndex = index % pageSize;

        // construct in place
        page[pageIndex].~T();
        new (&page[pageIndex]) T(std::forward<Args>(args)...);

        return page[pageIndex];
    }

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
    // initialize page here if not already initialized
    if (!page.initialised)
    {
        page.size = this->pageSize;
        page.repaginate();
    }

    return page;
}

template <typename T>
bool PaginatedSet<T>::valid(const size_t index)
{
    const Page<T>& page = _guarantee_page(index / pageSize);
    if (!page.initialised) return false;
    const size_t pageIndex = index % pageSize;
    return page.valid[pageIndex];
}

template <typename T>
bool PaginatedSet<T>::paginated(const size_t index)
{
    const Page<T>& page = _guarantee_page(index / pageSize);
    return page.initialised;
}

template <typename T>
bool PaginatedSet<T>::contains(const T& target)
{
    for (const Page<T>& page: pages)
    {
        if (!page.initialised) continue;
        for (size_t i = 0; i < page.size; i++)
        {
            if (!page.valid[i]) continue;
            T& elem = page.ptr[i];
            if (elem == target)
            {
                return true;
            }
        }
    }
    return false;
}

template <typename T>
T& PaginatedSet<T>::at(const size_t index)
{
    const Page<T>& page = pages[index / pageSize]; // throws error if not existing
    if (!page.initialised)
        throw std::out_of_range(std::format("PaginatedSet page not initialised: {}", index));

    const size_t pageIndex = index % pageSize;
    if (!page.valid[pageIndex])
        throw std::out_of_range(std::format("PaginatedSet element not initialised: {}", index));

    return page.ptr[pageIndex];
}

template <typename T>
void PaginatedSet<T>::append(const size_t index, T&& elem)
{
    const Page<T>& page = _ensure_initialised(index / pageSize);
    const size_t pageIndex = index % pageSize;
    page[pageIndex] = std::move(elem);
    validate(page, pageIndex);
}

template <typename T>
void PaginatedSet<T>::validate(Page<T>& page, const size_t pageIndex)
{
    if (!page.valid[pageIndex])
        ++page.elems;

    page.valid[pageIndex] = true;
}

template <typename T>
T PaginatedSet<T>::pop(const size_t index)
{
    const Page<T>& page = pages[index / pageSize]; // throws error if out of bounds
    size_t pageIndex = index % pageSize;

    if (!page.initialised || !page.valid[pageIndex])
        throw std::out_of_range(std::format("PaginatedSet does not have value at: {}", index));

    T ret = page[pageIndex]; // copy
    pages[pageIndex] = std::move(T{}); // reassign

    invalidate(page, pageIndex);

    return ret;
}

template <typename T>
void PaginatedSet<T>::invalidate(Page<T>& page, const size_t pageIndex)
{
    if (page.valid[pageIndex])
        --page.elems;
    page.valid[pageIndex] = false;

    // delete page here if no elements are left (freeing up space)
    if (page.elems == 0)
        page.unpaginate();
}

template <typename T>
void PaginatedSet<T>::clear()
{
    pages.clear();
}
