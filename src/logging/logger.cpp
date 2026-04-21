//
// Created by Nicholas on 21/04/26.
//

#include "logger.h"

#include <chrono>

#include "../util/stopwatch.h"

SmartString pretty_print(LoggedInformation& info)
{
    SmartString date_time(formatDateTime(info.timestamp));
    SmartString ret(date_time.length()); // gonna be at least this length

    ret += TextColour::RESET;
    ret += TextColour::GREY.from_txt_clr();
    ret += "[";
    ret += date_time;
    ret += "]";
    ret += TextColour::RESET;

    if (!info.colours.empty())
    {
        info.colours.for_each([&](TextColour& c)
        {
            ret += c.from_txt_clr();
        });
    }

    ret += info.msg;
    ret += TextColour::RESET;

    return ret;
}

ArrayList<SmartString> error_stack_parsing(const std::exception& e)
{
    const char* msg = e.what();
    int i = 0, start = 0, end = 0;
    ArrayList<SmartString> arr;

    while (true)
    {
        if (msg[i] == '\n')
        {
            arr.emplace(end - start);
            arr[-1].append(msg + start, end - start);
            start = end + 1;
        }

        if (msg[i++] == '\0') break;
        end++;
    };

    return arr;
}

void Logger::logHistory()
{
    history.for_each([&](const LoggedInformation& info){ this->log(info); });
};

Logger& Logger::error(const std::exception& e)
{
    return this->error(e, { TextColour::RED });
}

Logger& Logger::error(const std::exception& e, const std::initializer_list<TextColour> colours)
{
    auto stack_trace = error_stack_parsing(e);
    stack_trace.for_each([&](SmartString& trace)
    {
        this->push("[WARNING] | " + trace.to_string(), colours);
    });
    return *this;
}

Logger& Logger::warning(std::exception& e)
{
    return this->error(e, { TextColour::YELLOW });
}

Logger& Logger::warning(const std::exception& e, const std::initializer_list<TextColour> colours)
{
    auto stack_trace = error_stack_parsing(e);
    stack_trace.for_each([&](const SmartString& trace)
    {
        this->push("[ERROR] | " + trace.to_string(), colours);
    });
    return *this;
}

Logger& Logger::pass(std::string&& str)
{
    return this->push(std::move(str), { TextColour::GREEN });
}

Logger& Logger::push(std::string&& str)
{
    return this->push(std::move(str), {});
}

Logger& Logger::push(std::string&& e, const std::initializer_list<TextColour> colours)
{
    history.emplace(
        get_time_ns(),
        e
    );

    for (TextColour c : colours)
    {
        history[-1].colours.append(c);
    }

    return *this;
}
