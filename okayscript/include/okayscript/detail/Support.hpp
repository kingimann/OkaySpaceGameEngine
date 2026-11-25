#pragma once
// Small helpers of the OkayScript core, for any value type V with the script-value interface (okayscript::Value, or
// the engine's vs::VsValue): safe number casts, null, equality, typed-variable conversion. The rules are the engine
// OkayScriptVM's (written for this project).
#include "okayscript/Environment.hpp"
#include "okayscript/Error.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace okayscript {
namespace detail {

/// Whether a value type has 64-bit whole numbers (IntPtr / V::Int): the standalone okayscript::Value does, the
/// engine's value does not, so for the engine every integer path below compiles away and its rules stay the same.
template <class V, class = void> struct HasInt : std::false_type {};
template <class V> struct HasInt<V, std::void_t<decltype(std::declval<const V&>().IntPtr())>> : std::true_type {};
/// The whole number in v, or nullptr (always nullptr for a value type without them).
template <class V> inline const std::int64_t* IntOf(const V& v) {
    if constexpr (HasInt<V>::value) return v.IntPtr();
    else { (void)v; return nullptr; }
}
/// A whole number as a value: an Int where the value type has them, else the number (a 32-bit float).
template <class V> inline V MakeInt(std::int64_t i) {
    if constexpr (HasInt<V>::value) return V::Int(i);
    else return V{(float)i};
}

// Number -> integer without undefined behaviour: NaN is 0, out-of-range values clamp.
inline int SafeInt(double v) {
    if (v != v) return 0;
    if (v >= 2147483648.0) return std::numeric_limits<int>::max();
    if (v <= -2147483649.0) return std::numeric_limits<int>::min();
    return static_cast<int>(v);
}
inline long long SafeInt64(double v) {
    if (v != v) return 0;
    if (v >= 9223372036854775808.0) return std::numeric_limits<long long>::max();
    if (v <= -9223372036854775808.0) return std::numeric_limits<long long>::min();
    return static_cast<long long>(v);
}
/// Negative values and NaN read as 0.
inline std::size_t SafeSize(double v) {
    if (!(v > 0.0)) return 0;
    if (v >= static_cast<double>(std::numeric_limits<std::size_t>::max())) return std::numeric_limits<std::size_t>::max();
    return static_cast<std::size_t>(v);
}

/// The longest text a script may build (64 Mi characters): `s + s` in a loop or PadLeft(1e9) is a runtime error
/// instead of using up all memory.
constexpr std::size_t kMaxTextLength = std::size_t(1) << 26;
inline void CheckTextLength(std::size_t n) {
    if (n > kMaxTextLength)
        throw RuntimeError("text too long (more than " + std::to_string(kMaxTextLength) + " characters)", 0);
}
/// a + b as text, refusing a result over kMaxTextLength.
inline std::string JoinText(const std::string& a, const std::string& b) {
    CheckTextLength(a.size() + b.size());
    return a + b;
}

/// The vector type of a value type (what AsVec3 returns).
template <class V> using VecOf = decltype(std::declval<const V&>().AsVec3());
/// Calls a lambda value with arguments.
template <class V> using LambdaInvoker = std::function<V(const V&, std::vector<V>&)>;

template <class V> bool IsLambdaValue(const V& v) {
    auto m = v.AsMap();
    return m && m->count("__lambda");
}
/// A new list of these values (the engine's value has no List constructor).
template <class V> V ListOf(std::initializer_list<V> items) {
    V v = V::MakeArray();
    for (const V& x : items) v.AsArray()->push_back(x);
    return v;
}
/// A value a call can run: a lambda, or a named function {"__fn": name} (a module's has "__mod" too).
template <class V> bool IsCallableValue(const V& v) {
    auto m = v.AsMap();
    return m && (m->count("__lambda") || m->count("__fn"));
}
/// A plain-class object: a map tagged with the class that made it.
template <class V> bool IsClassInstance(const V& v) {
    auto m = v.AsMap();
    if (!m) return false;
    auto it = m->find("__class");
    return it != m->end() && it->second.IsString();
}

} // namespace detail

/// The engine's null: the number 0, an empty list/map reference, or a handle to an object that is gone.
template <class V> bool IsNullish(const V& v) {
    if (v.IsBool() || v.IsString() || v.IsVec3()) return false;
    if (v.IsObject()) return !v.AsBool();
    if (v.IsArray()) return !v.AsArray();
    if (v.IsMap()) return !v.AsMap();
    return v.IsNumber() && v.AsFloat() == 0.0f;
}

