#pragma once
// The standard library of a standalone OkayScript program (the okayscript CLI, or any embedder that wants it).
//
// InstallCoreLibrary: the shared builtins (CoreBuiltins.hpp), the same functions with the same rules as in the
// engine: maths, conversions, text and encodings, lists, maps and sets, JSON, random numbers (with a seed) and time.
// Nothing in it touches files, the environment or the process.
//
// InstallStandardLibrary: the core library, and input and output, which only a standalone program has (the engine
// logs `print` and has no console):
//   print(a, b, ...) / println(...)  the values as text, separated by spaces, then a new line
//   write(a, ...)                    the same without the new line
//   input([prompt])                  one line from the input ("" at the end of the input)
//   exit([code])                     stop the program; the CLI ends with that code (0..255)
//
// InstallSystemLibrary: what reaches outside the program, for the CLI only (never installed by the engine, whose
// scripts have no file or process access): sleep(seconds); file_read, file_write, file_append, file_exists, is_dir,
// file_delete, file_size, list_dir, make_dir, delete_dir; env(name [, default]). The CLI also gives `args` (the
// command line) and import's file loader.
#include "okayscript/Interpreter.hpp"

namespace okayscript {

void InstallCoreLibrary(Interpreter& interp);
void InstallStandardLibrary(Interpreter& interp);
void InstallSystemLibrary(Interpreter& interp);

} // namespace okayscript
