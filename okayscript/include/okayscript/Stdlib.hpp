#pragma once
// The standard natives every OkayScript program can use, registered through the same Define/Args API (Native.hpp)
// an embedder uses:
//   print(a, b, ...) / println(...)  the values as text, separated by spaces, then a new line
//   write(a, ...)                    the same without the new line
//   input([prompt])                  one line from the input ("" at the end of the input)
//   len(x)                           characters of text, items of a list or map
//   str(x)                           x as text
//   num(x)                           a number from a number, true/false or numeric text (an error otherwise)
//   type(x)                          "number", "bool", "string", "list", "map" or "function"
//   abs sqrt pow floor ceil round min max sin cos tan clamp   math on numbers (min/max take 1 or more)
//   exit([code])                     stop the program (the CLI ends with that code)
// and the global constant PI.
#include "okayscript/Interpreter.hpp"

namespace okayscript {

void InstallStandardLibrary(Interpreter& interp);

} // namespace okayscript