/// `==` in scripts. Class objects are equal only to themselves; a handle equals the same handle, its name as text, or
// null once its object is gone; text compares as text; anything else as numbers (so two lists of one length are equal).
template <class V> bool ValuesEqual(const V& a, const V& b) {
    if (const std::int64_t* x = detail::IntOf(a)) {
        if (const std::int64_t* y = detail::IntOf(b)) return *x == *y;
        if (b.IsNumber() || b.IsBool()) return (double)*x == (double)b.AsFloat();
    } else if (const std::int64_t* y = detail::IntOf(b)) {
        if (a.IsNumber() || a.IsBool()) return (double)a.AsFloat() == (double)*y;
    }
    if (detail::IsClassInstance(a) || detail::IsClassInstance(b)) {
        if (detail::IsClassInstance(a) && detail::IsClassInstance(b)) return a.AsMap().get() == b.AsMap().get();
        return false;
    }
    if (a.IsObject() || b.IsObject()) {
        if (a.IsObject() && b.IsObject()) return a.AsObjectId() == b.AsObjectId() || (IsNullish(a) && IsNullish(b));
        const V& o = a.IsObject() ? a : b;
        const V& x = a.IsObject() ? b : a;
        if (x.IsString()) return o.AsString() == x.AsString();
        if (x.IsBool()) return o.AsBool() == x.AsBool();
        return IsNullish(o) && IsNullish(x);
    }
    if (a.IsString() || b.IsString()) return a.AsString() == b.AsString();
    return a.AsFloat() == b.AsFloat();
}

namespace detail {
template <class V> const char* ValueKindName(const V& v) {
    if (v.IsString()) return "text";
    if (v.IsBool()) return "true/false";
    if (v.IsNumber()) return "a number";
    if (v.IsVec3()) return "a vector";
    if (v.IsArray()) return "a list";
    if (v.IsMap()) return "a map";
    return "that value";
}
} // namespace detail

/// Convert `v` for a variable `name` of `kind`, or throw a RuntimeError (line 0: the statement fills it in).
template <class V> V CoerceToKind(unsigned char kind, const V& v, const std::string& name) {
    auto fail = [&](const char* hint) -> V {
        throw RuntimeError("'" + name + "' is " + KindName(kind) + ", so it can't hold " + detail::ValueKindName(v) + hint, 0);
    };
    switch (kind & 0x0F) {
        case KInt:
            if (detail::IntOf(v)) return v;   // a whole number already
            if (v.IsNumber()) return V{(float)detail::SafeInt64(v.AsFloat())};   // drops the fraction, like (int)
            if (v.IsBool()) return V{v.AsBool() ? 1.0f : 0.0f};
            return fail(v.IsString() ? " (use int.Parse(text))" : "");
        case KFloat:
            if (detail::IntOf(v)) return V{v.AsFloat()};
            if (v.IsNumber()) return v;
            if (v.IsBool()) return V{v.AsBool() ? 1.0f : 0.0f};
            return fail(v.IsString() ? " (use float.Parse(text))" : "");
        case KBool:
            if (v.IsBool()) return v;
            if (v.IsNumber()) return V{v.AsFloat() != 0.0f};
            if (v.IsObject()) return V{v.AsBool()};   // an object reference: true while it exists
            return fail("");
        case KString:
            if (v.IsString()) return v;
            if (v.IsObject()) return v;   // keeps its identity and still reads as the object's name
            return fail(" (turn it into text with $\"{value}\" or .ToString())");
        default: return v;
    }
}
template <class V> V DefaultForKind(unsigned char kind) {
    switch (kind & 0x0F) { case KBool: return V{false}; case KString: return V{std::string()};
                           default: return V{}; }
}

namespace detail {
// What `new List<T>()`, `new Dictionary<K, V>()` and `new T[n]` turn into (the parser lowers them to calls of
// __new_list / __new_map / __new_array); the standalone interpreter and the engine both register these.
template <class V> V NewList(std::vector<V>& a) {
    V v = V::MakeArray();
    if (!a.empty()) if (auto src = a[0].AsArray()) *v.AsArray() = *src;   // initializer / copy
    return v;
}
template <class V> V NewArray(std::vector<V>& a) {
    V v = V::MakeArray();
    const int n = a.empty() ? 0 : SafeInt(a[0].AsFloat());
    if (n > 0) v.AsArray()->assign((std::size_t)(n < (1 << 20) ? n : (1 << 20)), V{0.0f});
    return v;
}
template <class V> V NewMap(std::vector<V>& a) {
    V v = V::MakeMap();
    if (a.empty()) return v;
    if (auto src = a[0].AsMap()) { *v.AsMap() = *src; return v; }
    if (auto pairs = a[0].AsArray())                          // { {"a", 1}, {"b", 2} }
        for (auto& pr : *pairs)
            if (auto kv = pr.AsArray(); kv && kv->size() >= 2) (*v.AsMap())[(*kv)[0].AsString()] = (*kv)[1];
    return v;
}
} // namespace detail

} // namespace okayscript
