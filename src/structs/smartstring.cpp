//
// Created by Nicholas on 17/04/26.
//

#include "smartstring.h"

#include <cctype>
#include <stdexcept>

size_t SmartString::_scaled_index(size_t t) const
{
    size_t scaled = t + m_sI;
    if (scaled > m_fI)
    {
        throw std::out_of_range("SmartString index out of range");
    }
    return scaled;
};

bool SmartString::operator==(const SmartString& other) const
{
    if (length() != other.length()) return false; // if lengths are not equal
    for (size_t i = 0; i < length(); i++)
    {
        if (at(i) != other.at(i)) return false; // if chars do not equal each other
    }
    return true;
}

void SmartString::operator+=(const SmartString& other)
{
    this->append(other);
}

void SmartString::operator+=(const std::string& other)
{
    this->append(other);
}

SmartString SmartString::operator+(const SmartString& other) const
{
    SmartString ret(this->length() + other.length());
    ret += *this;
    ret += other;
    return ret;
}

char SmartString::operator[](const size_t i)
{
    return m_str[_scaled_index(i)];
}

SmartString& SmartString::append(const char other)
{
    m_str.append(other);
    return *this;
}

SmartString& SmartString::append(const char* other, const size_t length)
{
    for (int i = 0; i < length; i++)
    {
        m_str.append(other[i]);
    }
    return *this;
}

SmartString& SmartString::append(const ArrayList<char>& arr)
{
    m_str.append(arr);
    return *this;
}

SmartString& SmartString::append(const std::string& str)
{
    m_str.append(string_search::to_list(str));
    return *this;
}

SmartString& SmartString::append(const SmartString& str)
{
    m_str.append(str.m_str);
    return *this;
}

SmartString SmartString::concat(const SmartString& other)
{
    SmartString ret(length() + other.length());
    return ret
        .append(*this)
        .append(other);
}

ArrayList<SmartString> SmartString::split_on(std::string pattern) const
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
    return find(pattern) != static_cast<size_t>(-1);
}

bool SmartString::rcontains(const std::string& pattern)
{
    return rfind(pattern) != static_cast<size_t>(-1);
}

bool SmartString::starts_with(const std::string& prefix) const
{
    if (length() < prefix.length()) return false;
    for (int i = 0; i < prefix.length(); i++) {
        if (at(i) != prefix.at(i))
            return false;
    }
    return true;
}

bool SmartString::ends_with(const std::string& suffix) const
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

size_t SmartString::find(const std::string& pattern) const
{
    return find(pattern, 0);
}

size_t SmartString::find(const std::string& pattern, size_t start) const
{
    int hit = string_search::find_first(m_str, pattern, static_cast<int>(_scaled_index(start)), m_fI);
    return hit == -1 ? static_cast<size_t>(-1) : static_cast<size_t>(hit - m_sI);
}

size_t SmartString::rfind(const std::string& pattern) const
{
    return rfind(pattern, length());
}

size_t SmartString::rfind(const std::string& pattern, size_t end) const
{
    int hit = string_search::find_last(m_str, pattern, m_sI, static_cast<int>(_scaled_index(end)));
    return hit == -1 ? static_cast<size_t>(-1) : static_cast<size_t>(hit - m_sI);
}

int SmartString::count(const std::string& pattern) const
{
    return string_search::z_search(m_str, pattern, m_sI, m_fI).length();
}

SmartString SmartString::lstrip()
{
    int start = m_sI;
    while (start < m_fI && std::isspace(static_cast<unsigned char>(m_str.at(start)))) {
        start++;
    }
    return SmartString(m_str, start, m_fI);
}

