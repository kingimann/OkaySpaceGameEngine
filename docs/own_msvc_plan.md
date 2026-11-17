# okay-ms

Internal note for issue #992. Not a user guide. This tree stays off by default: not in CMake, CI, the engine, or a release. It is not a replacement for MSVC. This is years of work. The host tool is POSIX; a Windows run was not run on Windows. Wine is used when it is installed, and a Wine run is written down with the command. Where MinGW gcc is installed, sixteen small programs are compiled with okay-cl and with `x86_64-w64-mingw32-gcc`, run under Wine, and compared. If MinGW is absent that comparison is skipped.

`tools/okayms/` is our own Windows toolchain. Issue #987 (`tools/okaycc/`) is the other compiler. The two trees share no source files.

The code now does stages 1 to 10 in a small form: COFF, a PE32+ linker, `kernel32.dll` imports, `.lib` archives, the relocations named below, a base-relocation table, `.pdata`/`.xdata` for any frame size, COMDAT pick-any and same-size, weak externals, `okay-link`, `okay-cl`, a DLL export table, import libraries, and programs Wine has run. okay-cl covers typedef, enums, unions, function pointers, a callback sort, varargs, and large-struct returns. A dense `switch` is a table of jumps. A nested block has its own locals, so a name there does not overwrite the outer variable. Arrays may have any number of dimensions. Locals and globals accept `{ }` initializer lists, and a local may be initialized from an expression. `continue` jumps to the next iteration of `for`, `while`, and `do`/`while`. A struct larger than 8 bytes is returned through a hidden pointer and is copied on assignment. The caller also copies a big struct argument and passes the address of that copy. Strings and character constants accept `\\n` `\\t` `\\r` `\\0` `\\xHH`, and an enum name can be a switch case. A varargs double is passed in both the xmm register and the matching integer register, including the second and third arguments. A small runtime (`memcpy`, `memset`, `strlen`, `strcmp`, `printf`, `snprintf`, `mainCRTStartup`) is C compiled by okay-cl. `printf` and `snprintf` share one formatter for `%d` `%u` `%x` `%c` `%s` `%f`, with `-` `+` space `0`, a width, and a precision. Programs on that runtime include a quicksort, a hash table, an expression parser, a three-object program that calls a DLL, a Game of Life written with nested `for` loops, a JSON tokenizer, a double matrix product, a text-adventure state machine, a sorted list allocated with `malloc`, and an ASCII mandelbrot. The `for` back-edge jumps to the condition, after `sub rsp`, and the temporary slots stay above the outgoing argument area. That is not a C compiler in the full sense and not an optimiser. Integer `+`, `-`, `*`, `/` and `%` of constants are folded. `/O1` drops a constant `if` and runs a linear scan over the callee-saved registers plus `rcx`, `rdx`, `r8`, `r9` and `r11`. Those caller-saved registers are written back to the stack around a call. `rax` and `r10` stay scratch. `/O2` also propagates constants, removes a dead store, and inlines a small function that has no loop. Each argument expression is stored once and then used under a fresh name. Float and double use SSE. A `/MAP:` file and a `/DEBUG` CodeView directory exist, and `/Z7` writes CodeView line records into the object. There is still no PDB, no symbol record and no type record, and the linker does not copy `.debug$S` into the image. Namespaces, references, classes with member functions, single inheritance and virtual functions are the C++ subset that runs.

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

### Classes and virtual functions

`class` and `struct` may hold fields and member functions. A body may be inside the class or outside (`int C::f(int x) { ... }`). Inside a body a bare field name or member function name means `this->name`. `Base::f()` calls that class's function without the table. A member function is an ordinary function whose first parameter is `this`. One base class is allowed (`: public B`).

Layout (from the C++ standard for what must hold, and the MSVC x64 layout as a design target, not copied from any compiler): the table pointer is the first field of a class that has a virtual function. A derived class starts after the whole base object, including the base's tail padding. The base fields keep their offsets.

COFF names, written from the public decoration rules: `?f@C@@` access letter, `EAA` (64-bit non-const this, one calling convention) or `EBA` for a const member function, return type, parameter types, `@Z`. The access letter is `Q`, `A`, `I` for public, private, protected, and `U`, `E`, `M` for the same when virtual. A struct return has `?A` in front of its type. The table is `??_7C@@6B@` in `.rdata`: one eight-byte slot per virtual function, each an ADDR64 relocation to the function. A pure virtual slot stays zero.

A virtual call through a pointer, a reference, or `*p` loads the table pointer from the object, indexes it by slot and calls through that. A call on a named object is a direct call, as in C++. The table pointer is stored where a local object is declared.

Not done: constructors and destructors, so there is no member initializer list and no `new`; a global or array of an object with virtual functions (it needs a constructor that runs at startup); more than one base; a virtual function added in a class whose base has none; virtual bases; overloads that differ only in parameter types (overloads are told apart by parameter count); parameter-name back references in decorated names (MSVC writes `1` for a repeated type; we write it again); access checks (the access letter is only used in the name); a virtual call whose object expression has a side effect (it is refused with an error); the struct-return slot comes before `this` in the call, MSVC puts `this` first; templates; exceptions.

Written from: ISO/IEC 14882 (class, member function, virtual function, override rules), the public MSVC name decoration description, the x64 calling convention (this in rcx), PE Format (ADDR64).

Tests: `tests/test_class.cpp` checks the decorated names, the `.rdata` table and its six ADDR64 relocations, and runs two programs under Wine; each is also built with MinGW g++ and the exit codes must match.

## CodeView line records

`okay-cl /Z7` (or `CompileInput::debug_lines`) adds a `.debug$S` section to the object. It is off by default. Layout, from the CodeView C13 subsection description and the PE Format relocation list: a 4-byte signature (4); then subsections, each a kind (4 bytes), a length (4 bytes) and the data padded to 4 bytes.

- `DEBUG_S_LINES` (0xF2), one per function. Header: code offset (SECREL relocation), section number (SECTION relocation), flags 0, code size. Then one block per source file: file id (an offset into the checksum table), line count, block size, and for each line a code offset (from the function start) and a 32-bit word with the line in the low 24 bits, no extra lines, and the statement bit set.
- `DEBUG_S_FILECHKSMS` (0xF4): per file, the name's offset in the string table, a checksum size and kind of 0 (no checksum). The files used are numbered in order of first use.
- `DEBUG_S_STRINGTABLE` (0xF3): a leading empty string, then each file name.

New relocation names: `kRelAmd64Section` (10) and `kRelAmd64Secrel` (11). The linker does not copy `.debug$S` or `.debug$T` sections (it has no PDB writer), so their relocations are not applied.

Lines: the preprocessor keeps, for every output line, the file and line it came from (comments keep their newlines, a directive leaves a blank line, an include is replaced by the file's lines), and every token has its output line. A statement records the line where it starts. A function has an entry at its prologue (the line of its `{`) and one at its epilogue (the line of its `}`). An entry with the same line as the one before it, or the same code offset, is merged. A header's functions get their own file block.

Not done: a PDB (the MSF container, the type and symbol streams), symbol and type records, column numbers, separate lines for the parts of a `for` header, line records for code made by the inliner (it keeps the statement's own line), checking the tables with a real debugger or with Microsoft's tools (none were run; the test reads the bytes back with its own parser).

## Estimate

Stages 1 to 3 are file formats. Stages 4 to 9 are the rest of a small linker and a small C subset. A full C++ front end, the Microsoft C++ ABI, a standard library and a real optimiser are not started. `/O2` is not an optimiser. Wine is not Windows.
