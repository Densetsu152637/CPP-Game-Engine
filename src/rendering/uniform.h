//
// Created by Nicholas on 26/04/26.
//

#pragma once
#include <algorithm>
#include <atomic>

#include "../structs/arraylist.h"


template <typename T>
class Uniform
{

    ArrayList<T>* m_ecs_arr;
    const size_t m_id;
    bool m_isClean = false;

public:

    explicit Uniform(ArrayList<T>* ecs_arr, const size_t id) : m_ecs_arr(ecs_arr), m_id(id) {}

    // copy / move
    Uniform(const Uniform& uni) noexcept : m_id(uni.m_id)
    {
        m_ecs_arr = uni.m_ecs_arr;
    }

    Uniform(Uniform&& uni) noexcept : m_id(uni.m_id)
    {
        m_ecs_arr = uni.m_ecs_arr;
        uni.m_ecs_arr = nullptr;
    }

    Uniform& operator=(const T& elem)
    {
        if (nullptr != m_ecs_arr)
        {
            (*m_ecs_arr)[m_id] = elem;
        };

        return *this;
    }

    Uniform& operator=(T&& elem)
    {
        if (nullptr != m_ecs_arr)
        {
            (*m_ecs_arr)[m_id] = std::move(elem);
        };

        return *this;
    }

    //

    uint32_t id() const { return this->m_id; }
    bool is_clean() const { return this->m_isClean; }
    void sprinkle() { this->m_isClean = true; }

};
