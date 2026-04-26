//
// Created by Nicholas on 19/04/26.
//

#include "toolbox.h"

#include <fstream>

constexpr size_t BUFFER_SIZE = 2048;

SmartString readFileSmart(const std::string& fp)
{
    std::ifstream file(fp, std::ios::binary);
    if (!file)
    {
        // handle error however your engine does it TODO
        return SmartString();
    }

    ArrayList<char> str;
    char buffer[BUFFER_SIZE];

    while (file)
    {
        file.read(buffer, BUFFER_SIZE);
        std::streamsize count = file.gcount();
        bool stop = false;

        if (0 == count) break;

        for (std::streamsize i = 0; i < count; ++i)
        {
            if ('\0' == buffer[i])
            {
                count = i;
                stop = true;
                break;
            }
        }

        str.append(buffer, count);

        if (stop)
            break;
    }

    return SmartString(std::move(str));
}

std::string readFile(const std::string& fp)
{
    return readFileSmart(fp).to_string();
}

ArrayList<SmartString> readLines(const std::string& fp)
{
    return readFileSmart(fp).split_on('\n');
}

void writeToFile(const SmartString& contents, const std::string& fp)
{
    if (
        std::ofstream outFile(fp);
        outFile.is_open()
    ) {
        outFile << contents.to_string() << std::endl;
        outFile.close();
    }
}