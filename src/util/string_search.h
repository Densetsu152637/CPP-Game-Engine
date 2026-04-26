//
// Created by Nicholas on 18/04/26.
//

#pragma once

#include <string>

#include "../structs/arraylist.h"

namespace string_search
{

    ArrayList<char> to_list(const std::string& s);

    int find_first(const char* str, const std::string& pattern, const size_t start, const size_t finish);
    int find_last(const char* str, const std::string& pattern, const size_t start, const size_t finish);
    ArrayList<int> z_search(const char* str, const std::string& pattern, const size_t start, const size_t finish);
    ArrayList<int> z_algo(const char* txt, const size_t length);

}
