//
// Created by Nicholas on 18/04/26.
//

#pragma once

#include <string>

#include "arraylist.h"

namespace string_search
{

    ArrayList<char> to_list(const std::string& s);

    int find_first(const ArrayList<char>& str, const std::string& pattern, int start, int finish);
    int find_last(const ArrayList<char>& str, const std::string& pattern, int start, int finish);
    ArrayList<int> z_search(const ArrayList<char>& str, const std::string& pattern, int start, int finish);
    ArrayList<int> z_algo(const ArrayList<char>& txt);

}
