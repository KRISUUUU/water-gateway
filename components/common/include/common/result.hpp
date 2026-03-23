#pragma once

#include "common/error.hpp"
#include <utility>
#include <new>

namespace common {

// Lightweight Result type for error handling without exceptions.
// Modeled after Rust's Result<T, E> but simplified for embedded use.
//
// Usage:
//   Result<int> ok_result = Result<int>::ok(42);
//   Result<int> err_result = Result<int>::error(ErrorCode::InvalidArgument);
//
//   if (ok_result.is_ok()) { int val = ok_result.value(); }
//   if (err_result.is_error()) { ErrorCode ec = err_result.error(); }

template <typename T>
class Result {
public:
    static Result ok(const T& value) {
        Result r;
        r.ok_ = true;
        new (&r.storage_) T(value);
        return r;
    }

    static Result ok(T&& value) {
        Result r;
        r.ok_ = true;
        new (&r.storage_) T(std::move(value));
        return r;
    }

    static Result error(ErrorCode code) {
        Result r;
        r.ok_ = false;
        r.error_code_ = code;
        return r;
    }

    Result(const Result& other) : ok_(other.ok_), error_code_(other.error_code_) {
        if (ok_) {
            new (&storage_) T(*reinterpret_cast<const T*>(&other.storage_));
        }
    }

    Result(Result&& other) noexcept
        : ok_(other.ok_), error_code_(other.error_code_) {
        if (ok_) {
            new (&storage_) T(std::move(*reinterpret_cast<T*>(&other.storage_)));
        }
    }

    Result& operator=(const Result& other) {
        if (this != &other) {
            destroy();
            ok_ = other.ok_;
            error_code_ = other.error_code_;
            if (ok_) {
                new (&storage_) T(*reinterpret_cast<const T*>(&other.storage_));
            }
        }
        return *this;
    }

    Result& operator=(Result&& other) noexcept {
        if (this != &other) {
            destroy();
            ok_ = other.ok_;
            error_code_ = other.error_code_;
            if (ok_) {
                new (&storage_)
                    T(std::move(*reinterpret_cast<T*>(&other.storage_)));
            }
        }
        return *this;
    }

    ~Result() { destroy(); }

    bool is_ok() const { return ok_; }
    bool is_error() const { return !ok_; }

    const T& value() const { return *reinterpret_cast<const T*>(&storage_); }
    T& value() { return *reinterpret_cast<T*>(&storage_); }

    ErrorCode error() const { return error_code_; }

    // Returns value if ok, otherwise returns the provided default
    T value_or(const T& default_value) const {
        return ok_ ? value() : default_value;
    }

private:
    Result() : ok_(false), error_code_(ErrorCode::Unknown) {}

    void destroy() {
        if (ok_) {
            reinterpret_cast<T*>(&storage_)->~T();
        }
    }

    bool ok_;
    ErrorCode error_code_;
    typename std::aligned_storage<sizeof(T), alignof(T)>::type storage_;
};

// Specialization for void — used when an operation can succeed or fail
// but produces no value on success.
template <>
class Result<void> {
public:
    static Result ok() {
        Result r;
        r.ok_ = true;
        return r;
    }

    static Result error(ErrorCode code) {
        Result r;
        r.ok_ = false;
        r.error_code_ = code;
        return r;
    }

    bool is_ok() const { return ok_; }
    bool is_error() const { return !ok_; }
    ErrorCode error() const { return error_code_; }

private:
    Result() : ok_(false), error_code_(ErrorCode::Unknown) {}

    bool ok_;
    ErrorCode error_code_;
};

} // namespace common
