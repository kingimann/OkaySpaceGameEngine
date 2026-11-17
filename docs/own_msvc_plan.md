# okay-ms

Internal note for issue #992. Not a user guide. This tree stays off by default: not in CMake, CI, the engine, or a release. It is not a replacement for MSVC. This is years of work. The host tool is POSIX; a Windows run was not run on Windows. Wine is used when it is installed, and a Wine run is written down with the command. Where MinGW gcc is installed, sixteen small programs are compiled with okay-cl and with `x86_64-w64-mingw32-gcc`, run under Wine, and compared. If MinGW is absent that comparison is skipped.

`tools/okayms/` is our own Windows toolchain. Issue #987 (`tools/okaycc/`) is the other compiler. The two trees share no source files.

The code now does stages 1 to 10 in a small form: COFF, a PE32+ linker, `kernel32.dll` imports, `.lib` archives, the relocations named below, a base-relocation table, `.pdata`/`.xdata` for any frame size, COMDAT pick-any and same-size, weak externals, `okay-link`, `okay-cl`, a DLL export table, import libraries, and programs Wine has run. okay-cl covers typedef, enums, unions, function pointers, a callback sort, varargs, and large-struct returns. A dense `switch` is a table of jumps. A nested block has its own locals, so a name there does not overwrite the outer variable. Arrays may have any number of dimensions. Locals and globals accept `{ }` initializer lists, and a local may be initialized from an expression. `continue` jumps to the next iteration of `for`, `while`, and `do`/`while`. A struct larger than 8 bytes is returned through a hidden pointer and is copied on assignment. The caller also copies a big struct argument and passes the address of that copy. Strings and character constants accept `\\n` `\\t` `\\r` `\\0` `\\xHH`, and an enum name can be a switch case. A varargs double is passed in both the xmm register and the matching integer register, including the second and third arguments. A small runtime (`memcpy`, `memset`, `strlen`, `strcmp`, `printf`, `snprintf`, `mainCRTStartup`) is C compiled by okay-cl. `printf` and `snprintf` share one formatter for `%d` `%u` `%x` `%c` `%s` `%f`, with `-` `+` space `0`, a width, and a precision. Programs on that runtime include a quicksort, a hash table, an expression parser, a three-object program that calls a DLL, a Game of Life written with nested `for` loops, a JSON tokenizer, a double matrix product, a text-adventure state machine, a sorted list allocated with `malloc`, and an ASCII mandelbrot. The `for` back-edge jumps to the condition, after `sub rsp`, and the temporary slots stay above the outgoing argument area. That is not a C compiler in the full sense and not an optimiser. Integer `+`, `-`, `*`, `/` and `%` of constants are folded. `/O1` drops a constant `if` and runs a linear scan over the callee-saved registers plus `rcx`, `rdx`, `r8`, `r9` and `r11`. Those caller-saved registers are written back to the stack around a call. `rax` and `r10` stay scratch. `/O2` also propagates constants, removes a dead store, and inlines a small function that has no loop. Each argument expression is stored once and then used under a fresh name. Float and double use SSE. A `/MAP:` file and a `/DEBUG` CodeView directory exist; there is no PDB and no line-number table yet. The next debug record would be a CodeView line block: a file name, then pairs of code offset and source line. Namespaces and references are the C++ subset that runs.

Every line is written here, from the documents below. No other toolchain's code is copied. No third-party libraries. No cryptography. Build with `tools/okayms/build.sh`.

## Stages

1. **COFF object writer and COFF object reader.** File header, sections, relocations, symbols, string table. Done when the byte tests and the round trip pass.
2. **PE linker.** Hand-made COFF objects become one PE32+ console executable. `file` must report `PE32+ executable (console) x86-64`.
3. **Imports.** A call to `ExitProcess` in `kernel32.dll` through an import address table.
4. **`.lib` archives.** Read a COFF archive and the short import-library members inside it.
5. **Relocations.** `REL32`, `ADDR64` and `ADDR32NB`, then the other AMD64 types we actually need. An unknown type is an error.
6. **Unwind.** `.pdata` and `.xdata` for a function prologue, from the x64 exception handling rules.
7. **`okay-link`.** `/OUT:` `/ENTRY:` `/SUBSYSTEM:` `/LIBPATH:` plus object and `.lib` files.
8. **`okay-cl`.** `/c` `/Fo` `/Fe` `/I` `/D` and `/link`. It writes COFF. It does not include or wrap okaycc. `/O2` is accepted and does not pretend to optimise until an optimiser exists.
9. **DLL.** A PE32+ DLL with an export table, and an import library for those exports.
10. **Programs.** Several small programs built with this toolchain and run under Wine.

## Documents

| Work | Written from |
|---|---|
| COFF, PE, archives, imports, exports, relocations | Microsoft, "PE Format" |
| Registers, shadow space, alignment | Microsoft, "x64 calling convention" |
| `.pdata` / `.xdata` | Microsoft, "x64 exception handling" (unwind) |
| Opcode bytes | Intel 64 and IA-32 Architectures Software Developer's Manual, Volume 2. The same long-mode forms are in the AMD64 Architecture Programmer's Manual, Volume 3 |
| C language in `okay-cl` | ISO/IEC 9899. A small C++ subset (namespaces and references) is started from ISO/IEC 14882 |
| Flag names | Microsoft's compiler and linker option descriptions |
| Two-pass link | John R. Levine, *Linkers and Loaders* (the model only). Bytes come from the PE Format document |

## C++ subset

Namespaces and references are in. A namespace is a brace around declarations. `N::f` calls the function declared there. The COFF name is the MSVC decoration: `?` name `@` namespace `@@YA`, then the return type, then the argument types, then `@Z`. `H` is `int` and `X` is `void`. A reference type has an `A` in front of the referred type, so `int&` is `AAH`.

`int &r = x` stores the address of `x`. Reading `r` reads `x`. Assigning `r` writes `x`. It does not reseat `r`.

Member functions, classes, templates and exceptions are not started.

## Estimate

Stages 1 to 3 are file formats. Stages 4 to 9 are the rest of a small linker and a small C subset. A full C++ front end, the Microsoft C++ ABI, a standard library and a real optimiser are not started. `/O2` is not an optimiser. Wine is not Windows.
