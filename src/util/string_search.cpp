//
// Created by Nicholas on 18/04/26.
//

#include <string>

#include "string_search.h"
#include "../structs/arraylist.h"

ArrayList<char> string_search::to_list(const std::string& s)
{
    ArrayList<char> res(s.length());
    for (char c : s)
    {
        res.append(c);
    }
    return res;
}

int string_search::find_first(const char* str, const std::string& pattern, const size_t start, const size_t finish)
{
    if (pattern.empty()) {
        return static_cast<int>(start);
    }

    const size_t patternLength = pattern.length();
    const size_t limit = finish - patternLength;

    for (size_t i = start; i <= limit; i++) {
        size_t j = 0;

        while (j < patternLength && str[i + j] == pattern.at(j)) {
            j++;
        }

        if (j == patternLength) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

int string_search::find_last(const char* str, const std::string& pattern, const size_t start, const size_t finish)
{
    if (pattern.empty()) {
        return static_cast<int>(finish);
    }

    const size_t patternLength = pattern.length();
    const size_t limit = finish - patternLength;

    for (size_t i = limit; i >= start; i--) {
        size_t j = 0;
        while (j < patternLength && str[i + j] == pattern.at(j)) {
            j++;
        }
        if (j == patternLength) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

ArrayList<size_t> string_search::z_search(const char* str, const std::string& pattern, const size_t start, const size_t finish)
{
    if (pattern.empty()) {
        ArrayList<size_t> no_matches(finish - start);
        for (size_t i = start; i <= finish; ++i) {
            no_matches.append(i);
        }
        return no_matches;
    }

    // combine the strings to form the z_str, which is:
    // pattern + '\0' + search_str
    const size_t length = finish - start;
    const size_t pattern_length = pattern.length();
    std::string combined;
    combined.reserve(length + pattern_length + 1);

    int i = 0;

    for (; i < pattern_length; i++)
    {
        combined.push_back(pattern.at(i));
    }
    combined.push_back('\0');
    for (; i < length + pattern_length; i++)
    {
        combined.push_back(str[start + (i - pattern_length)]);
    }

    // here i = combinedLength
    ArrayList<size_t> z_arr = z_algo(combined.data(), combined.size());
    ArrayList<size_t> matches;

    for (size_t j = pattern_length + 1; j < z_arr.length(); j++)
    {
        if (z_arr[j] == pattern_length)
            matches.append(j - pattern_length + 1);
    }

    return matches;
}

ArrayList<size_t> string_search::z_algo(const char* txt, const size_t length)
{
    ArrayList<size_t> arr(length);
    for (size_t i = 0; i < length; ++i) {
        arr.append(0);
    }

    if (length == 0) {
        return arr;
    }

    size_t left = 0;
    size_t right = 0;

    for (size_t i = 1; i < length; i++)
    {
        //
        if (i <= right)
            arr[i] = std::min(right - i + 1, arr[i - left]);

        //
        while (i + arr[i] < length && txt[arr[i]] == txt[i + arr[i]])
            arr[i]++;

        //
        if (i + arr[i] - 1 > right)
        {
            left = i;
            right = i + arr[i] - 1;
        }
    }

    return arr;
}
