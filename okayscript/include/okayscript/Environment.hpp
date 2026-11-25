#pragma once
// Variable kinds: what a typed declaration (`int x`, `const string s`) may hold. The scopes themselves are
// BasicEnvironment in Runtime.hpp. Rules taken from the engine's OkayScriptVM: blocks (if/while/for bodies) do NOT open
// a new scope; a function call does. Assigning a name that no scope holds creates a global. Reading a name nobody
// defined gives 0 (the engine's null).
#include <string>

namespace okayscript {

/// `int`/`float`/`bool`/`string` convert on assignment (an int drops its fraction) or fail with an error;
/// `const`/`readonly` cannot change after they are set; `static` is shared by every run of the same source;
/// `var` holds anything.
enum VarKind : unsigned char { KAny = 0, KInt = 1, KFloat = 2, KBool = 3, KString = 4, KConst = 0x10, KStatic = 0x20 };

/// The kind for a C#-style type word ("int", "double", "string", ...); KAny for any other word.
unsigned char TypeKind(const std::string& typeWord);
/// "an int", "a float", "a bool", "a string" or "a variable".
const char* KindName(unsigned char kind);

} // namespace okayscript
