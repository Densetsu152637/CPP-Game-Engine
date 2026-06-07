//
// Created by Nicholas on 17/04/26.
//

#include "smartstring.h"

#include <memory>
#include <stdexcept>

#include "../util/string_search.h"

int safe_size_t_to_int(const size_t& source) {
    if (source > static_cast<size_t>(INT_MAX)) {
        throw std::runtime_error("Overflow error: size_t value exceeds INT_MAX.");
    }
    return static_cast<int>(source);
}

size_t SmartString::_scaled_index(const size_t t) const
{
    const size_t scaled = t + m_sI;
    if (scaled > m_fI)
    {
        throw std::out_of_range("SmartString index out of range");
    }
    return scaled;
}

bool SmartString::_assert_modification()
{
    bool modified = false;

    if (
        m_shared->modifier != this ||  // if the smart string has been modified before
        m_shared->str.length() != m_fI // if the smart string only contains a slice of the original
    ) {
        const auto shared_copy = std::make_shared<SharedString>(length());
        for (size_t i = m_sI; i < m_fI; i++)
        {
            shared_copy->str.append(m_shared->str[i]);
        }
        m_shared = shared_copy;

        // resetting values to default to allow for more optimal string transformation
        m_sI = 0;
        m_fI = m_shared->str.length();

        modified = true;
    }

    m_shared->modifier = this;
    m_shared->modified = true;

    return modified;
}

SmartString::SmartString(const size_t size)
{
    // creates new array
    m_shared = std::make_shared<SharedString>(size);
}

SmartString::SmartString(const std::string& s)
{
    // creates new array
    m_shared = std::make_shared<SharedString>(s.length());
    for (int i = 0; i < s.length(); i++)
    {
        m_shared->str.append(s.at(i));
    }
    m_sI = 0;
    m_fI = s.length();
}

SmartString::SmartString(ArrayList<char>&& str)
{
    m_shared = std::make_shared<SharedString>();
    m_shared->str = std::move(str);
    m_sI = 0;
    m_fI = m_shared->str.length();
}

SmartString::SmartString(const SmartString& other) noexcept
{
    m_shared = other.m_shared;
    m_sI = other.m_sI;
    m_fI = other.m_fI;
}

SmartString::SmartString(SmartString&& other) noexcept
{
    m_shared = other.m_shared;
    m_sI = other.m_sI;
    m_fI = other.m_fI;

    if (m_shared && m_shared->modifier == &other)
    {
        m_shared->modifier = this;
    }

    // Leave other in a valid, empty state
    other.m_shared = nullptr;
    other.m_sI = 0;
    other.m_fI = 0;
}

SmartString& SmartString::operator=(const SmartString& other) noexcept
{
    m_shared = other.m_shared;
    m_sI = other.m_sI;
    m_fI = other.m_fI;
    return *this;
}

SmartString& SmartString::operator=(SmartString&& other) noexcept
{
    m_shared = other.m_shared;
    m_sI = other.m_sI;
    m_fI = other.m_fI;

    if (m_shared && m_shared->modifier == &other)
    {
        m_shared->modifier = this;
    }

    // Leave other in a valid, empty state
    other.m_shared = nullptr;
    other.m_sI = 0;
    other.m_fI = 0;

    return *this;
}

bool SmartString::operator==(const SmartString& other) const
{
    if (length() != other.length()) return false; // if lengths are not equal
    for (size_t i = 0; i < length(); i++)
    {
        if (at(i) != other.at(i)) return false; // if chars do not equal each other
    }
    return true;
}

SmartString SmartString::operator+(const SmartString& other) const
{
    return this->concat(other);
}

SmartString SmartString::operator+(const std::string& other) const
{
    return this->concat(SmartString(other));
}

SmartString& SmartString::operator+=(const SmartString& other)
{
    return this->append(other);
}

SmartString& SmartString::operator+=(const std::string& other)
{
    return this->append(SmartString(other));
}

char SmartString::operator[](const size_t i) const
{
    return m_shared->str[_scaled_index(i)];
}

SmartString& SmartString::append(const char c)
{
    this->_assert_modification();

    m_shared->str.append(c);
    m_fI++;

    return *this;
}

SmartString& SmartString::append(const char* other, const size_t length)
{
    this->_assert_modification();

    m_shared->str.append(other, length);
    m_fI += length;
    return *this;
}

