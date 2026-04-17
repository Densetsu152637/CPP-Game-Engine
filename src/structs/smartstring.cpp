//
// Created by Nicholas on 17/04/26.
//

#include "smartstring.h"

#include <stdexcept>

size_t SmartString::_scaled_index(size_t t)
{
    size_t scaled = t + m_sI;
    if (scaled > m_fI)
    {
        throw std::out_of_range(std::format("Failed to fetch char at {} from {} (scaled from {})", scaled, m_fI, t));
    }
    return scaled;
};

bool SmartString::operator=(const SmartString& other)
{
    if (length() != other.length()) return false; // if lengths are not equal
    for (int i = 0; i < length(); i++)
    {
        if (at(i) != other.at(i)) return false; // if chars do not equal each other
    }
    return true;
}

SmartString SmartString::concat(const SmartString& other)
{
    return SmartString(m_str.concat(other.m_str), m_sI, m_fI + other.length());
}

ArrayList<SmartString> SmartString::split_on(std::string pattern)
{
    ArrayList<int> hits = string_search::z_search(m_str, pattern, m_sI, m_fI);
    ArrayList<SmartString> splits(hits.length() + 1);
    int start = m_sI;
    int patternLength = pattern.length();
    int nextAllowed = m_sI;
    for (const int hit : hits) {
        if (hit < nextAllowed) {
            continue;
        }
        splits.append(SmartString(m_str, start, hit));
        start = hit + patternLength;
        nextAllowed = start;
    }
    splits.append(SmartString(m_str, start, m_fI));
    return splits;
}

bool SmartString::contains(const std::string& pattern)
{
    return find(pattern) != -1;
}

bool SmartString::rcontains(const std::string& pattern)
{
    return rfind(pattern) != -1;
}

bool SmartString::starts_with(const std::string& prefix)
{
    if (length() < prefix.length()) return false;
    for (int i = 0; i < prefix.length(); i++) {
        if (at(i) != prefix.at(i))
            return false;
    }
    return true;
}

bool SmartString::ends_with(const std::string& suffix)
{
    if (length() < suffix.length()) return false;
    for (int i = 0; i < suffix.length(); i++) {
        int tI = length() - i - 1;
        int oI = suffix.length() - i - 1;
        if (at(tI) != suffix.at(oI))
            return false;
    }
    return true;
}

size_t SmartString::find(const std::string& pattern)
{
    return find(pattern, 0);
}

size_t SmartString::find(const std::string& pattern, size_t start)
{
    int hit = string_search::find_first(m_str, pattern, _scaled_index(start),m_fI);
    return hit == -1 ? -1 : hit - m_sI;
}

size_t SmartString::rfind(const std::string& pattern)
{
    return rfind(pattern, length());
}

size_t SmartString::rfind(const std::string& pattern, size_t end)
{
    int hit = string_search::find_first(m_str, pattern, m_sI, _scaled_index(end));
    return hit == -1 ? -1 : hit - m_sI;
}

int SmartString::count(const std::string& pattern)
{
    return string_search::z_search(m_str, pattern, m_sI, m_fI).length();
}

SmartString SmartString::lstrip()
{
    int start = m_sI;
    while (start < m_fI && ' ' != m_str[start]) {
        start++;
    }
    return SmartString(m_str, start, m_fI);
}

SmartString SmartString::rstrip()
{
    int finish = m_fI;
    while (finish > m_sI && ' ' != m_str[finish - 1]) {
        finish--;
    }
    return SmartString(m_str, m_sI, finish);
}

SmartString SmartString::remove_prefix(const std::string& prefix)
{
    return starts_with(prefix) ?
        SmartString(m_str, m_sI + prefix.length(), m_fI) :
        *this;
}

SmartString SmartString::remove_suffix(const std::string& suffix)
{
    return ends_with(suffix) ?
        SmartString(m_str, m_sI, m_fI - suffix.length()) :
        *this;
}

SmartString SmartString::replace(char old_value, char new_value)
{
    return SmartString(m_str.map([old_value, new_value](const char& c) -> char {
        return c == old_value ? new_value : c;
    }));
}


SmartString SmartString::replace(const std::string& old_pattern, const std::string& new_pattern)
{

}


SmartString SmartString::repeat(int count)
{
    if (count <= 0 || is_empty()) {
        return SmartString("");
    }

    ArrayList<char> repeated(length() * count);
    for (int i = 0; i < count; i++) {
        repeated.append(m_str);
    }
    return SmartString(repeated);
}

ArrayList<SmartString> SmartString::partition(const std::string& separator)
{
    int hit = find(separator);
    if (hit == -1) {
        return ArrayList<SmartString>(3)
            .append(*this)
            .append(SmartString(""))
            .append(SmartString(""));
    }

    int absoluteHit = _scaled_index(hit);
    return ArrayList<SmartString>(3)
        .append(SmartString(m_str, m_sI, absoluteHit))
        .append(SmartString(m_str, absoluteHit, absoluteHit + separator.length()))
        .append(SmartString(m_str, absoluteHit + separator.length(), m_fI));
    };
}

ArrayList<SmartString> SmartString::rpartition(const std::string& separator)
{
    int hit = rfind(separator);
    if (hit == -1) {
        return ArrayList<SmartString>(3)
            .append(*this)
            .append(SmartString(""))
            .append(SmartString(""));
    }

    int absoluteHit = _scaled_index(hit);
    return ArrayList<SmartString>(3)
        .append(SmartString(m_str, m_sI, absoluteHit))
        .append(SmartString(m_str, absoluteHit, absoluteHit + separator.length()))
        .append(SmartString(m_str, absoluteHit + separator.length(), m_fI));
}

std::string SmartString::to_string()
{
    return std::string(m_str.begin() + m_sI, m_str.begin() + m_fI);
}

