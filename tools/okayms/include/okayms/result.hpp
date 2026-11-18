#pragma once

#include <string>
#include <utility>

namespace okayms {

// A value, or a plain error string. Format errors do not throw.
template <typename T>
struct Result {
    T value{};
    std::string error;

    bool ok() const { return error.empty(); }

    static Result success(T v) {
        Result out;
        out.value = std::move(v);
        return out;
    }

    static Result fail(std::string message) {
        Result out;
        out.error = std::move(message);
        return out;
    }
};

}  // namespace okayms
