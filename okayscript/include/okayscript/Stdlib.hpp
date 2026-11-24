#pragma once
// The standard library of a standalone OkayScript program (the okayscript CLI, or any embedder that wants it):
//   - the shared core builtins (CoreBuiltins.hpp), the same functions with the same rules as in the engine:
//     abs, sin, cos, sqrt, floor, ceil, round, min, max, pow, clamp, lerp, len (the length of a 2D vector), ... and
//     to_num / num, to_str / str;
//   - input and output, which only a standalone program has (the engine logs `print` and has no console):
//     print(a, b, ...) / println(...)  the values as text, separated by spaces, then a new line
//     write(a, ...)                    the same without the new line
//     input([prompt])                  one line from the input ("" at the end of the input)
//     exit([code])                     stop the program; the CLI ends with that code (0..255)
#include "okayscript/Interpreter.hpp"

namespace okayscript {

void InstallStandardLibrary(Interpreter& interp);

} // namespace okayscript
