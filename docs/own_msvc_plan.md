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

The `/O2` inliner skips member functions (their body names `this->field`); `/O1` and `/O2` are tested on a virtual-call program.

Not done: `new` and `delete`; a global or array of an object with virtual functions (it needs a constructor that runs at startup); more than one base; a virtual function added in a class whose base has none; virtual bases; overloads that differ only in parameter types (overloads are told apart by parameter count); parameter-name back references in decorated names (MSVC writes `1` for a repeated type; we write it again); access checks (the access letter is only used in the name); a virtual call whose object expression has a side effect (it is refused with an error); the struct-return slot comes before `this` in the call, MSVC puts `this` first; templates; exceptions.

Written from: ISO/IEC 14882 (class, member function, virtual function, override rules), the public MSVC name decoration description, the x64 calling convention (this in rcx), PE Format (ADDR64).

Tests: `tests/test_class.cpp` checks the decorated names, the `.rdata` table and its six ADDR64 relocations, and runs two programs under Wine; each is also built with MinGW g++ and the exit codes must match.

### Constructors and destructors (round 11)

A class can declare constructors (`C(int a) : x(a), y(b) { ... }`, overloaded by parameter count), a destructor (`~C()`) and may leave both out. Constructors are not inherited. A constructor runs, in this order: the base class constructor (arguments from `: Base(...)`, else the default one), the table pointer store when the class has virtual functions (so a virtual call inside a constructor reaches that class's own function), then each member in declaration order (a member of class type with a constructor is constructed with its initializer arguments or the default constructor; any other member in the list is assigned), then the body. A destructor stores the table pointer, runs its body, destroys the members in reverse order, then the base. A class with a base or a member that needs a constructor or destructor, and none of its own, gets an implicit one. A local `C x(args);` or `C x;` runs the constructor at the declaration. Its destructor runs at the end of the block, before `return` (the return value is computed first into a hidden local), and before `break` and `continue` for the blocks they leave, latest object first. A `virtual ~C()` is an ordinary virtual function in the table (MSVC puts a scalar deleting destructor in slot 0; this tree does not, because there is no `delete`).

COFF names: a constructor is `??0C@@QEAA@H@Z` (`??0`, class, `@@`, access letter, `EAA`, `@` where the return type would be, parameters, `@Z`), a destructor `??1C@@QEAA@XZ`. Written from ISO/IEC 14882 [class.ctor], [class.dtor], [class.base.init], [stmt.jump] and the public MSVC decoration description.

Not done: an array, a `static` local or a global object of a class that has a constructor or destructor; move constructors; a destructor call for a temporary; `goto` out of a block that has objects with destructors; a destructor name that is shadowed by an inner variable of the same name when `return` is inside the inner block; a field of class type declared by its bare name in a class that is not itself C++ (`struct S { A a; }` needs the class to be written with a constructor, a base or a member function; a plain C struct field still needs `struct A a;`); `= default`; the scalar deleting destructor.

Tests: `tests/test_ctor.cpp` checks the decorated names and runs five programs under Wine (order of construction and destruction, return and nested blocks, `break` and `continue`, a derived class with a virtual function and an implicit constructor); each is also built with MinGW g++ and the exit codes must match.

### Round 13: objects at global scope, arrays, references returned, temporaries

- Global objects, static local objects and arrays of objects with constructors and destructors. `C g(args);`, `C g;` and `C g[N];` at file scope are built by `__okay_init` and destroyed by `__okay_fini` (both made by the compiler); `main` (or `entry` when there is no `main`) calls the first at its start and the second after its value is computed, at every `return` and at its end. A file with neither `main` nor `entry` never runs them. A `static` local object is built the first time its declaration is reached (a hidden guard flag) and destroyed with the globals, before them. A local array `C a[N];` (one dimension, no initializer) constructs element 0 to N-1 and destroys N-1 down to 0, also at `return`, `break` and `continue`. Not done: a static or global array of objects with an initializer, a static local array, objects at namespace scope other than the global one (they work, untested).
- `new T[n]` and `delete[] p`. A class with a constructor or destructor goes through `__vnew_C` and `__vdel_C` (made once per class as syntax trees; the block has an 8-byte count header when destructors must run); other types are a plain `malloc` and `free`. `delete[]` of a null pointer does nothing.
- Fixed: `pointer - integer` lost the upper 32 bits of the address (only `+` was scaled and widened). Found by `delete[]`; the vector test covers it.
- References returned from functions (`T &f()`, `return *this;`, `c.inc().inc()`, `f() = 5`): the function returns the address, a call is dereferenced.
- A reference parameter may be given an expression that is not an object (`f(x + 1)`, `peek(makeA(4))`): the value goes into a hidden local before the statement. Not supported inside a loop condition (an error says so). A temporary that has a destructor is destroyed after the statement.
- Copies made in an `if` or `while` condition are destroyed once the condition is evaluated (before the branch, and per loop iteration), not after the whole statement.

Written from ISO/IEC 14882 [basic.start.init], [basic.start.term], [stmt.dcl], [expr.new], [expr.delete], [dcl.ref], [class.temporary], [expr.add]. Tests: `tests/test_globals.cpp` (global/static/array objects with the last destructor calling ExitProcess so the exit code shows the whole order; new[]/delete[]), `tests/test_copy.cpp` (condition copies, references). Each is also built by MinGW g++ under Wine and the exit codes must match. Test-first: `test_cond`, `test_globals` and `test_refs` were written after the code and were not seen failing first; the pointer-subtraction bug was found by the vector test failing (exit 5 against 44).

### new, delete, copy constructors and the scalar deleting destructor (round 12)

`new T`, `new T(args)` and `delete p;`. `new` calls `malloc` (the program declares `malloc` and `free`, or links the runtime) and then the constructor; constructors now return their object, so `new C(args)` is one call. `delete p;` does nothing for a null pointer, otherwise runs the destructor and calls `free`. A class with a virtual destructor puts the scalar deleting destructor (`??_GC@@UEAAPEAXI@Z`: runs the destructor, frees the memory when its flag argument is non-zero, returns this) in the destructor's table slot, and `delete` calls it through the table, so deleting a derived object through a base pointer runs the derived destructor first. A class that has virtual functions now always gets a constructor (implicit if needed), which stores the table pointer.

References: a parameter can be `T &` or `const T &`; the caller passes the address of its argument, the body sees a reference (so `p.x` and `p.x = n` work). Reference returns are not done. A plain struct can be named without the word `struct` where a declaration clearly follows.

Copy constructors: constructors overload by parameter type as well as count (`A(int)` and `A(const A &)` coexist; the internal name has an `r` or `v` per parameter). `A b(a);` and `A b = a;` (with `a` an object of the class or a derived class) run the copy constructor; `A b = f();` still copies bytes (the object is built in place). A class passed by value, when it has a copy constructor, is copied by that constructor into a hidden local and the copy is destroyed after the statement (Itanium style, which is what MinGW g++ does). A class with no copy constructor of its own but a base or member that has one gets an implicit copy constructor that copies the base (by its copy constructor, or field by field) and each member.

Not done: `new T[n]` and `delete[]`; `new` of a scalar with an initializer; a reference return; a reference bound to a temporary; the hidden argument copies of a `while` or `if` condition are destroyed after the whole statement, not after the condition; copies for arguments that are not a plain name, field, element or dereference; array, static and global objects with constructors (next).

Written from: ISO/IEC 14882 [expr.new], [expr.delete], [class.copy.ctor], [dcl.ref], [class.temporary], and the public MSVC decoration description (`??_G`). Tests: `tests/test_new.cpp` (2 programs) and `tests/test_copy.cpp` (1 program), each also built by MinGW g++ and run under Wine with equal exit codes.

## Fix: rsi and rdi around a call (round 11)

`test_reg` and `test_spill` failed (the program hung or returned a wrong value). Cause: before the argument loads the call code reloaded a local kept in `rsi` or `rdi` from its stack slot, because the struct-argument copies use `rep movsb` (which clobbers `rsi` and `rdi`), but nothing had stored the register there first, so the loop counter was read from an unwritten slot. The call code now stores `rsi` and `rdi` locals before the copies. Reproduced first with the two failing tests (plus a one-function `/O1` program under Wine: loop of calls hung). Written from the x64 calling convention (rsi and rdi are callee-saved) and the Intel manual (`rep movsb`).

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
