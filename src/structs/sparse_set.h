//
// Created by Nicholas on 02/05/26.
//

#pragma once
#include "arraylist.h"

#include <format>

#include "page_set.h"

template <typename T>
class SparseSet
{
    const size_t pageSize = DEFAULT_PAGE_SZ;
    PaginatedSet<size_t> pages;
    Array<T> values;
    size_t nextInsertionIndex = 0;
    size_t _find_next_suitable_insertion();

public:

    SparseSet() = default;
    explicit SparseSet(const size_t pgsz) : pageSize(pgsz) {}

    size_t length() const
    { return values.size; }

    T& operator[](const size_t index) { return this->at(index); };
    bool contains(size_t index);
    T& at(const size_t index)
    { return values[pages[index]]; }

    size_t append(size_t index, T&& elem);
    T pop(size_t index);

    void clear();

    template<typename... Args>
    T& emplace(const size_t index, Args&&... args)
    {
        size_t insertionIndex = _find_next_suitable_insertion();
        pages.emplace(insertionIndex, index);
        values.appendGhost();
        // construct in place
        (values + insertionIndex).~T();
        new (values + insertionIndex) T(std::forward<Args>(args)...);
        return values[insertionIndex];
    }
};


template <typename T>
size_t SparseSet<T>::_find_next_suitable_insertion()
{
    // prevents memory fragmentation and having to iterate through pages to modify entries
    const size_t ret = nextInsertionIndex++;
    if (nextInsertionIndex < values.length())
    {
        // loops while the index is not -1 to find the next vacant spot that was previously deleted
        while (!pages.valid(nextInsertionIndex++)) {}
    }
    return ret;
}

template <typename T>
bool SparseSet<T>::contains(const size_t index)
{ return pages.valid(index); }

template <typename T>
size_t SparseSet<T>::append(size_t index, T&& elem)
{
    size_t insertionIndex = _find_next_suitable_insertion();
    pages.emplace(insertionIndex, index);

    values.appendGhost();
    values[insertionIndex] = std::move(elem);

    return insertionIndex;
}

template <typename T>
T SparseSet<T>::pop(const size_t index)
{
    const size_t pageIndex = pages.pop(index);

    T ret = values[pageIndex]; // copy
    values[pageIndex] = std::move(T{}); // reassign

    return ret;
}

template <typename T>
void SparseSet<T>::clear()
{
    values.clear();
    pages.clear();
}
