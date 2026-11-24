#pragma once
// Private to okayscript/src: properties and methods on values (s.Length, xs.Add(3), m.ContainsKey("a"), ...).
#include "okayscript/Value.hpp"
#include <cmath>
#include <climits>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace okayscript {
namespace detail {

/// A double as an int for indexes and counts: NaN is 0, out-of-range values clamp, fractions drop.
inline int ToInt(double d) {
    if (std::isnan(d)) return 0;
    if (d >= (double)INT_MAX) return INT_MAX;
    if (d <= (double)INT_MIN) return INT_MIN;
    return (int)d;
}
inline long long ToInt64(double d) {
    if (std::isnan(d)) return 0;
    if (d >= 9.2e18) return LLONG_MAX;
    if (d <= -9.2e18) return LLONG_MIN;
    return (long long)d;
}
inline std::size_t ToSize(double d) { int i = ToInt(d); return i < 0 ? 0 : (std::size_t)i; }

/// Calls a lambda value with arguments (the runtime's CallLambda).
using LambdaInvoker = std::function<Value(const Value&, std::vector<Value>&)>;

/// s.Length, xs.Count, m.Keys, m.someKey ... False when `prop` is not one.
bool ValueProperty(const Value& v, const std::string& prop, Value& out);
/// s.ToUpper(), xs.Add(3), m.ContainsKey("a"), n.ToString("F2") ... False when `m` is not one. Lists and maps are
/// shared, so Add/Remove/Clear change the variable's value.
bool CallValueMethod(const Value& v, const std::string& m, std::vector<Value>& a, Value& out,
                     const LambdaInvoker* invoke);

} // namespace detail
} // namespace okayscript
