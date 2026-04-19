//
// Created by Nicholas on 17/04/26.
//

#pragma once

#include <cstddef>
#include <string>

#include "arraylist.h"
#include "string_search.h"

class SmartString {

    ArrayList<char> m_str;
    int m_sI = 0, m_fI = -1;

    size_t _scaled_index(size_t t) const;

public:

    SmartString() : m_fI(0) {}
    SmartString(const std::string& s) : m_str(string_search::to_list(s)), m_fI(m_str.length()) {}
    SmartString(const std::string&& s) : m_str(string_search::to_list(s)), m_fI(m_str.length()) {}
    SmartString(const ArrayList<char>& ref_arr) : m_str(ref_arr), m_fI(ref_arr.length()) {}
    SmartString(const ArrayList<char>& ref_arr, const int sI, const int fI) : m_str(ref_arr), m_sI(sI), m_fI(fI) {}
    SmartString(ArrayList<char>&& move_arr) : m_str(std::move(move_arr)), m_fI(m_str.length()) {}
    SmartString(ArrayList<char>&& move_arr, const int sI, const int fI) : m_str(std::move(move_arr)), m_sI(sI), m_fI(fI) {}

    bool operator==(const SmartString& other) const;

    size_t length() const { return static_cast<size_t>(m_fI - m_sI); }
    bool is_empty() const { return length() == 0; }
    char at(size_t i) const { return m_str.at(_scaled_index(i)); }

    SmartString slice() { return *this; };
    SmartString slice(const size_t start) { return SmartString(m_str, _scaled_index(start), m_fI); };
    SmartString slice(const size_t start, const size_t end) { return SmartString(m_str, _scaled_index(start), _scaled_index(end)); };

    SmartString concat(const SmartString& other);

    // pattern searching / concatenation
    ArrayList<SmartString> split_on(std::string pattern);
    bool contains(const std::string& pattern);
    bool rcontains(const std::string& pattern);
    bool starts_with(const std::string& prefix);
    bool ends_with(const std::string& suffix);
    size_t find(const std::string& pattern);
    size_t find(const std::string& pattern, size_t start);
    size_t rfind(const std::string& pattern);
    size_t rfind(const std::string& pattern, size_t end);
    int count(const std::string& pattern);

    SmartString strip()
    { return lstrip().rstrip(); }
    SmartString lstrip();
    SmartString rstrip();

    SmartString remove_prefix(const std::string& prefix);
    SmartString remove_suffix(const std::string& suffix);

    SmartString replace(char old_value, char new_value);
    SmartString replace(const std::string& old_pattern, const std::string& new_pattern);
    SmartString repeat(int count);

    ArrayList<SmartString> partition(const std::string& separator);
    ArrayList<SmartString> rpartition(const std::string& separator);

    std::string to_string();

};

