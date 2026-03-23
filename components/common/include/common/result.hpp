#pragma once

#include "common/error.hpp"

namespace common {

template <typename T>
class Result {
public:
    Result(const T& value)
        : ok_(true), value_(value), error_(ErrorCode::Ok) {
    }

    Result(T&& value)
        : ok_(true), value_(static_cast<T&&>(value)), error_(ErrorCode::Ok) {
    }

    Result(ErrorCode error)
        : ok_(false), value_(), error_(error) {
    }

    [[nodiscard]] bool ok() const {
        return ok_;
    }

    [[nodiscard]] const T& value() const {
        return value_;
    }

    [[nodiscard]] T& value() {
        return value_;
    }

    [[nodiscard]] ErrorCode error() const {
        return error_;
    }

private:
    bool ok_;
    T value_;
    ErrorCode error_;
};

template <>
class Result<void> {
public:
    Result()
        : ok_(true), error_(ErrorCode::Ok) {
    }

    Result(ErrorCode error)
        : ok_(false), error_(error) {
    }

    [[nodiscard]] bool ok() const {
        return ok_;
    }

    [[nodiscard]] ErrorCode error() const {
        return error_;
    }

private:
    bool ok_;
    ErrorCode error_;
};

}  // namespace common