SmartString& SmartString::append(const ArrayList<char>& str)
{
    this->_assert_modification();

    m_shared->str.append(str);
    m_fI += str.length();
    return *this;
}

SmartString& SmartString::append(const std::string& str)
{
    return this->append(str.c_str(), str.length());
}

SmartString& SmartString::append(const SmartString& str)
{
    return this->append(str.m_shared->str);
}

SmartString SmartString::concat(const SmartString& other) const
{
    SmartString ret(length() + other.length());
    ret
        .append(*this)
        .append(other);

    return ret;
}

ArrayList<SmartString> SmartString::split_on(const std::string& pattern) const
{
    ArrayList<size_t> hits = string_search::z_search(m_shared->str.cbegin(), pattern, m_sI, m_fI);
    ArrayList<SmartString> splits(hits.length() + 1);
    size_t start = m_sI;
    size_t nextAllowed = m_sI;

    for (const size_t hit : hits)
    {
        if (hit < nextAllowed)
            continue;

        splits.emplace(m_shared, start, hit);
        start = hit + pattern.length();
        nextAllowed = start;
    }

    splits.emplace(m_shared, start, m_fI);
    return splits;
}

ArrayList<SmartString> SmartString::split_on(const char token) const
{
    // do linear search for token in string
    ArrayList<size_t> hits;
    for (size_t i = m_sI; i < m_fI; ++i)
    {
        if (m_shared->str.at(i) == token)
        {
            hits.append(i);
        }
    }

    //
    ArrayList<SmartString> splits(hits.length() + 1);
    size_t start = m_sI;
    size_t nextAllowed = m_sI;

    for (const size_t hit : hits)
    {
        if (hit < nextAllowed)
            continue;

        splits.emplace(m_shared, start, hit);
        start = hit + 1;
        nextAllowed = start;
    }

    splits.emplace(m_shared, start, m_fI);
    return splits;
}

bool SmartString::contains(const std::string& pattern) const
{
    return find(pattern) != static_cast<size_t>(-1);
}

bool SmartString::rcontains(const std::string& pattern) const
{
    return rfind(pattern) != static_cast<size_t>(-1);
}

bool SmartString::starts_with(const std::string& prefix) const
{
    if (length() < prefix.length()) return false;
    for (int i = 0; i < prefix.length(); i++)
    {
        if (at(i) != prefix.at(i))
            return false;
    }
    return true;
}

bool SmartString::ends_with(const std::string& suffix) const
{
    if (length() < suffix.length()) return false;
    for (int i = 0; i < suffix.length(); i++)
    {

        const size_t tI = length() - i - 1;
        const size_t oI = suffix.length() - i - 1;

        if (at(tI) != suffix.at(oI))
            return false;
    }
    return true;
}

int SmartString::find(const std::string& pattern) const
{
    return find(pattern, 0);
}

int SmartString::find(const std::string& pattern, const size_t start) const
{
    const int hit = string_search::find_first(
        m_shared->str.cbegin(),
        pattern,
        _scaled_index(start), m_fI);

    return hit == -1 ? -1 : safe_size_t_to_int(hit - m_sI);
}

int SmartString::rfind(const std::string& pattern) const
{
    return rfind(pattern, length());
}

int SmartString::rfind(const std::string& pattern, const size_t end) const
{
    const int hit = string_search::find_last(
        m_shared->str.cbegin(),
        pattern,
        m_sI, _scaled_index(end));

    return hit == -1 ? -1 : safe_size_t_to_int(hit - m_sI);
}

size_t SmartString::count(const std::string& pattern) const
{
    return string_search::z_search(
        m_shared->str.cbegin(),
        pattern,
        m_sI, m_fI).length();
}

SmartString SmartString::lstrip() const
{
    size_t start = m_sI;
    while (start < m_fI && ' ' != at(start))
    {
        ++start;
    }
    return { m_shared, start, m_fI };
}

SmartString SmartString::rstrip() const
{
    size_t finish = m_fI;
    while (finish > m_sI && ' ' != at(finish))
    {
        --finish;
    }
    return { m_shared, m_sI, finish };
}

SmartString SmartString::remove_prefix(const std::string& prefix)
{
    return starts_with(prefix) ?
        SmartString(m_shared, m_sI + prefix.length(), m_fI) :
        *this;
}

