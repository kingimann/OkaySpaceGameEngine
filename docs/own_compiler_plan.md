# okay-cc

Internal note. Not a user guide. It stays off by default: not in the engine CMake, not in CI, and not in releases. The engine does not include it. Built only by `tools/okaycc/build.sh`. This is our own compiler, not a replacement for MSVC or Clang.

Every line is written here from the documents named below. No copied compiler source, no third-party libraries, no cryptography.

## Status

- The lexer, preprocessor, parser, and type checker work for a C subset (`void`/`char`/`short`/`int`/`long`, signed and unsigned, `typedef`, casts, pointers, arrays, structs, `sizeof` of a type or an expression, functions, control flow). Diagnostics use the source file name and the line number.
- The intermediate form is three-address code. Integers, calls, control flow, pointers, arrays, globals, string literals, and structs are lowered. Object sizes follow the System V AMD64 data model: `char` 1, `short` 2, `int` 4, `long` and pointers 8. Struct fields are aligned to those sizes. A struct of at most 8 bytes is passed and returned in a register. Multi-dimensional arrays use the element size as the step. A pointer to the struct being defined carries that struct's fields.
- The x86-64 code generator emits System V assembly for that subset. Tests assemble it with the system assembler and link it with the system linker (`gcc`). Narrow integers are sign-extended when loaded. An `int *` steps by 4. A `char *` steps by 1.
- The optimizer is not started. The ELF writer encodes x86-64 instructions itself and writes a static executable (one read/write/execute load at address 0x400000), so those tests do not use the system assembler or the system linker. The assembly path still does. A COFF writer and a general linker are not started. The driver is `okay-cc`.

## Written from

- ISO/IEC 9899 (C) and ISO/IEC 14882 (C++) for the language.
- System V AMD64 ABI for the calling convention the code generator will use. The Windows x64 convention is later.
- Intel 64 manual for instruction encodings.
- ELF and COFF specifications for object files, when those writers are built.

A full C++ compiler is still a very large piece of work. Nothing here is "ready" to replace another compiler.
