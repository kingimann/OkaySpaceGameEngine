# okay-cc

Internal note. Not a user guide. It stays off by default: not in the engine CMake, not in CI, and not in releases. The engine does not include it. Built only by `tools/okaycc/build.sh`. This is our own compiler, not a replacement for MSVC or Clang.

Every line is written here from the documents named below. No copied compiler source, no third-party libraries, no cryptography.

## Status

- The lexer, preprocessor, parser, and type checker work for a C subset (`void`/`char`/`short`/`int`/`long`, signed and unsigned, `typedef`, casts, pointers, arrays, structs, `sizeof` of a type or an expression, functions, control flow, `switch` with `case`, `default`, `break`, and fallthrough, function pointers and arrays of them, `enum`, `union`, and global initializers for arrays, structs, and strings). Diagnostics use the source file name and the line number.
- The intermediate form is three-address code. Integers, calls, control flow, pointers, arrays, globals, string literals, and structs are lowered. Object sizes follow the System V AMD64 data model: `char` 1, `short` 2, `int` 4, `long` and pointers 8. Struct fields are aligned to those sizes. A struct of 1 to 8 bytes is passed and returned in one register, 9 to 16 bytes in two registers, and a larger struct in memory. The caller passes a hidden pointer for a large return. Multi-dimensional arrays use the element size as the step. A pointer to the struct being defined carries that struct's fields.
- The x86-64 code generator emits System V assembly for that subset. Tests assemble it with the system assembler and link it with the system linker (`gcc`). Narrow integers are sign-extended when loaded. An `int *` steps by 4. A `char *` steps by 1.
- A call to `write` is the Linux write system call, so a program can print without a C library. The same programs are compiled with the system gcc and the exit code and standard output are compared.
- The whole test suite has been run with AddressSanitizer and UndefinedBehaviorSanitizer on the compiler (`SAN=1 tools/okaycc/build.sh`). That run was clean.
- The optimizer is a separate pass: it folds constants inside a block and deletes dead code. A simple allocator keeps up to three values in callee-saved registers when their address is not taken. The ELF writer encodes x86-64 instructions itself. It writes a static executable and a relocatable object with a symbol table and `R_X86_64_PC32`, `R_X86_64_PLT32`, and `R_X86_64_64` relocations. `okay-ld` links those objects into one executable. A small libc (`write`, `exit`, `memcpy`, `memset`, `strlen`, `strcmp`, `printf`) is C compiled by okay-cc; `syscall` is the Linux syscall instruction. A variadic function saves the six integer argument registers and the caller sets `al` to 0. `include/stdarg.h` is for okay-cc; gcc keeps its own. Those paths do not use the system assembler or the system linker. The assembly path still does. A COFF writer is not started. The driver is `okay-cc`.

## Written from

- ISO/IEC 9899 (C) and ISO/IEC 14882 (C++) for the language.
- System V AMD64 ABI for the calling convention the code generator will use. The Windows x64 convention is later.
- Intel 64 manual for instruction encodings.
- ELF and COFF specifications for object files, when those writers are built.

A full C++ compiler is still a very large piece of work. Nothing here is "ready" to replace another compiler.
