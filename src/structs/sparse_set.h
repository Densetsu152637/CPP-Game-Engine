//
// Created by Nicholas on 02/05/26.
//

#pragma once
#include "arraylist.h"

#include <format>

constexpr int DEFAULT_PAGE_SZ = 128;

struct intlike {
    int value = -1;

    // Default constructor → -1
    intlike() = default;

    // Constructor from int
    intlike(const int v) : value(v) {}
    intlike(const size_t v) : value(static_cast<int>(v)) {}

    // Optional: implicit conversion to int
    explicit operator int() const { return value; }
    explicit operator size_t() const { return value; }
    bool operator==(const int i) const { return value == i; };
    bool operator==(const size_t i) const { return value == i; };
};

template <typename T>
struct Page
{
    T* ptr = nullptr;
    size_t size = 0;
    size_t elems = 0;
    bool initialised = false;

    Page() : size(DEFAULT_PAGE_SZ) {}
    ~Page() { unpaginate(); }

    void repaginate()
    {
        delete[] ptr;
        ptr = new T[size];
        initialised = true;
        elems = 0;
    }

    void unpaginate()
    {
        if (elems != 0) return;
        delete[] ptr;
        initialised = false;
    }

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
class SparseSet
{

    const size_t pageSize = DEFAULT_PAGE_SZ;
    ArrayList<Page<intlike>> pages;
    ArrayList<T> values;
    size_t nextInsertionIndex = 0;

    Page<intlike>& _guarantee_page(size_t index);
    size_t _find_next_suitable_insertion();

public:

    SparseSet() = default;
    SparseSet(const size_t pgsz) : pageSize(pgsz) {}

    size_t length() const
    { return values.length(); }

    T& operator[](const size_t index) { return this->at(index); };
    bool contains(size_t index);
    T& at(size_t index);
    T& directQuery(size_t index);

    size_t append(size_t index, T&& elem);
    T pop(size_t index);

    void clear();

    template<typename... Args>
    T& emplace(const size_t index, Args&&... args)
    {
        size_t insertionIndex = _find_next_suitable_insertion();
        const Page<intlike>& page = _guarantee_page(index);
        const intlike pageReverseIndex = index % pageSize;

        page.ptr[pageReverseIndex.value] = index;
        values.appendGhost();
        // construct in place
        values[insertionIndex].~T();
        new (&values[insertionIndex]) T(std::forward<Args>(args)...);

        return values[insertionIndex];
    }

};


template <typename T>
Page<intlike>& SparseSet<T>::_guarantee_page(const size_t index)
{
    const size_t pageIndex = index / pageSize;
    Page<intlike>& page = pages[pageIndex];
    // initialise page here if not already initialised
    if (!page.initialised)
    {
        page.size = this->pageSize;
        page.repaginate();
    }

    return page;
}

template <typename T>
size_t SparseSet<T>::_find_next_suitable_insertion()
{
    // prevents memory fragmentation and having to iterate through pages to modify entries
    const size_t ret = nextInsertionIndex++;
    if (nextInsertionIndex < values.length())
    {
        // loops while the index is not -1 to find the next vacant spot that was previously deleted
        while (values[nextInsertionIndex++].index != -1) {}
    }
    return ret;
}

template <typename T>
bool SparseSet<T>::contains(const size_t index)
{
    const Page<intlike>& page = pages[index / pageSize];
    if (!page.initialised) return false;
    const intlike pageIndex = page.ptr[index % pageSize];
    return pageIndex == -1 || values.length() > index;
}

template <typename T>
T& SparseSet<T>::at(const size_t index)
{
    const Page<intlike>& page = pages[index / pageSize];
    const intlike pageIndex = page.ptr[index % pageSize];
    return directQuery(pageIndex);
}

template <typename T>
T& SparseSet<T>::directQuery(size_t index)
{ return values[index].value; }

template <typename T>
size_t SparseSet<T>::append(const size_t index, T&& elem)
{
    size_t insertionIndex = _find_next_suitable_insertion();
    const Page<intlike>& page = _guarantee_page(index);
    const intlike pageReverseIndex = index % pageSize;

    page.ptr[pageReverseIndex.value] = index;
    values.appendGhost();
    values[insertionIndex] = std::move(elem);

    return insertionIndex;
}

template <typename T>
T SparseSet<T>::pop(const size_t index)
{
    Page<intlike>& page = pages[index / pageSize];
    intlike& pageIndex = page.ptr[index % pageSize];
    if (pageIndex == -1)
        throw std::out_of_range(std::format("SparseSet does not have value: {}", index));

    T ret = values[pageIndex]; // copy
    values[pageIndex] = std::move(T{}); // reassign

    nextInsertionIndex = std::min(nextInsertionIndex, static_cast<size_t>(pageIndex));
    pageIndex = -1;

    // delete page here if no elements are left (freeing up space)
    if (--page.elems == 0)
        page.unpaginate();

    return ret;
}

template <typename T>
void SparseSet<T>::clear()
{
    values.clear();
    pages.clear();
}
