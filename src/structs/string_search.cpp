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

int string_search::find_first(ArrayList<char>& str, const std::string& pattern, int start, int finish)
{
    int patternLength = pattern.length();
    int limit = finish - patternLength;
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

int string_search::find_last(ArrayList<char>& str, const std::string& pattern, int start, int finish)
{
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

ArrayList<int> string_search::z_search(ArrayList<char>& str, const std::string& pattern, int start, int finish)
{
    int textLength = finish - start;
    ArrayList<char> combined = to_list(pattern)
        .append('\0')
        .concat(str);

    ArrayList<int> arr = z_algo(combined);
    ArrayList<int> matches;
    int matchStart = pattern.length() + 1;
    int matchEnd = pattern.length() + 1 + textLength;
    for (int i = matchStart; i < matchEnd; i++) {
        if (arr[i] == pattern.length()) {
            matches.append(start + (i - matchStart));
        }
    }

    return matches;
}

ArrayList<int> string_search::z_algo(const ArrayList<char>& txt)
{
    int length = txt.length();
    ArrayList<int> arr(length);
    arr[0] = 0; // explicit declaration

    int left = 0;
    int right = 0;
    for (int i = 1; i < length; i++) {
        if (i <= right) {
            arr[i] = std::min(right - i + 1, arr[i - left]);
        }

        while (i + arr[i] < length && txt[arr[i]] == txt[i + arr[i]]) {
            arr[i]++;
        }

        if (i + arr[i] - 1 > right) {
            left = i;
            right = i + arr[i] - 1;
        }
    }

    return arr;
}
