//
// Created by Nicholas on 17/04/26.
//

#ifndef CPP_GAME_ENGINE_RESULT_H
#define CPP_GAME_ENGINE_RESULT_H
#include <exception>
#include <functional>
#include <optional>

template <typename T>
class Result {

    const std::optional<T> m_result;
    const std::exception m_exception;

public:
    Result(T res) : m_result(res) {}
    Result(std::exception e) : m_exception(e) {}

    template <typename R> static Result<R> success(R res) { return Result<R>(res); }
    template <typename R> static Result<R> failure(std::exception e) { return Result<R>(e); }

    bool is_success() { return m_result.has_value(); }

    T& get()
    {
        if (!is_success()) throw m_exception;
        return m_result;
    }

    template <typename U> Result<U> map(std::function<U (T&)> func)
    {
        if (!is_success()) return Result::failure(m_exception); // propagates exception down the path
        try { return Result::success(func(get())); }
        catch (std::exception e) { return Result::failure(e); }
    }

};

#endif //CPP_GAME_ENGINE_RESULT_H
