//
// Created by Nicholas on 18/04/26.
//

#ifndef CPP_GAME_ENGINE_STRING_SEARCH_H
#define CPP_GAME_ENGINE_STRING_SEARCH_H
#include <string>

#include "arraylist.h"

namespace string_search
{

    ArrayList<char> to_list(const std::string& s);

    int find_first(ArrayList<char>& str, const std::string& pattern, int start, int finish);
    int find_last(ArrayList<char>& str, const std::string& pattern, int start, int finish);
    ArrayList<int> z_search(ArrayList<char>& str, const std::string& pattern, int start, int finish);
    ArrayList<int> z_algo(const ArrayList<char>& txt);

}


#endif //CPP_GAME_ENGINE_STRING_SEARCH_H
