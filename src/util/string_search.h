//
// Created by Nicholas on 18/04/26.
//

#pragma once

#include <string>

#include "../structs/arraylist.h"

namespace string_search
{

    ArrayList<char> to_list(const std::string& s);

    int find_first(const char* str, const std::string& pattern, size_t start, size_t finish);
    int find_last(const char* str, const std::string& pattern, size_t start, size_t finish);
    ArrayList<size_t> z_search(const char* str, const std::string& pattern, size_t start, size_t finish);
    ArrayList<size_t> z_algo(const char* txt, size_t length);

}
