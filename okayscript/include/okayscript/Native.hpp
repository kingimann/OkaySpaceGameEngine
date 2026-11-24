#pragma once
// Native (C++) functions callable from OkayScript.
//
//   interp.Define("add", [](okayscript::Args& a) { return okayscript::Value{a.Number(0) + a.Number(1)}; },
//                 okayscript::Arity::Exactly(2));
//
// The arity is checked before the function runs; a wrong count is a RuntimeError such as
// "add() takes 2 arguments, got 3" carrying the line of the call. Args::Number/String/Bool/List/Map check the type of
// one argument and fail the same way ("add(): argument 1 must be a number, got string"). Anything else a native
// throws as a ScriptError (Args::Fail) becomes a runtime error on the calling line; other C++ exceptions do too.
#include "okayscript/Error.hpp"
#include "okayscript/Value.hpp"
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace okayscript {

class Runtime;

/// How many arguments a native takes. max < 0 means "any number from min up".
struct Arity {
    int min = 0;
    int max = -1;
    static Arity Exactly(int n) { return {n, n}; }
    static Arity Between(int lo, int hi) { return {lo, hi}; }
    static Arity AtLeast(int n) { return {n, -1}; }
    static Arity Any() { return {0, -1}; }
    bool Accepts(std::size_t n) const { return (int)n >= min && (max < 0 || (int)n <= max); }
    /// "2 arguments", "1 to 3 arguments", "at least 1 argument".
    std::string Describe() const;
};

/// The arguments of one native call.
class Args {
public:
    Args(Runtime& rt, const std::string& name, std::vector<Value>& values, int line)
        : m_rt(rt), m_name(name), m_values(values), m_line(line) {}

    std::size_t Size() const { return m_values.size(); }
    bool Has(std::size_t i) const { return i < m_values.size(); }
    /// Argument i, or 0 (null) when it was not given.
    Value Get(std::size_t i) const { return i < m_values.size() ? m_values[i] : Value{}; }
    const Value& operator[](std::size_t i) const { return m_values.at(i); }
    std::vector<Value>& All() { return m_values; }

    /// Typed reads: fail with "name(): argument N must be a <type>, got <type>" when argument i is something else.
    double Number(std::size_t i) const;
    std::string String(std::size_t i) const;
    bool Bool(std::size_t i) const;
    Value::ArrayRef List(std::size_t i) const;
    Value::MapRef Map(std::size_t i) const;

    /// Call a script function value (a lambda, or a function's name as text) with these arguments.
    Value Call(const Value& fn, std::vector<Value> args) const;

    const std::string& Name() const { return m_name; }
    int Line() const { return m_line; }
    Runtime& Rt() const { return m_rt; }
    /// Throw a RuntimeError "name(): message" on the calling line.
    [[noreturn]] void Fail(const std::string& message) const;

private:
    void Need(std::size_t i, const char* type) const;
    Runtime& m_rt;
    const std::string& m_name;
    std::vector<Value>& m_values;
    int m_line;
};

using NativeFn = std::function<Value(Args&)>;

struct Native {
    NativeFn fn;
    Arity arity;
};

} // namespace okayscript
