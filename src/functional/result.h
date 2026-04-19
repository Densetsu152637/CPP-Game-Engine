//
// Created by Nicholas on 17/04/26.
//

#pragma once

#include <exception>
#include <functional>
#include <optional>
#include <utility>

template <typename T>
class Result {
    std::optional<T> m_result;
    std::exception_ptr m_exception;

public:
    Result() = default;
    explicit Result(T res) : m_result(std::move(res)) {}
    explicit Result(std::exception_ptr e) : m_exception(std::move(e)) {}
    Result(const Result& r) = default;
    Result(Result&& r) noexcept = default;
    Result& operator=(const Result& r) = default;
    Result& operator=(Result&& r) noexcept = default;
    ~Result() = default;

    template <typename R> static Result<R> success(R res) { return Result<R>(res); }
    template <typename R> static Result<R> failure(const std::exception& e) { return Result<R>(std::make_exception_ptr(e)); }
    template <typename R> static Result<R> failure(std::exception_ptr e) { return Result<R>(std::move(e)); }

    bool is_success() const { return m_result.has_value(); }
    bool is_failure() const { return !is_success(); }

    T& get()
    {
        if (!is_success())
        {
            std::rethrow_exception(m_exception);
        }

        return *m_result;
    }

    const T& get() const
    {
        if (!is_success())
        {
            std::rethrow_exception(m_exception);
        }

        return *m_result;
    }

    std::exception_ptr exception() const
    {
        return m_exception;
    }

    template <typename U> Result<U> map(std::function<U (T&)> func)
    {
        if (is_failure())
        {
            return Result<U>::failure(m_exception);
        }

        try
        {
            return Result<U>::success(func(get()));
        }
        catch (...)
        {
            return Result<U>::failure(std::current_exception());
        }
    }
};
