#pragma once
// Variables: one scope (Environment) per running function call plus the global scope, and the declared kinds that
// typed variables keep (`int x`, `const string s`). Rules taken from the engine's OkayScriptVM so scripts behave the
// same: blocks (if/while/for bodies) do NOT open a new scope; a function call does. Assigning a name that no scope
// holds creates a global. Reading a name nobody defined gives 0 (the engine's null).
#include "okayscript/Value.hpp"
#include <string>
#include <unordered_map>

namespace okayscript {

/// What a declared variable may hold. `int`/`float`/`bool`/`string` convert on assignment (an int drops its
/// fraction) or fail with an error; `const`/`readonly` cannot change after they are set; `var` holds anything.
enum VarKind : unsigned char { KAny = 0, KInt = 1, KFloat = 2, KBool = 3, KString = 4, KConst = 0x10, KStatic = 0x20 };

/// The kind for a C#-style type word ("int", "double", "string", ...); KAny for any other word.
unsigned char TypeKind(const std::string& typeWord);
/// "an int", "a float", "a bool", "a string" or "a variable".
const char* KindName(unsigned char kind);
/// Convert `v` for a variable `name` of `kind`, or throw a RuntimeError saying why it cannot hold it.
Value CoerceToKind(unsigned char kind, const Value& v, const std::string& name);
/// The starting value of a typed variable declared without one (false, "", or 0).
Value DefaultForKind(unsigned char kind);

struct Environment {
    std::unordered_map<std::string, Value> vars;
    std::unordered_map<std::string, unsigned char> kinds;   ///< typed / const variables only
};

} // namespace okayscript
