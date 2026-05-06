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
        return start;
    }

    const int patternLength = pattern.length();
    const int limit = finish - patternLength;

    for (int i = start; i <= limit; i++) {
        int j = 0;

        while (j < patternLength && str[i + j] == pattern.at(j)) {
            j++;
        }

        if (j == patternLength) {
            return i;
        }
    }

    return -1;
}

int string_search::find_last(const char* str, const std::string& pattern, const size_t start, const size_t finish)
{
    if (pattern.empty()) {
        return finish;
    }

    int patternLength = pattern.length();
    int limit = finish - patternLength;
    for (int i = limit; i >= start; i--) {
        int j = 0;
        while (j < patternLength && str[i + j] == pattern.at(j)) {
            j++;
        }
        if (j == patternLength) {
            return i;
        }
    }
    return -1;
}

ArrayList<int> string_search::z_search(const char* str, const std::string& pattern, const size_t start, const size_t finish)
{
    if (pattern.empty()) {
        ArrayList<int> no_matches(finish - start);
        for (int i = start; i <= finish; ++i) {
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
    ArrayList<int> z_arr = z_algo(combined.data(), combined.size());
    ArrayList<int> matches;

    for (int j = pattern_length + 1; j < z_arr.length(); j++)
    {
        if (z_arr[j] == pattern_length)
            matches.append(j - pattern_length + 1);
    }

    return matches;
}

ArrayList<int> string_search::z_algo(const char* txt, const size_t length)
{
    ArrayList<int> arr(length);
    for (int i = 0; i < length; ++i) {
        arr.append(0);
    }

    if (length == 0) {
        return arr;
    }

    int left = 0;
    int right = 0;

    for (int i = 1; i < length; i++)
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
