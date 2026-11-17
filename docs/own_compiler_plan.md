# okay-cc

Internal note. Not a user guide. It stays off by default: not in the engine CMake, not in CI, and not in releases. The engine does not include it. Built only by `tools/okaycc/build.sh`. This is our own compiler, not a replacement for MSVC or Clang.

Every line is written here from the documents named below. No copied compiler source, no third-party libraries, no cryptography.

## Status

- The lexer, preprocessor, parser, and type checker work for a C subset (`void`/`char`/`int`, pointers, arrays, functions, control flow).
- The intermediate form is three-address code for that subset's integer programs. Pointers, arrays, structs, and globals are not in the IR yet.
- The optimizer is not started.
- x86-64 code generator, assembler, ELF, COFF, and our own linker: not started. The driver is `okay-cc`.

## Written from

- ISO/IEC 9899 (C) and ISO/IEC 14882 (C++) for the language.
- System V AMD64 ABI for the calling convention the code generator will use. The Windows x64 convention is later.
- Intel 64 manual for instruction encodings.
- ELF and COFF specifications for object files, when those writers are built.

A full C++ compiler is still a very large piece of work. Nothing here is "ready" to replace another compiler.