SmartString SmartString::rstrip()
{
    int finish = m_fI;
    while (finish > m_sI && std::isspace(static_cast<unsigned char>(m_str.at(finish - 1)))) {
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
    ArrayList<char> replaced(length());
    for (int i = m_sI; i < m_fI; ++i)
    {
        const char c = m_str.at(i);
        replaced.append(c == old_value ? new_value : c);
    }

    return SmartString(std::move(replaced));
}


SmartString SmartString::replace(const std::string& old_pattern, const std::string& new_pattern)
{
    if (old_pattern.empty()) {
        return *this;
    }

    ArrayList<int> raw_hits = string_search::z_search(m_str, old_pattern, m_sI, m_fI);
    ArrayList<int> hits(raw_hits.length());
    int next_allowed = m_sI;
    for (const int hit : raw_hits) {
        if (hit < next_allowed) {
            continue;
        }
        hits.append(hit);
        next_allowed = hit + old_pattern.length();
    }

    const int old_length = static_cast<int>(old_pattern.length());
    const int new_length_delta = static_cast<int>(new_pattern.length()) - old_length;
    const int source_length = m_fI - m_sI;
    const int replaced_length = source_length + (new_length_delta * static_cast<int>(hits.length()));

    ArrayList<char> replaced_chars(replaced_length);
    for (int i = 0; i < replaced_length; i++) {
        replaced_chars.append('\0');
    }

    ArrayList<char> new_chars = string_search::to_list(new_pattern);
    Array<char> src = m_str.ptr();
    Array<char> dst = replaced_chars.ptr();
    const Array<char>& replacement = new_chars.ptr();

    int src_index = m_sI;
    int dst_index = 0;
    for (const int hit : hits) {
        int copy_length = hit - src_index;
        if (copy_length > 0) {
            array_cpy(dst, dst_index, src, src_index, copy_length);
            dst_index += copy_length;
        }

        if (replacement.size > 0) {
            array_cpy(dst, dst_index, replacement, 0, static_cast<int>(replacement.size));
            dst_index += static_cast<int>(replacement.size);
        }

        src_index = hit + old_length;
    }

    int remaining_length = m_fI - src_index;
    if (remaining_length > 0) {
        array_cpy(dst, dst_index, src, src_index, remaining_length);
    }

    return SmartString(std::move(replaced_chars));
}


SmartString SmartString::repeat(int count)
{
    if (count <= 0 || is_empty()) {
        return SmartString(std::string());
    }

    ArrayList<char> repeated(length() * count);
    for (int i = 0; i < count; i++) {
        for (int j = m_sI; j < m_fI; ++j)
        {
            repeated.append(m_str.at(j));
        }
    }
    return SmartString(std::move(repeated));
}

ArrayList<SmartString> SmartString::partition(const std::string& separator)
{
    size_t hit = find(separator);
    if (hit == static_cast<size_t>(-1)) {
        return ArrayList<SmartString>(3)
            .append(*this)
            .append(SmartString(std::string()))
            .append(SmartString(std::string()));
    }

    int absoluteHit = static_cast<int>(_scaled_index(hit));
    return ArrayList<SmartString>(3)
        .append(SmartString(m_str, m_sI, absoluteHit))
        .append(SmartString(m_str, absoluteHit, absoluteHit + static_cast<int>(separator.length())))
        .append(SmartString(m_str, absoluteHit + static_cast<int>(separator.length()), m_fI));
}

ArrayList<SmartString> SmartString::rpartition(const std::string& separator)
{
    size_t hit = rfind(separator);
    if (hit == static_cast<size_t>(-1)) {
        return ArrayList<SmartString>(3)
            .append(*this)
            .append(SmartString(std::string()))
            .append(SmartString(std::string()));
    }

    int absoluteHit = static_cast<int>(_scaled_index(hit));
    return ArrayList<SmartString>(3)
        .append(SmartString(m_str, m_sI, absoluteHit))
        .append(SmartString(m_str, absoluteHit, absoluteHit + static_cast<int>(separator.length())))
        .append(SmartString(m_str, absoluteHit + static_cast<int>(separator.length()), m_fI));
}

std::string SmartString::to_string()
{
    return std::string(m_str.begin() + m_sI, m_str.begin() + m_fI);
}

