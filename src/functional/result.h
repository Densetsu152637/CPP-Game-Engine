//
// Created by Nicholas on 17/04/26.
//

#pragma once

#include <exception>
#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>

template <typename T>
class Result
{
    std::exception_ptr m_exception;
    std::optional<T> m_result;

public:
    Result() = default;
    explicit Result(const T& res) : m_result(res) {}
    explicit Result(T&& res) : m_result(std::move(res)) {}
    explicit Result(std::exception_ptr e) : m_exception(std::move(e)) {}
    Result(const Result&) = default;
    Result(Result&&) noexcept = default;
    Result& operator=(const Result&) = default;
    Result& operator=(Result&&) noexcept = default;
    ~Result() = default;

    static Result<T> success(const T& res)
    { return Result<T>(res); }

    static Result<T> success(T&& res)
    { return Result<T>(std::move(res)); }

    static Result<T> failure(const std::exception& e)
    { return Result<T>(std::make_exception_ptr(e)); }

    static Result<T> failure(std::exception_ptr e)
    { return Result<T>(std::move(e)); }

    bool is_success() const
    { return m_result.has_value() && nullptr == m_exception; }

    bool is_failure() const
    { return !is_success(); }

    T& get()
    {
        if (!is_success())
        {
            if (m_exception)
                std::rethrow_exception(m_exception);
            throw std::runtime_error("Result does not contain a value");
        }

        return *m_result;
    }

    const T& get() const
    {
        if (!is_success())
        {
            if (m_exception)
                std::rethrow_exception(m_exception);
            throw std::runtime_error("Result does not contain a value");
        }

        return *m_result;
    }

    std::exception_ptr exception() const
    { return m_exception; }

    template <typename U>
    Result<U> map(std::function<U(T&)> func)
    {
        if (is_failure())
            return Result<U>::failure(m_exception);

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

template <>
class Result<void>
{
    std::exception_ptr m_exception;

public:
    Result() = default;
    explicit Result(std::exception_ptr e) : m_exception(std::move(e)) {}

    static Result<void> success()
    { return Result<void>(); }

    static Result<void> failure(const std::exception& e)
    { return Result<void>(std::make_exception_ptr(e)); }

    static Result<void> failure(std::exception_ptr e)
    { return Result<void>(std::move(e)); }

    bool is_success() const
    { return nullptr == m_exception; }

    bool is_failure() const
    { return !is_success(); }

    void get() const
    {
        if (m_exception)
            std::rethrow_exception(m_exception);
    }

    std::exception_ptr exception() const
    { return m_exception; }

    template <typename U>
    Result<U> map(std::function<U()> func)
    {
        if (is_failure())
            return Result<U>::failure(m_exception);

        try
        {
            return Result<U>::success(func());
        }
        catch (...)
        {
            return Result<U>::failure(std::current_exception());
        }
    }
};