SmartString SmartString::remove_suffix(const std::string& suffix)
{
    return ends_with(suffix) ?
        SmartString(m_shared, m_sI, m_fI - suffix.length()) :
        *this;
}

SmartString SmartString::replace(const char old_value, const char new_value) const
{
    ArrayList<char> replaced(length());
    for (size_t i = m_sI; i < m_fI; ++i)
    {
        const char c = m_shared->str.at(i);
        replaced.append(c == old_value ? new_value : c);
    }

    return { std::move(replaced) };
}


SmartString SmartString::replace(const std::string& old_pattern, const std::string& new_pattern)
{
    if (old_pattern.empty()) {
        return *this;
    }

    ArrayList<size_t> raw_hits = string_search::z_search(m_shared->str.cbegin(), old_pattern, m_sI, m_fI);
    ArrayList<size_t> hits(raw_hits.length());
    size_t next_allowed = m_sI;
    for (const size_t hit : raw_hits) {
        if (hit < next_allowed) {
            continue;
        }
        hits.append(hit);
        next_allowed = hit + old_pattern.length();
    }

    const int old_length = static_cast<int>(old_pattern.length());
    const int new_length_delta = static_cast<int>(new_pattern.length()) - old_length;
    const size_t source_length = m_fI - m_sI;
    const size_t replaced_length = source_length + (new_length_delta * static_cast<int>(hits.length()));

    ArrayList<char> replaced_chars{ replaced_length };
    for (int i = 0; i < replaced_length; i++) {
        replaced_chars.append('\0');
    }

    ArrayList<char> new_chars = string_search::to_list(new_pattern);
    const Array<char>& src = m_shared->str.ptr();
    Array<char>& dst = replaced_chars.ptr();
    const Array<char>& replacement = new_chars.ptr();

    size_t src_index = m_sI;
    size_t dst_index = 0;
    for (const size_t hit : hits)
    {
        if (
            const size_t copy_length = hit - src_index;
            copy_length > 0
        ) {
            array_cpy(dst, dst_index, src, src_index, copy_length);
            dst_index += copy_length;
        }

        if (replacement.size > 0)
        {
            array_cpy(dst, dst_index, replacement, 0, static_cast<int>(replacement.size));
            dst_index += static_cast<int>(replacement.size);
        }

        src_index = hit + old_length;
    }

    if (
        const size_t remaining_length = m_fI - src_index;
        remaining_length > 0
    ) {
        array_cpy(dst, dst_index, src, src_index, remaining_length);
    }

    return { std::move(replaced_chars) };
}


SmartString SmartString::repeat(const int count) const
{
    if (count <= 0 || is_empty())
    {
        return { "" };
    }

    ArrayList<char> repeated(length() * count);
    for (int i = 0; i < count; ++i) {
        for (size_t j = m_sI; j < m_fI; ++j)
        {
            repeated.append(m_shared->str.at(j));
        }
    }

    return { std::move(repeated) };
}

ArrayList<SmartString> SmartString::partition(const std::string& separator)
{
    const int hit = find(separator);
    ArrayList<SmartString> ret;

    if (hit == -1)
    {
        ret.emplace(m_shared, m_sI, m_fI);
        ret.emplace("");
        ret.emplace("");
    } else
    {
        const size_t absoluteHit = _scaled_index(hit);
        ret.emplace(m_shared, m_sI, absoluteHit);
        ret.emplace(m_shared, absoluteHit, absoluteHit + separator.length());
        ret.emplace(m_shared, absoluteHit + separator.length(), m_fI);
    }

    return ret;
}

ArrayList<SmartString> SmartString::rpartition(const std::string& separator)
{
    int hit = rfind(separator);
    ArrayList<SmartString> ret;

    if (hit == -1) {
        ret.emplace(m_shared, m_sI, m_fI);
        ret.emplace("");
        ret.emplace("");
    } else
    {
        const size_t absoluteHit = _scaled_index(hit);
        ret.emplace(m_shared, m_sI, absoluteHit);
        ret.emplace(m_shared, absoluteHit, absoluteHit + separator.length());
        ret.emplace(m_shared, absoluteHit + separator.length(), m_fI);
    }

    return ret;
}

std::string SmartString::to_string() const
{
    return std::string(m_shared->str.cbegin() + m_sI, m_shared->str.cbegin() + m_fI);
}

