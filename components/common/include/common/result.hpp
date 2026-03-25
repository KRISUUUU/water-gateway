#pragma once

#include "common/error.hpp"
#include <new>
#include <utility>

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

template <typename T> class Result {
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
            new (ptr()) T(*other.ptr());
        }
    }

    Result(Result&& other) noexcept : ok_(other.ok_), error_code_(other.error_code_) {
        if (ok_) {
            new (ptr()) T(std::move(*other.ptr()));
        }
    }

    Result& operator=(const Result& other) {
        if (this != &other) {
            destroy();
            ok_ = other.ok_;
            error_code_ = other.error_code_;
            if (ok_) {
                new (ptr()) T(*other.ptr());
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
                new (ptr()) T(std::move(*other.ptr()));
            }
        }
        return *this;
    }

    ~Result() {
        destroy();
    }

    bool is_ok() const {
        return ok_;
    }
    bool is_error() const {
        return !ok_;
    }

    const T& value() const {
        return *ptr();
    }
    T& value() {
        return *ptr();
    }

    ErrorCode error() const {
        return error_code_;
    }

    // Returns value if ok, otherwise returns the provided default
    T value_or(const T& default_value) const {
        return ok_ ? value() : default_value;
    }

  private:
    Result() : ok_(false), error_code_(ErrorCode::Unknown) {}

    void destroy() {
        if (ok_) {
            ptr()->~T();
        }
    }

    T* ptr() {
        return std::launder(reinterpret_cast<T*>(storage_));
    }
    const T* ptr() const {
        return std::launder(reinterpret_cast<const T*>(storage_));
    }

    bool ok_;
    ErrorCode error_code_;
    alignas(T) unsigned char storage_[sizeof(T)];
};

// Specialization for void — used when an operation can succeed or fail
// but produces no value on success.
template <> class Result<void> {
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

    bool is_ok() const {
        return ok_;
    }
    bool is_error() const {
        return !ok_;
    }
    ErrorCode error() const {
        return error_code_;
    }

  private:
    Result() : ok_(false), error_code_(ErrorCode::Unknown) {}

    bool ok_;
    ErrorCode error_code_;
};

} // namespace common
