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
// BasicArgs (Runtime.hpp) is the same for any value type; Args is the standalone one.
#include <cstddef>
#include <string>

namespace okayscript {

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

} // namespace okayscript
