# okay-ms

Internal note for issue #992. Not a user guide. This tree stays off by default: not in CMake, CI, the engine, or a release. It is not a replacement for MSVC. This is years of work. The host tool is POSIX; a Windows run was not run on Windows. Wine is used when it is installed, and a Wine run is written down with the command.

`tools/okayms/` is our own Windows toolchain. Issue #987 (`tools/okaycc/`) is the other compiler. The two trees share no source files.

The code now does stages 1 to 10 in a small form: COFF, a PE32+ linker, `kernel32.dll` imports, `.lib` archives, the relocations named below, a base-relocation table, `.pdata`/`.xdata` for any frame size, COMDAT pick-any and same-size, weak externals, `okay-link`, `okay-cl`, a DLL export table, import libraries, and programs Wine has run. okay-cl covers typedef, enums, unions, function pointers, varargs, and large-struct returns. A small runtime (`memcpy`, `memset`, `strlen`, `strcmp`, `printf`, `mainCRTStartup`) is C compiled by okay-cl. Programs on that runtime include a quicksort, a hash table, an expression parser, a three-object program that calls a DLL, a Game of Life, a JSON tokenizer, and a double matrix product. That is not a C compiler in the full sense and not an optimiser. Integer `+`, `-`, `*`, `/` and `%` of constants are folded. `/O1` drops a constant `if` and keeps some locals in callee-saved registers. `/O2` also propagates constants and removes a dead store. Float and double use SSE. A `/MAP:` file and a `/DEBUG` CodeView directory exist; there is no PDB.

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
| C language in `okay-cl` | ISO/IEC 9899. C++ is not implemented; ISO/IEC 14882 is the document a later front end would be written from |
| Flag names | Microsoft's compiler and linker option descriptions |
| Two-pass link | John R. Levine, *Linkers and Loaders* (the model only). Bytes come from the PE Format document |

## Estimate

Stages 1 to 3 are file formats. Stages 4 to 9 are the rest of a small linker and a small C subset. A full C++ front end, the Microsoft C++ ABI, a standard library and a real optimiser are not started. `/O2` is not an optimiser. Wine is not Windows.
