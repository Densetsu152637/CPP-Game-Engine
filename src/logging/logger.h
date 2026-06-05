//
// Created by Nicholas on 21/04/26.
//

#pragma once

#include <cstdint>
#include <utility>

#include "text_colour.h"
#include "../structs/arraylist.h"
#include "../structs/smartstring.h"

struct LoggedInformation
{

    int64_t timestamp = 0;
    ArrayList<TextColour> colours;
    std::string msg;

    LoggedInformation() = default;
    LoggedInformation(const int64_t ts, std::string str) : timestamp(ts), msg(std::move(str)) {}

};

SmartString pretty_print(LoggedInformation& info);
ArrayList<SmartString> error_stack_parsing(const std::exception& e);

class Logger {

    ArrayList<LoggedInformation> history;

public:

    Logger() = default;
    virtual ~Logger() = default;

    void logHistory();

    void log(const LoggedInformation& info) { log(info.msg); };
    virtual void log(const std::string&) {}

    Logger& error(const std::exception& e);
    Logger& error(const std::exception& e, std::initializer_list<TextColour> colours);

    Logger& warning(std::exception& e);
    Logger& warning(const std::exception& e, std::initializer_list<TextColour> colours);

    Logger& pass(std::string&& str);

    Logger& push(std::string&& str);
    Logger& push(std::string&& e, std::initializer_list<TextColour> colours);

};
