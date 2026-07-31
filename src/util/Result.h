/*
 * POLPhone - prova técnica SIP/DTMF para Windows.
 * Copyright (C) 2026 POLPhone contributors
 *
 * Este programa é software livre; você pode redistribuí-lo e/ou modificá-lo
 * sob os termos da GNU General Public License versão 2.
 */

#pragma once

#include <cassert>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace polphone::util {

enum class ErrorCode {
    InvalidArgument,
    NotFound,
    Io,
    Parse,
    Validation,
    Pjsip,
    Runtime
};

struct Error {
    ErrorCode code;
    std::string message;
    std::string detail;
};

template <typename T>
class [[nodiscard]] Result {
    static_assert(!std::is_same_v<std::decay_t<T>, Error>,
                  "Result<Error> não é suportado porque Error é o estado de falha.");

public:
    static Result success(T value)
    {
        return Result(std::move(value));
    }

    static Result failure(Error error)
    {
        return Result(std::move(error));
    }

    static Result failure(ErrorCode code, std::string message, std::string detail = {})
    {
        return failure(Error{code, std::move(message), std::move(detail)});
    }

    [[nodiscard]] bool hasValue() const noexcept
    {
        return std::holds_alternative<T>(storage_);
    }

    [[nodiscard]] bool hasError() const noexcept
    {
        return !hasValue();
    }

    explicit operator bool() const noexcept
    {
        return hasValue();
    }

    T& value() & noexcept
    {
        T* result = std::get_if<T>(&storage_);
        assert(result != nullptr && "value() chamado em Result com erro");
        return *result;
    }

    const T& value() const& noexcept
    {
        const T* result = std::get_if<T>(&storage_);
        assert(result != nullptr && "value() chamado em Result com erro");
        return *result;
    }

    T&& value() && noexcept
    {
        T* result = std::get_if<T>(&storage_);
        assert(result != nullptr && "value() chamado em Result com erro");
        return std::move(*result);
    }

    Error& error() & noexcept
    {
        Error* result = std::get_if<Error>(&storage_);
        assert(result != nullptr && "error() chamado em Result com valor");
        return *result;
    }

    const Error& error() const& noexcept
    {
        const Error* result = std::get_if<Error>(&storage_);
        assert(result != nullptr && "error() chamado em Result com valor");
        return *result;
    }

private:
    explicit Result(T value)
        : storage_(std::move(value))
    {
    }

    explicit Result(Error error)
        : storage_(std::move(error))
    {
    }

    std::variant<T, Error> storage_;
};

template <>
class [[nodiscard]] Result<void> {
public:
    static Result success()
    {
        return Result();
    }

    static Result failure(Error error)
    {
        return Result(std::move(error));
    }

    static Result failure(ErrorCode code, std::string message, std::string detail = {})
    {
        return failure(Error{code, std::move(message), std::move(detail)});
    }

    [[nodiscard]] bool hasValue() const noexcept
    {
        return !error_.has_value();
    }

    [[nodiscard]] bool hasError() const noexcept
    {
        return error_.has_value();
    }

    explicit operator bool() const noexcept
    {
        return hasValue();
    }

    Error& error() & noexcept
    {
        assert(error_.has_value() && "error() chamado em Result<void> com sucesso");
        return *error_;
    }

    const Error& error() const& noexcept
    {
        assert(error_.has_value() && "error() chamado em Result<void> com sucesso");
        return *error_;
    }

private:
    Result() = default;

    explicit Result(Error error)
        : error_(std::move(error))
    {
    }

    std::optional<Error> error_;
};

} // namespace polphone::util
