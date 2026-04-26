//
// Created by Nicholas on 19/04/26.
//

#pragma once
#include <string>

#include "../structs/smartstring.h"
#include "../functional/result.h"

// file writing functions

SmartString readFileSmart(const std::string& fp);
std::string readFile(const std::string& fp);
ArrayList<SmartString> readLines(const std::string& fp);
void writeToFile(const SmartString& contents, const std::string& fp);



template<typename Callable, typename... Args>
auto safeExecute(Callable&& fn, Args&&... args)
    -> Result<std::invoke_result_t<Callable, Args...>>
{
    using ReturnT = std::invoke_result_t<Callable, Args...>;

    try
    {
        return Result<ReturnT>::success(
            std::invoke(std::forward<Callable>(fn), std::forward<Args>(args)...)
        );
    }
    catch (const std::exception& e)
    {
        return Result<ReturnT>::failure(e);
    }
}

template <typename T>
T clamp(T num, T min, T max)
{
    if (num < min) return min;
    if (num > max) return max;
    return num;
};

template <typename T>
T normalise(T val)
{
    return clamp(val, 0, 1.0);
}

template <typename T>
T& max(const ArrayList<T>& arr)
{
    T& max = arr[0];
    for (T& elem : arr)
    {
        if (elem > max) max = elem;
    }
    return max;
}

template <typename T>
T& min(const ArrayList<T>& arr)
{
    T& min = arr[0];
    for (T& elem : arr)
    {
        if (elem < min) min = elem;
    }
    return min;
}

template <typename T>
ArrayList<T> flatten(const ArrayList<ArrayList<T>>& arr)
{
    ArrayList<T> result;
    size_t total_space = 0;
    for (auto& sub_arr : arr)
    {
        total_space += sub_arr.length();
    }

    result.reserve(total_space);

    for (auto& sub_arr : arr)
        for (auto& elem : sub_arr)
            result.append(elem);

    return result;
}

