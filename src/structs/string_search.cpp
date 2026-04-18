//
// Created by Nicholas on 18/04/26.
//

#include "string_search.h"

#include <string>

#include "arraylist.h"

ArrayList<char> string_search::to_list(const std::string& s)
{
    ArrayList<char> res(s.length());
    for (char c : s)
    {
        res.append(c);
    }
    return res;
}

int string_search::find_first(const ArrayList<char>& str, const std::string& pattern, int start, int finish)
{
    if (pattern.empty()) {
        return start;
    }

    int patternLength = pattern.length();
    int limit = finish - patternLength;
    for (int i = start; i <= limit; i++) {
        int j = 0;
        while (j < patternLength && str.at(i + j) == pattern.at(j)) {
            j++;
        }
        if (j == patternLength) {
            return i;
        }
    }
    return -1;
}

int string_search::find_last(const ArrayList<char>& str, const std::string& pattern, int start, int finish)
{
    if (pattern.empty()) {
        return finish;
    }

    int patternLength = pattern.length();
    int limit = finish - patternLength;
    for (int i = limit; i >= start; i--) {
        int j = 0;
        while (j < patternLength && str.at(i + j) == pattern.at(j)) {
            j++;
        }
        if (j == patternLength) {
            return i;
        }
    }
    return -1;
}

ArrayList<int> string_search::z_search(const ArrayList<char>& str, const std::string& pattern, int start, int finish)
{
    ArrayList<int> matches;
    if (pattern.empty()) {
        for (int i = start; i <= finish; ++i) {
            matches.append(i);
        }
        return matches;
    }

    const int patternLength = static_cast<int>(pattern.length());
    const int limit = finish - patternLength;
    for (int i = start; i <= limit; ++i) {
        int j = 0;
        while (j < patternLength && str.at(i + j) == pattern.at(j)) {
            ++j;
        }
        if (j == patternLength) {
            matches.append(i);
        }
    }

    return matches;
}

ArrayList<int> string_search::z_algo(const ArrayList<char>& txt)
{
    int length = static_cast<int>(txt.length());
    ArrayList<int> arr(length);
    for (int i = 0; i < length; ++i) {
        arr.append(0);
    }

    if (length == 0) {
        return arr;
    }

    int left = 0;
    int right = 0;
    for (int i = 1; i < length; i++) {
        if (i <= right) {
            arr[i] = std::min(right - i + 1, arr[i - left]);
        }

        while (i + arr[i] < length && txt.at(arr[i]) == txt.at(i + arr[i])) {
            arr[i]++;
        }

        if (i + arr[i] - 1 > right) {
            left = i;
            right = i + arr[i] - 1;
        }
    }

    return arr;
}
