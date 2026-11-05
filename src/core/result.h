// Result<T>: value or static error message (no exceptions in this project).
#pragma once
#include <utility>

template <class T>
class Result {
public:
    static Result ok(T&& v) {
        Result r;
        r.value_ = std::move(v);
        r.error_ = nullptr;
        return r;
    }
    static Result fail(const char* msg) {
        Result r;
        r.error_ = msg;
        return r;
    }
    bool ok() const { return error_ == nullptr; }
    T& value() { return value_; }
    const T& value() const { return value_; }
    const char* error() const { return error_; }

private:
    Result() = default;
    T value_{};
    const char* error_ = "UNINITIALIZED";
};
