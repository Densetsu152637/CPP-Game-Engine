//
// Created by Nicholas on 17/04/26.
//

#pragma once

#include <memory>
#include <string>

#include "arraylist.h"

class SmartString;

struct SharedString
{
    ArrayList<char> str;
    SmartString* modifier = nullptr;
    bool modified = false;

    SharedString() {}
    explicit SharedString(const size_t cap) : str(cap) {}
    explicit SharedString(ArrayList<char>&& move)
    { str = std::move(move); }

};

class SmartString {

    std::shared_ptr<SharedString> m_shared;
    size_t m_sI = 0;
    size_t m_fI = 0;

    size_t _scaled_index(size_t t) const;
    bool _assert_modification();

public:

    SmartString() {}

    SmartString(size_t size);
    SmartString(const std::string& s);
    SmartString(ArrayList<char>&& str);
    SmartString(const std::shared_ptr<SharedString>& shared, const size_t sI, const size_t fI) : m_shared(shared), m_sI(sI), m_fI(fI) {}

    SmartString(const SmartString& other) noexcept;
    SmartString(SmartString&& other) noexcept;
    SmartString& operator=(const SmartString& other) noexcept;
    SmartString& operator=(SmartString&& other) noexcept;

    bool operator==(const SmartString& other) const;
    SmartString operator+(const SmartString& other) const;
    SmartString& operator+=(const SmartString& other);
    char operator[](size_t i) const;

    size_t length() const { return m_fI - m_sI; }
    bool is_empty() const { return length() == 0; }
    char at(const size_t i) const { return m_shared->str[_scaled_index(i)]; }

    SmartString slice() { return *this; };
    SmartString slice(const size_t start) const { return SmartString(m_shared, _scaled_index(start), m_fI); };
    SmartString slice(const size_t start, const size_t end) const { return SmartString(m_shared, _scaled_index(start), _scaled_index(end)); };

    SmartString& append(char c);
    SmartString& append(const char* other, size_t length);
    SmartString& append(const ArrayList<char>& str);
    SmartString& append(const std::string& str);
    SmartString& append(const SmartString& str);
    SmartString concat(const SmartString& other) const;

    // pattern searching / concatenation
    ArrayList<SmartString> split_on(const std::string& pattern) const;
    ArrayList<SmartString> split_on(const char token) const;
    bool contains(const std::string& pattern) const;
    bool rcontains(const std::string& pattern) const;
    bool starts_with(const std::string& prefix) const;
    bool ends_with(const std::string& suffix) const;
    int find(const std::string& pattern) const;
    int find(const std::string& pattern, size_t start) const;
    int rfind(const std::string& pattern) const;
    int rfind(const std::string& pattern, size_t end) const;
    size_t count(const std::string& pattern) const;

    SmartString strip() const
    { return lstrip().rstrip(); }
    SmartString lstrip() const;
    SmartString rstrip() const;

    SmartString remove_prefix(const std::string& prefix);
    SmartString remove_suffix(const std::string& suffix);

    SmartString replace(char old_value, char new_value) const;
    SmartString replace(const std::string& old_pattern, const std::string& new_pattern);
    SmartString repeat(int count);

    ArrayList<SmartString> partition(const std::string& separator);
    ArrayList<SmartString> rpartition(const std::string& separator);

    std::string to_string() const;

};

