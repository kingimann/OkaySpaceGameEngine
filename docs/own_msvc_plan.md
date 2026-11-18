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
### Round 14: templates and array initializers

- Templates. `template <typename T, int N = 3>` before a function or a `struct`/`class`. The declaration is kept as a token range and parsed again with its parameters bound (a type parameter is a typedef, a value parameter an enumerator, so `T items[N]` works). A class template is instantiated when it is named (`Box<int>`, `Box<Box<int> >`, defaults filled in, the class may name itself inside its body); its members are compiled for each instance. A function template is instantiated at the first call: all arguments given (`maxOf<int>(8, 2)`), the first ones given (`convert<int>(7)`, the rest deduced) or all deduced from the argument types (a parameter `T`, `T *` or `T &` binds to the argument's type; literals, names, fields, elements, calls and arithmetic have a type). Instance symbols follow the public MSVC decoration: `??$twice@H@@YAHH@Z`, a member `?get@?$Box@H@@QEBAHXZ`, a constructor `??0?$Box@H@@QEAA@H@Z`, a nested instance `?$Box@U?$Box@H@@@`, a value parameter `$02` for 3 (`$0` then the value minus one for 1 to 10, `A@` for 0, hex digits A to P ending in `@` above that). Test: `tests/test_templates.cpp` (the program, plus 8 symbol names). Not done: member templates, variadic templates, template template parameters, a template used before its definition (specialization, out-of-class members, class-argument deduction and `typename T::type` are in round 15).
- A member call on a returned object (`f().g()`) puts the object in a hidden local. A function body can now be parsed in the middle of another one (a template instance) without losing the outer function's locals (the outer `cur_locals` was reset; fixed).
- Brace initializers for arrays of objects (`P gl[3] = { 1, {2, 3} };`, global or local): each item is the argument (or, in braces, the arguments) of a constructor of that element; elements without an item use the default constructor. Test: `arrinit` in `tests/test_globals.cpp`. Not done: a static local array, a multi-dimensional array of objects.

## Exceptions (round 15): a frame-list runtime, not the MSVC model

What is built. `throw e;`, `throw;`, `try { } catch (T) { } catch (T &) { } catch (T *) { } catch (...) { }` work, with destructors of unwound locals, array elements and partly built members and bases run in reverse order, rethrow, nested try, try left by `return`, `break` and `continue`, a handler that throws, a class caught by a base reference (the thrown class and its bases are matched by name), and an uncaught exception that ends the program with exit code 3 (what MinGW `abort` gives under Wine) after a message on stderr. The scheme is the one of tools/okaycc (see its plan): a stack of records in the runtime (`runtime/eh.c`), a try frame holding the registers saved by `__okay_setjmp` (an inline sequence in the code generator: rbx rbp rdi rsi r12 to r15 rsp and the resume address, the Windows x64 callee-saved set), `throw` copying the object into runtime memory and unwinding the records until a try frame is entered, the handlers testing the type name and passing the exception on when none fits. A function with a `try` keeps its locals in memory (no register allocation for it). Test: `tests/test_exceptions.cpp` (three programs, each also built by MinGW g++ with exceptions and run under Wine, exit codes equal; the uncaught program gives 3 in both).

What it is not. It does not use `.pdata`/`.xdata` handlers, `ThrowInfo`, `CatchableType`, `FuncInfo` or `__CxxFrameHandler`, so it cannot interoperate with code that throws through SEH or the MSVC runtime, and a debugger or Windows Error Reporting sees ordinary calls. The MSVC-compatible design written earlier (FuncInfo with unwind map, try map and IP-to-state map; `_CxxThrowException` raising 0xE06D7363; a handler that walks frames with `RtlVirtualUnwind` and `RtlUnwindEx`) is still not started: it needs a state number per call site, funclets for catch bodies and destructor actions, and the table writers (none exist; they should be checked byte for byte against the documented structures). That remains.

Not supported (`throw` as an expression and the copy constructor of a thrown class are in round 16): hidden temporaries (by-value argument copies, references bound to temporaries, results of `f().g()`) are not recorded for unwinding, a pointer catch needing a conversion to a base, `new` whose constructor throws does not free the memory, a `return` inside a constructor body of a class with destructible members (an error says so), exception specifications, `std::exception`. Cost: each object with a destructor pays a record push and pop.

### Round 15: template gaps (specialization, out-of-class members, class-parameter deduction, `typename`)

Tried with `sh tools/okayms/build.sh` (runs `tests/test_templates2.cpp`; it needs MinGW g++ and Wine for the exit-code comparison, otherwise the g++ half is skipped with a message). okay-ms is a separate tool and is off by default; nothing in the engine build calls it. Written from ISO/IEC 14882 [temp.spec], [temp.class.spec], [temp.mem], [temp.deduct.type] and [temp.res]; no code from other compilers.

- Explicit and partial specialization of class templates. `template <> struct Box<char> { ... }` and `template <typename T> struct Box<T *> { ... }`, also value arguments (`Tag<T, 4>`, `Tag<int, N>`). The argument list after the name is read as a pattern (a parameter is a marker, `Node<T>` is a named pattern), and an instance is matched against every specialization; the one with the fewest parameters wins (ties: first declared). Instance names and symbols stay `?$Box@PEAH@` style, built from the real arguments. Explicit specialization of a function template, `template <> int pick<char>(char)` and `template <> int pick(int)`; it is compiled when first called and replaces the instance (same decorated name as the instance would have).
- Member functions of a class template defined outside the class, including constructors (with initializer lists), destructors, `const` members and a different parameter name in the definition (`template <typename U> U Stack<U>::pop()`). The definition is kept as a token range; when the class is instantiated the declaration inside the class picks up the body (matched by name, parameter count and `const`) and the parameter names are bound to the instance's arguments.
- Deduction through a class template parameter: `length(Node<T> *)`, `head(const Node<T> &)`, `cap(Arr<T, N> &)` (a value parameter is deduced too). A class instance remembers its template and arguments. A parameter such as `typename C::type` is a non-deduced context and is skipped.
- `typename T::type`, `T::type`, `Wrap<int>::type`, `typename Traits<P>::type` as a return type, parameter, local or member type. `typedef` and `using name = type;` are now accepted inside a class body (visible to the members after them, and as `C::name`).
- Tests: `tests/test_templates2.cpp`, five programs (specialization, out-of-class members, class-parameter deduction, typename, traits with value specializations), each run by okay-ms and by MinGW g++ under Wine with equal exit codes, plus three symbol-name checks. The whole `build.sh` suite passes with `-Wall -Wextra -Werror`.
- Not done (members of a specialization defined outside the class, static data members, member templates, `T::value` and variadic templates are in round 16): partial ordering beyond "fewest parameters", a specialization or member definition that appears after the first use (definitions must come first), static data members and member templates, `typename` inside an expression, a `T::value` constant, variadic and template template parameters, conversions in deduction (a later argument that differs from the first deduced type is accepted as before; the first one wins).

### Round 16: static members, specialization members, member templates, variadic templates, throw expressions

Tried with `sh tools/okayms/build.sh` (new tests `tests/test_templates3.cpp` and `tests/test_exceptions2.cpp`; each program is run by okay-ms and by MinGW g++ under Wine and the exit codes must match). okay-ms is a separate tool and off by default; nothing in the engine build calls it. To try one program by hand: `tools/okayms/build/okay-cl file.cpp /Feout.exe` (a file that defines `int entry(void)`; built by `build.sh`) and run `out.exe` under Wine. Programs that throw also need `runtime/eh.c` and `runtime/mem.c` linked, which `tests/test_exceptions2.cpp` does. Written from ISO/IEC 14882 [class.static.data], [class.static.mfct], [temp.expl.spec], [temp.class.spec.mfunc], [temp.mem], [temp.variadic], [expr.sizeof], [expr.throw], [except.throw], [expr.type.conv]; no code from other compilers.

- Static data members and class constants, in plain classes and in class templates. `static int n;` in a class makes a global named by the public MSVC decoration (`?n@P@@2HA`, `?count@?$Box@H@@2HA`; `B` instead of `A` for const; `EA` after a pointer type). The definition `int P::n = 7;` (global scope) or `template <typename T> int Box<T>::count = 10;` gives the initial value (without one the member is zero-filled). `static const int k = 5;` and the enumerators of a class are constants: `P::k`, `Box<int>::k`, `T::value` (T a template parameter bound to a class), `obj.k`, `ptr->k` and the bare name inside members all fold to the value. `P::n`, `Box<int>::count`, `Tr<T>::value` work in expressions and statements (`Box<int>::count = 20;` is read as an expression, not a declaration, when a name that is not a type follows the `::`). A class template instance is made when it is named in an expression. Derived classes see the statics of their base. Static member functions (`static int g(int)`, decorated `?g@P@@SAHH@Z`, no `this`) were missing too and are done: `P::g(1)`, `p.g(1)`, bare `g(1)` inside a member.
- Members of a specialization defined outside the class: `int Box<char>::get() const { ... }` and `Box<char>::Box(int z) { ... }` (no `template <>` prefix, as the standard says for an explicitly specialized class), the same for a partial specialization (`template <typename T> int Box<T *>::get() const`) and for static data members (`int Box<char>::cnt = 5;`). A definition is kept with the specialization it was written for (the argument list is spelled with the parameters numbered, so `Box<T *>` and `Box<U *>` match) and attached when that specialization is instantiated. `template <> int Box<char>::get() const { ... }` for an implicit instance of the primary template replaces the primary's definition for those arguments only (member functions; not static data members).
- Member function templates: `template <typename T> T add(T x) { ... }` inside a class or class template, also `static`, `const`, value parameters (`s.fixed<7>()`), several parameters, explicit arguments (`p->add<int>(1)`, `S::id<char>(2)`), deduction from the call, calls from other members, and a member template of a class template (it keeps the class's own arguments). The instance is a member named `add<H>` of the class that declared the template, decorated `??$add@H@S@@QEAAHH@Z`. The body must be inside the class.
- Variadic templates: `template <typename... Ts>`, a function parameter pack `Ts... xs` (also `const Ts &... xs`), `sizeof...(Ts)` and `sizeof...(xs)`, expansion `f(xs...)` in call arguments, deduction of the pack from the call (empty packs too), a class template with a pack (`Cnt<int, char>::value` using `sizeof...(Ts)`, `Cnt<>`), and `Cnt<Ts...>` naming a class with a pack. Function templates of the same name may now be overloaded (the usual recursion `sum(T)` and `sum(T, Rest...)`): a template with exactly as many parameters as the call wins over one with a pack, a pack template takes any number from its fixed parameters up. Pack elements are written one after the other in the decorated name, as MSVC does.
- Exceptions: a thrown named class object is copy-constructed by its copy constructor into storage from the runtime (`__okay_eh_alloc`, then `__okay_throw_built`, new in `runtime/eh.c`), so a counting copy constructor sees one copy, like g++. `throw` is an expression: `c ? x : throw E()`, `p ? *p : throw 7`, a bare `throw` inside an expression. It is a call of a small function made once per thrown type (the class object goes by reference, so it is copied once). `C(args)` now builds a temporary object of a class (`use(E(4))`, `throw E(x + 4)`): the temporary is built before the statement and destroyed after it; a thrown `C(args)` is the exception object itself (no copy, as C++17 requires).
- Test-first, honestly: before the code was written these failed, and I ran them: static members (`a1`: "expected a function template", a plain `static int g()` "expected a type"), the copy-constructor throw (exit 12 against 115 from g++) and `throw E(x + 4)` ("cannot tell the type of the thrown value"). The other programs (specialization members, member templates, variadic, throw expressions) were written together with the code and were not seen failing first.
- Not done: a static data member of an explicit `template <>` implicit-instance member (`template <> int Box<int>::cnt = 1;`); a member function template defined outside its class; constructor templates; `s.template add<int>(1)`; a pack of values (`int... Ns`), pack expansion that is not a bare name (`f(g(xs)...)`, `Ts...` inside a base list or an initializer list), fold expressions, a class template specialization that matches a pack (`Cnt<T, Rest...>`, the usual compile-time recursion), explicit arguments that start a pack in a function call (`f<int, char>(...)`); a static member initialized by an expression that names another static of its own class (`static const int b = a + 1;`); taking the address of an in-class initialized `static const` (it has no storage here); `sizeof(T)` in a class enumerator; a throw-expression whose operand is `C(args)` builds the temporary before the whole statement, so a constructor with side effects runs even when the other branch is taken; the copy constructor of a thrown object that itself throws.

### Round 17: out-of-class member templates, constructor templates, pack expressions, folds, pack specializations

Tried with `sh tools/okayms/build.sh` (full run on head 8bee7dc4 before this round: all 53 test programs green, exit 0; again after this round: green). New test `tests/test_templates4.cpp`: each program is built by okay-ms and by MinGW g++ and run under Wine, and the exit codes must match. Before the code was written all five programs failed (seen: "a member function template needs a body in the class", "no matching constructor in S", "cannot deduce template arguments of twice", "expected an expression", "expected '>'"). okay-ms is a separate tool and off by default; to try one program: `tools/okayms/build/okay-cl file.cpp /Feout.exe` (file defines `int entry(void)`), run `out.exe` under Wine. Written from ISO/IEC 14882 [temp.mem], [temp.names], [temp.variadic], [expr.prim.fold], [temp.class.spec.match]; no code from other compilers.

- Member function template defined outside its class: `template <typename T> T S::add(T x) { ... }`, `template <int N> int S::fixed() const { ... }`, and for a class template `template <typename T> template <typename U> U B<T>::plus(U u) const { ... }` (the class's names and the member's names are both bound, so the definition may rename them). The definition has to come before the first call (as for other out-of-class definitions here). Symbol is the same as for an in-class one: `??$add@H@S@@QEAAHH@Z`.
- Constructor templates: `template <typename T> S(T x) : v(x + 1) {}`, deduced from the constructor call (`S a(3)`, `S b(c)`, also `new`). The instance is a constructor named `??$?0H@S@@QEAA@H@Z`. Simplified choice: a template is used when the call has its number of arguments, is not a copy of the class, and no ordinary constructor takes that many arguments (no full overload ranking). The body must be in the class.
- `s.template add<int>(1)` and `p->template add<int>(1)`: the word `template` after `.` or `->` is accepted (it only says that `<` opens arguments).
- Pack expansion of expressions in a call: `f(g(xs)...)`, `f((xs + 1)...)`, `f(xs * xs..., 100)`: the pattern is read once for each element, with the pack name replaced by that element (several packs of the same length work).
- Fold expressions: `(xs + ...)`, `(... + xs)`, `(init op ... op xs)`, `(xs op ... op init)` for `+ - * / % == != < > <= >= && ||` and `,`. An empty pack gives `true` for `&&`, `false` for `||` and the init value when there is one; any other empty unary fold is an error, as in the standard. The operand must be a cast-expression, so `((xs * xs) + ...)` needs its inner parentheses, as the standard says.
- A class template specialization that matches a pack: `template <typename T, typename... R> struct Len<T, R...>` with the primary `template <typename... Ts> struct Len;` and `template <> struct Len<>`: the usual compile-time recursion (`1 + Len<R...>::value`) works.
- Unverified / not done: only the programs in `test_templates4.cpp` were run (g++ agrees on all); a pack of values (`int... Ns`); expansion outside call arguments (initializer lists, base lists, `Ts...` in a template argument list other than the existing splice); the pattern of a fold or expansion naming the pack inside a nested lambda or a `sizeof...`; fold operators other than the ones listed (no `<<`, `>>`, `&`, `|`, `^`, assignments, `.*`: the compiler has no such binary nodes); a pattern with a comma inside template angle brackets in a call argument; a constructor template defined outside its class; a member function template used before its out-of-class definition; explicit arguments that start a pack in a function call (`f<int, char>(...)`); `bool` is not a type in okay-ms at all (the tests use `int`).

### Round 18: bool, bit operators, real engine sources (measurement)

Tried with `sh tools/okayms/build.sh` (new tests `test_bits.cpp`, `test_pp.cpp`, `test_cxx11.cpp`, `test_members.cpp`; each program is built by okay-ms and by MinGW g++ and run under Wine, exit codes must match; the whole suite was green on the final head, see the report in the issue for the exact run). okay-ms is a separate tool and off by default; nothing in the engine build calls it. Written from ISO/IEC 14882 ([basic.fundamental], [conv.bool], [expr.shift], [expr.bit.and], [expr.xor], [expr.or], [expr.prim.fold], [cpp.cond], [cpp.include], [cpp.pragma], [lex.phases], [dcl.enum], [namespace.def], [class.mem], [basic.lookup.unqual], [expr.const]); no code from other compilers.

How to try it by hand (host tool, POSIX; Wine only for running the result):

1. `sh tools/okayms/build.sh` builds `tools/okayms/build/okay-cl` and runs every test.
2. Compile one engine file to an object (a header goes through a one-line `.cpp` that includes it): `printf '#include "okay/Physics/ForceMode.hpp"\n' > /tmp/w.cpp; tools/okayms/build/okay-cl /c /tmp/w.cpp /Fo/tmp/w.obj /I engine/include /I engine/src /I shared /I tools/okayms/stdinc`. No output and an object file means it compiled. A failing file prints `file:line: message`, where the file is the include name. `tools/okayms/stdinc/` holds our own small `<cstdint>`, `<cstddef>`, `<cmath>`, `<cstring>`, `<cstdlib>`, `<cstdio>`, `<algorithm>`, `<limits>`.

What changed:

- `bool`, `true`, `false`. `bool` is a one-byte unsigned type; a value stored into it (assignment, initializer, return, argument, cast, global data) is first compared with zero, so `bool b = 256;` holds 1. The decorated name letter is `_N`. `true` and `false` are the numbers 1 and 0 (their type is int, not bool: overloading on `bool` is not done).
- `& | ^ << >> ~` and `&= |= ^= <<= >>=`, with the standard precedence (the table in the parser was renumbered: `|` < `^` < `&` < equality < relational < shift < additive). A `>>` is two adjacent `>` tokens that the expression parser joins, so `A<B<int>>` still closes twice. A right shift of an unsigned left operand is logical, otherwise arithmetic; the count goes through `cl`. Constant operands are folded (also `-x`, `~x`, `!x`, and casts of constants to integer types), so `case 1 << 1 | 1:`, `int a[1 << 3]`, `enum { E = 1 << 2 }`, `int g = (1 << 4) | 3;` work. Fold expressions over `| & ^ << >>` work (`(xs | ...)`, `(1 << ... << xs)`, `(xs >> ...)`).
- Preprocessor: `#pragma once`, `#undef`, `#error`, `#line`, other `#pragma` lines are ignored except `#pragma pack`, which is an error (ignoring it would change struct layout silently). `#if` has `~ << >> & | ^ ?:`. A backslash before a newline joins lines. A quoted include is looked up next to the including file first. Errors now say `file:line:`.
- C++ spellings, lowered before parsing (so they are read exactly as the rule says and nothing else changes): `constexpr` (as `const`), `inline`, `explicit`, `override`, `final`, `volatile`, `noexcept`, `[[attributes]]` are dropped; `static_assert(...)` is dropped (NOT checked); `nullptr` is 0; `using Name = type;` is a typedef; `enum class E : T {}` and `enum E : T {}` (the type is int; scoped enumerators are named `E::X`, so two enums may share a name); `static_cast`, `reinterpret_cast`, `const_cast` are a C cast; `int *const p`. `const` before or after a type is accepted in members, parameters and locals (it is not recorded in the type).
- Namespaces: `namespace a::b { }`, unnamed namespaces, nesting. A function is decorated innermost first (`?f@b@a@@YAHH@Z`). Types, variables and enumerators of a namespace share one flat table, and a qualifier in front of them (`geo::unit::kScale`, `std::uint8_t`) is dropped; a call keeps its qualifier. A bare call inside a namespace finds the functions of that namespace and the ones around it. `std::min(a, b)` finds a function template registered by its bare name.
- Classes: default member initializers (`float x = 0.25f;`, `int n{};`, run by the constructor for every member the constructor does not name; a class that only has such members gets an implicit constructor); several members in one declaration (`float x, y, z;`); `sizeof(S)` for a plain struct name, and `sizeof(expr)` for any expression the parser can type; unsized arrays with an initializer (`const char* const kCats[] = {...}`), a string literal in the initial data of a global (a hidden global plus an ADDR64 relocation, also `char s[] = "..."`).
- Bugs found by the measurement and fixed (each has a test that failed before): `p[i]` where `p` is a pointer field or an element of a pointer array or a call result used the address of the pointer instead of the pointer (wrong value); reading a `float` or `double` through a field, `*p`, or a reference loaded the bits into `eax` and left `xmm0` as it was (wrong value, `v.x` was whatever the last float was).

Measurement (36 small files that use only `<cstdint>`, `<cstddef>`, `<cmath>`, `<algorithm>`, `<limits>` or nothing, compiled one at a time with our own headers in `tools/okayms/stdinc/`, `okay-cl /c`, 2026-11-18). The first 23 of them were tried before any change and every one failed (the first error was `#pragma once` for all that got that far): 0 compiled. After: 5 of 36 compile to an object. Compiling is all this measures: none of these objects was linked or run, and "compiles" does not mean the code is right (default arguments, `static_assert`, 64-bit integers and others are left out or wrong, see below).

| File | Result | First error |
|---|---|---|
| `engine/include/okay/Physics/ForceMode.hpp` | compiles | |
| `engine/include/okay/Physics/PhysicsQuality.hpp` | compiles | |
| `engine/include/okay/VisualScript/VsPinType.hpp` | compiles | |
| `engine/src/Audio/Mp3Tables.hpp` | compiles | |
| `shared/LauncherTips.hpp` | compiles | |
| `engine/include/okay/Components/Stance.hpp` | fails | not an address |
| `engine/include/okay/Core/ClockCursor.hpp` | fails | okay/Core/ClockCursor.hpp:19: expected a type |
| `engine/include/okay/Environment/Wind.hpp` | fails | okay/Math/Mathf.hpp:11: global initializer must be a constant |
| `engine/include/okay/Input/InputBuffer.hpp` | fails | okay/Input/InputBuffer.hpp:14: expected ')' |
| `engine/include/okay/Math/Bounds.hpp` | fails | okay/Math/Mathf.hpp:11: global initializer must be a constant |
| `engine/include/okay/Math/ColorSpace.hpp` | fails | cannot open include string |
| `engine/include/okay/Math/Curves.hpp` | fails | cannot open include vector |
| `engine/include/okay/Math/Easing.hpp` | fails | cannot open include cctype |
| `engine/include/okay/Math/Mat3.hpp` | fails | okay/Math/Mathf.hpp:11: global initializer must be a constant |
| `engine/include/okay/Math/Mat4.hpp` | fails | okay/Math/Mathf.hpp:11: global initializer must be a constant |
| `engine/include/okay/Math/Mathf.hpp` | fails | okay/Math/Mathf.hpp:11: global initializer must be a constant |
| `engine/include/okay/Math/Quat.hpp` | fails | okay/Math/Mathf.hpp:11: global initializer must be a constant |
| `engine/include/okay/Math/Ray.hpp` | fails | okay/Math/Mathf.hpp:11: global initializer must be a constant |
| `engine/include/okay/Math/Rect.hpp` | fails | okay/Math/Mathf.hpp:11: global initializer must be a constant |
| `engine/include/okay/Math/Vec2.hpp` | fails | okay/Math/Mathf.hpp:11: global initializer must be a constant |
| `engine/include/okay/Math/Vec3.hpp` | fails | okay/Math/Mathf.hpp:11: global initializer must be a constant |
| `engine/include/okay/Math/VecInt.hpp` | fails | cannot open include functional |
| `engine/include/okay/Physics/PhysicsMaterial.hpp` | fails | not an address |
| `engine/include/okay/Render/Color.hpp` | fails | cannot open include string |
| `engine/include/okay/UI/TooltipPlace.hpp` | fails | okay/UI/TooltipPlace.hpp:26: expected an expression |
| `engine/src/Core/RandomExtras.cpp` | fails | cannot open include atomic |
| `engine/src/Crypto/Blake2b.cpp` | fails | okay/Crypto/Sha256.hpp:62: expected ')' |
| `engine/src/Crypto/ChaCha20.cpp` | fails | okay/Crypto/Sha256.hpp:62: expected ')' |
| `engine/src/Crypto/Sha512.cpp` | fails | okay/Crypto/Sha256.hpp:62: expected ')' |
| `engine/src/Crypto/Util.cpp` | fails | cannot open include CtAnnotate.hpp |
| `engine/src/Input/GestureRecognizer.cpp` | fails | cannot open include string |
| `engine/src/Input/PlayerInput.cpp` | fails | cannot open include array |
| `engine/src/Optimize/LodGroup.cpp` | fails | cannot open include string |
| `engine/src/Optimize/PortalCells.cpp` | fails | cannot open include string |
| `engine/src/UI/FocusGraph.cpp` | fails | cannot open include vector |
| `shared/OkfDecode.hpp` | fails | cannot open include vector |

The first errors, counted: the standard library (`<string>` 5, `<vector>` 3, `<functional>`, `<cctype>`, `<atomic>`, `<array>`: 12 files stop at an include we have not written; a real `std::vector`/`std::string` is a very large piece of work for a template engine at this stage), `static constexpr float X = A / B;` (a float constant expression: 10 files, all through `Mathf.hpp`: a `static constexpr float` member is read back as an integer constant and float operations are not folded), `volatile T *p = static_cast<volatile T *>(q)` in `Sha256.hpp` (the cast text `volatile T *` is not lowered), `std::uint64_t` (okay-ms has no 64-bit integer: `long long` and `__int64` are read as 32-bit `int`; the stdinc `<cstdint>` leaves the 64-bit types out unless `OKAYMS_INT64` is set so a use fails instead of silently being 32 bits), default arguments (`float window = 0.15f` in a parameter list), a braced return `return {x, y, w, h};`, `const T&` result of a conditional expression ("not an address"), and a header that needs a Valgrind include.

Not done / unverified / found and not fixed:

- (Round 19: done, see below.) 64-bit integers do not exist (`long long`, `uint64_t`, `size_t` arithmetic, `std::size_t` as 8 bytes). `typedef long long` is still silently 32 bits. This blocks most real code and is the largest missing piece after the standard library.
- (Round 19: default arguments and a braced `return {...}` are done. Round 20: lambdas, `auto`, range `for` over arrays, operator overloading and overloading by parameter type are done; `static_assert` evaluation is still dropped.) Default arguments, a braced `return {...}`, `static_assert` evaluation, `= default` / `= delete`, lambdas, `auto`, range `for`, operator overloading are not there (not tried one by one: the measurement stops at the first error).
- (Round 19: fixed, with a test.) A float array element is wrong: `float a[2]; a[0] = 1.5f; a[1] = 2.5f; return (int)(a[1] * 4.0f);` returns 5, g++ 10 (seen with the old and the new build; not fixed, no test yet).
- (Round 19: `static constexpr float` members and `using namespace` are done.) `static constexpr float` members, unqualified use of a namespace's variables from another namespace with the same member name, calls of constructors by a qualified name (`geo::Vec(1, 2)`), `using namespace`, `T a[] = {...}` inside a function that is also static, and aggregate default member initializers are not done. A namespace is flat for variables and types: two namespaces that declare the same variable name collide.
- `#pragma pack` is refused, not ignored.
- The `/O2` constant propagation was not looked at for the new compound operators; the tests run at the default level only.
- Run on Linux with Wine only; not run on Windows.

### Round 19: real 64-bit integers, float fixes, default arguments, braced return, and the correctness bugs the new tests found

Tried with `sh tools/okayms/build.sh` on a fresh checkout of branch `claude/okayms-3` at head dece7b8b plus this round (full run: every test green, exit 0, 2026-11-18). New tests: `tests/test_int64.cpp` (9 programs) and `tests/test_round19.cpp` (13 programs in 8 test functions). Each program returns 0 when all its checks hold, or the number of the first check that failed; it is built by okay-ms at `/O0`, `/O1` and `/O2` (three optimization levels, not only the default as before), run under Wine, and also built by MinGW g++ and run under Wine; all four exit codes must be 0. Every new test failed before its code was written (the first errors seen: "expected an expression" for `3000000000LL`, exit codes of 9 and 7 for wrong 64-bit answers, "expected ')'" for default arguments, `return {...}`, `volatile` in a cast, "expected a type" for `using namespace`). okay-ms is a separate tool and off by default; nothing in the engine build calls it. Written from ISO/IEC 14882 ([lex.icon], [basic.fundamental], [conv.integral], [conv.fpint], [conv.double], [conv.bool], [expr.arith.conv], [expr.shift], [expr.cond], [expr.const], [expr.sizeof], [expr.type.conv], [stmt.return], [dcl.fct.default], [dcl.fct.def.default], [dcl.init.aggr], [dcl.dcl], [class.ctor], [namespace.udir], [basic.lookup.unqual]) and the x64 instruction encodings; no code from other compilers.

How to try it by hand (host tool, POSIX; Wine only for running the result):

1. `sh tools/okayms/build.sh` builds `tools/okayms/build/okay-cl` and runs every test (about 2 minutes). It prints `tests N, checks passed M, failed 0` for each test program.
2. One sample program, `tools/okayms/examples/int64_demo.cpp` (64-bit integers, a float array, a default argument, a braced return): `tools/okayms/build/okay-cl tools/okayms/examples/int64_demo.cpp /Feint64_demo.exe /I tools/okayms/stdinc`, then `wine int64_demo.exe; echo $?`. It must print `0`. (A number is the first failed check; open the file and count.) This was run on Linux with Wine, not on Windows.
3. The engine measurement below: `printf '#include "okay/Math/Mathf.hpp"\n' > /tmp/w.cpp; tools/okayms/build/okay-cl /c /tmp/w.cpp /Fo/tmp/w.obj /I engine/include /I engine/src /I shared /I tools/okayms/stdinc`. No output and an object file means it compiled.

What changed:

- **64-bit integers** (`Type::wide`: an int-family type of 8 bytes that lives in all of `rax`). `long long`, `unsigned long long`, `long long int`, `__int64`, `unsigned __int64`, and the typedefs `int64_t`, `uint64_t`, `size_t`, `ptrdiff_t`, `intptr_t`, `uintptr_t` (now real in the stdinc headers, no switch needed). `long` stays 32 bits (the Windows model). Size and alignment 8 in structs, arrays, globals and parameters; the decorated names are `_J` and `_K` (`?f@N@@YA_J_J@Z`). Integer literals follow the table in [lex.icc] with a 32-bit `long`: `3000000000LL`, `4294967296` (decimal too big for `int` is `long long`), `0x80000000` (hex that fits `unsigned int` is `unsigned int`), suffixes `u l ll ull` in any case and `i64`; octal (`0777`) and binary (`0b101`) literals now exist. Arithmetic `+ - * / %`, `& | ^ ~ << >>`, unary `-`, `++ --`, compound assignments, comparisons, `&& || ! ?:` and conditions test and use all 64 bits when an operand is wide; the result type follows the usual arithmetic conversions (a wide operand wins, unsigned wins over signed of the same width, `unsigned int` converts to `long long` by zero extension). Conversions are one function: int to wide (sign or zero extension), wide to int (cut), to and from `float`/`double` (including `unsigned long long` above 2^63), to `bool` (`b = 4294967296LL` is 1), narrow types by `movsx`/`movzx`; it is used by assignment, initializers, arguments, `return`, `?:` branches and casts. `switch` on a `long long` compares 64 bits. `sizeof` is a `size_t` (unsigned, 64 bits). `T(x)` and `unsigned(x)` function-style casts. Constants are folded with their real type (a wrapped `unsigned int`, `1LL << 40`, comparisons and `&& ||` of constants too). Pointer comparison and `p - q` use 64 bits (before, only the low 32 bits were compared), `n + p` and `p += n` scale by the element size, an array index is sign-extended.
- **Float and double**: the miscompile from Round 18 (`float a[2]; a[0] = 1.5f; a[1] = 2.5f; return (int)(a[1] * 4.0f);` gave 5, now 10) came from the temporary-slot code taking the array type `float[2]` for a float value and saving `xmm0` instead of the address; the same mistake was in `x++`, `x += y` on a float lvalue and in binding a reference to an lvalue. Also fixed: reading a float or double global or static local (it loaded the bits into `eax`), `f++`, `f += 2`, `int n += 1.9`, `n = 2.9f`, `float f = 3`, `return 1;` in a float function, a `float` passed through `...` (now a `double`), `if (f)` and `!f` on a float, a float in a global initializer.
- **Float constants**: `static constexpr float Deg2Rad = PI / 180.0f;` (a constant of the class named later in the class body), `1.5f * 2`, `10 / 4.0f`, `-0.5f`, `(float)1 / 3`, `(int)7.9`, float comparisons and `&&` of constants are folded (two floats are computed as floats, anything with a double as a double); a `const`/`constexpr` global with a constant initial value is a constant in later initializers (`constexpr float kTwoPi = kPi * 2;`, `float t[kN]`); `std::numeric_limits<float>::infinity()` (a static member function whose body is `return <constant>;`) is a constant; `static constexpr long long` and `unsigned long long` members keep their type. A decimal float literal with a long integer part is no longer overflowed (`9223372036854775808.0`).
- **Default arguments** (`int add(int a, int b = 5)`, `float g(float w = 0.15f)`, a default given in a prototype and not repeated, in member functions, static members and constructors, `Base + 1` and other constants): the default is read where it is declared and copied into every call that leaves it out.
- **Braced `return {...}`**: `Rect make(...) { return {x, y, w, h}; }` for an aggregate (elements are real expressions now, not only constants, and elements not named are zero) and for a class with a constructor (`return {a, b};` calls it). A class with a destructor is refused with an error.
- **`const T&` from `?:`**: `const int& r = a > b ? a : b;`, `return first ? a : b;` in a function returning `const T&` or `T&`, `(a < b ? a : b) = 7;`.
- **`volatile T*` in `static_cast`/`reinterpret_cast`**, `const` as the first word of a cast (`(const char*)p`), **`using namespace x;`** (the functions of `x` can be called by bare name, for the whole file; types and variables of a namespace were already one flat table), a call named relative to the enclosing namespace (`sha256_detail::Blocks(...)` written in `okay::crypto`).
- **Found and fixed on the way** (each is in a test): `for (int i = 0; ...)` (a declaration in the `for` init, it did not parse at all), `int a = 1, b = 2, *p = &a;` (several declarators, locals and globals), an empty statement `;`, `(M::PI * 2.0f)` read as a cast to the class `M`, `sizeof x` without parentheses, a static member function calling another static member by its bare name, out-of-class `C::C(...) : m(1) {}` and `C::~C()` (also in a namespace), `= default` and `= delete` for constructors and `operator=`, `constexpr` in front of a constructor, a quoted `#include` in the main source file (looked up next to that file), a declared namespace function (`std::fabs`) that had no symbol of the decorated name. The `unsigned` `/` and `%` used signed division; `%` lost its result when a local lived in `rdx`.
- **`/O1` and `/O2` were wrong in several ways and are now covered by the tests** (the old tests ran the default level only): at `/O1` `x++` and `x += 2` on a variable kept in a register changed the stack slot, not the register; two parameters with disjoint uses shared one register; `bool` locals were put in registers; at `/O2` constant propagation turned `while (i < 3) { ...; i = i + 1; }` into an endless loop (a value known before a loop was used inside it), kept a stale value after `if (c) x++;`, replaced `x` in `x++`, ignored `&x`, ran an assignment inside `&&`/`?:` as if it always ran, and the dead-store pass deleted `x = f(); x = 3;` including the call; the inliner dropped the conversion of an argument and of the result (`long long wide(int x) { return x; }` inlined as an `int` sum), inlined a function with a reference parameter, a static local or a hidden temporary, and treated a function with a `__`-named temporary as having none. The propagation now only tracks `int` and `long long` locals, with their own width and signedness.

Measurement, second run (the same 36 files as Round 18, same command, our own headers in `tools/okayms/stdinc/`, 2026-11-18). Round 18: 5 of 36 compiled. After Round 19: **15 of 36 compile to an object** (ForceMode, PhysicsQuality, VsPinType, Mp3Tables, LauncherTips, Stance, ClockCursor, InputBuffer, Mathf, PhysicsMaterial, TooltipPlace, Blake2b, ChaCha20, Sha512, Util). A `.cpp` is compiled in place (so its quoted `#include "CtAnnotate.hpp"` is found next to it; in Round 18 the measurement copied it, which is why `Util.cpp` stopped there), a header through a one-line stub. Compiling is all this measures: none of these objects was linked or run, and "compiles" does not mean the code is right.

| File | Result | First error |
|---|---|---|
| `engine/include/okay/Physics/ForceMode.hpp` | compiles | |
| `engine/include/okay/Physics/PhysicsQuality.hpp` | compiles | |
| `engine/include/okay/VisualScript/VsPinType.hpp` | compiles | |
| `engine/src/Audio/Mp3Tables.hpp` | compiles | |
| `shared/LauncherTips.hpp` | compiles | |
| `engine/include/okay/Components/Stance.hpp` | compiles | (was "not an address") |
| `engine/include/okay/Core/ClockCursor.hpp` | compiles | (was "expected a type") |
| `engine/include/okay/Environment/Wind.hpp` | fails | `Vec2.hpp:15` `operator[]` |
| `engine/include/okay/Input/InputBuffer.hpp` | compiles | (was "expected ')'", a default argument) |
| `engine/include/okay/Math/Bounds.hpp` | fails | `Vec2.hpp:15` `operator[]` |
| `engine/include/okay/Math/ColorSpace.hpp` | fails | cannot open include string |
| `engine/include/okay/Math/Curves.hpp` | fails | cannot open include vector |
| `engine/include/okay/Math/Easing.hpp` | fails | cannot open include cctype |
| `engine/include/okay/Math/Mat3.hpp` | fails | `Vec2.hpp:15` `operator[]` |
| `engine/include/okay/Math/Mat4.hpp` | fails | `Vec2.hpp:15` `operator[]` |
| `engine/include/okay/Math/Mathf.hpp` | compiles | (was "global initializer must be a constant") |
| `engine/include/okay/Math/Quat.hpp` | fails | `Vec2.hpp:15` `operator[]` |
| `engine/include/okay/Math/Ray.hpp` | fails | `Vec2.hpp:15` `operator[]` |
| `engine/include/okay/Math/Rect.hpp` | fails | `Vec2.hpp:15` `operator[]` |
| `engine/include/okay/Math/Vec2.hpp` | fails | `Vec2.hpp:15` `operator[]` |
| `engine/include/okay/Math/Vec3.hpp` | fails | `Vec2.hpp:15` `operator[]` |
| `engine/include/okay/Math/VecInt.hpp` | fails | cannot open include functional |
| `engine/include/okay/Physics/PhysicsMaterial.hpp` | compiles | (was "not an address") |
| `engine/include/okay/Render/Color.hpp` | fails | cannot open include string |
| `engine/include/okay/UI/TooltipPlace.hpp` | compiles | (was "expected an expression") |
| `engine/src/Core/RandomExtras.cpp` | fails | cannot open include atomic |
| `engine/src/Crypto/Blake2b.cpp` | compiles | (was `Sha256.hpp:62` `volatile` cast) |
| `engine/src/Crypto/ChaCha20.cpp` | compiles | (was `Sha256.hpp:62` `volatile` cast) |
| `engine/src/Crypto/Sha512.cpp` | compiles | (was `Sha256.hpp:62` `volatile` cast) |
| `engine/src/Crypto/Util.cpp` | compiles | (was "cannot open include CtAnnotate.hpp": a quoted include in the main file is now looked up next to that file) |
| `engine/src/Input/GestureRecognizer.cpp` | fails | cannot open include string |
| `engine/src/Input/PlayerInput.cpp` | fails | cannot open include array |
| `engine/src/Optimize/LodGroup.cpp` | fails | cannot open include string |
| `engine/src/Optimize/PortalCells.cpp` | fails | cannot open include string |
| `engine/src/UI/FocusGraph.cpp` | fails | cannot open include vector |
| `shared/OkfDecode.hpp` | fails | cannot open include vector |

The remaining first errors, counted: the standard library (`<string>` 5, `<vector>` 3, `<functional>`, `<cctype>`, `<atomic>`, `<array>`: 12 files) and operator overloading (`operator[]` in `Vec2.hpp` line 15, reached by the 9 files that include it, and `operator+` and others after it).

Not done / unverified / found and not fixed:

- Operator overloading (`operator[]`, `operator+`, `operator==`, ...) is not there; it is now the first thing between the math headers (`Vec2` to `Mat4`, 9 files) and an object. The standard library (`<string>`, `<vector>`, `<functional>`, `<array>`, `<atomic>`, `<cctype>`) is not there.
- Lambdas, `auto`, range `for`, `static_assert` evaluation (it is dropped), overloading by parameter type (a function is found by name and argument count), default arguments in function templates, `return {...}` for a class with a destructor and for a non-class type, a field of a function result that is a struct (`f().x`: "not an address"), `switch` jump tables for `long long`, conversion of a `double` above 2^63 to `unsigned long long` (the conversion is the signed one), `long double`, and NaN in a float condition (`if (nan)` is false; the standard says true).
- `using namespace` is remembered for the whole file, not for the block it is written in; two namespaces that declare the same variable name still collide (one flat table for variables and types).
- `std::size_t` is `unsigned long long` in the stdinc headers, so a function from `<cstring>` and `<cstdlib>` declared there takes a 64-bit count; those headers are declarations only and their decorated names (`?memcpy@std@@...`) are not the C runtime's names, so a program that calls them is not linked by this tree (unchanged from Round 18; the test programs use their own functions).
- Only the host tool was run, on Linux with Wine; not run on Windows. The measurement compiles each file to an object only: none was linked or run. The comparison with g++ covers the 22 test programs of the two new test files and the small programs written while debugging; the rest of the language was checked only by the older tests (all still green, now with the changes above).


### Round 20: operator overloading, overloading by parameter type, `f().x`, `auto`, range `for`, lambdas, and the bugs they found

Tried with `sh tools/okayms/build.sh` on branch `claude/okayms-3` (head f4e82411 plus this round) on Linux with Wine, 2026-11-18. New test: `tests/test_round20.cpp` (8 test functions, 13 programs). Each program returns 0 when all its checks hold, or the number of the first check that failed; it is built by okay-cl at `/O0`, `/O1` and `/O2`, run under Wine, and built again by MinGW g++ and run under Wine; all four exit codes must be 0. Several of the programs also check decorated names in the object. Every new test failed before its code was written (first errors seen: "unknown function a" and "expected ')'" for operators, "wrong number of arguments to f" and duplicate symbols for overloads, "not an address" for `f().x`, "expected ';'" for `auto`, "expected an expression" for a lambda). okay-ms is a separate tool and off by default; nothing in the engine build calls it. Written from ISO/IEC 14882 ([over.oper], [over.match], [over.binary], [over.unary], [over.sub], [over.call], [over.inc], [class.conv.fct], [expr.ref], [class.temporary], [class.copy.elision], [dcl.spec.auto], [stmt.ranged], [expr.prim.lambda], [dcl.init.list], [stmt.return]) and the public MSVC name decoration rules; no code from other compilers.

How to try it by hand (host tool, POSIX; Wine only for running the result):

1. `sh tools/okayms/build.sh` builds `tools/okayms/build/okay-cl` and runs every test (about 4 minutes). It prints `tests N, checks passed M, failed 0` for `test_round20`.
2. One sample program, `tools/okayms/examples/ops_demo.cpp` (operators, overloads, `f().x`, `auto`, range for, lambdas): `tools/okayms/build/okay-cl tools/okayms/examples/ops_demo.cpp /Feops_demo.exe /I tools/okayms/stdinc`, then `wine ops_demo.exe; echo $?`. It must print `0`. (A number is the first failed check; open the file and count the `if` lines.) This was run on Linux with Wine, not on Windows.
3. Look at the decorated names: `tools/okayms/build/okay-cl /c tools/okayms/examples/ops_demo.cpp /Fo/tmp/o.obj /I tools/okayms/stdinc` then `strings /tmp/o.obj | grep '^?'`: you see `??HV@@QEBA?AUV@@AEBUV@@@Z` (`V::operator+`), `??BV@@QEBAHXZ` (`V::operator int`), `?size_of_thing@@YAHH@Z` and `?size_of_thing@@YAHN@Z` (the two overloads).
4. The engine measurement below: `printf '#include "okay/Math/Vec3.hpp"\n' > /tmp/w.cpp; tools/okayms/build/okay-cl /c /tmp/w.cpp /Fo/tmp/w.obj /I engine/include /I engine/src /I shared /I tools/okayms/stdinc`. No output and an object file means it compiled.

What changed:

- **Operator functions.** `operator` and its symbol are one identifier token before parsing (`operator+`, `operator[]`, `operator()`, `operator<<=`, ...), so a member or a free operator is declared like any function. A binary `a op b` (all arithmetic, bit, shift, comparison operators and the compound assignments), a unary `- ! ~ *`, prefix and postfix `++`/`--` (the postfix form takes the dummy `int`), `a[i]`, `a(args)` (also on a variable that is an object) and `a = b` (only when the class declares its own `operator=`; otherwise the old member copy) look for a member function of the left operand's class, then for a free function (any namespace) by the rules of overloading below. The result of a function that returns a reference is an lvalue (`a[1] = 9`, `a[0] += 2`). The operand types come from the parser's static types: where one is unknown the plain built-in operator is used. Decorated names follow the MSVC table: `??H` `+`, `??G` `-`, `??D` `*`, `??K` `/`, `??A` `[]`, `??R` `()`, `??8` `==`, `??9` `!=`, `??M` `<`, `??O` `>`, `??N` `<=`, `??P` `>=`, `??E` `++`, `??F` `--`, `??6` `<<`, `??5` `>>`, `??Y` `+=`, `??Z` `-=`, `??X` `*=`, `??_0` `/=`, `??4` `=`, `??B` conversion (and the rest of the table); members `??HV@@QEBA?AUV@@AEBUV@@@Z`, free functions `??H@YA?AUV@@AEBUV@@AEBUV@@@Z`, spelled out in full (no back references, as before). A `const T&` or `const T*` parameter now decorates as `AEB` / `PEB` (a new `Type::pconst` flag; it was `AEA` before).
- **Conversion functions** (`operator int() const`, `operator bool() const`, `explicit` is ignored): used by a C cast, a `static_cast`, a functional cast, `int x = obj;`, `x = obj;`, a call argument, `return obj;`, a condition (`if`, `while`, `for`, `do`, `?:`), `!obj`, `obj && x` and `obj + 1` when no operator function fits. The target picks the function with the same type first, any arithmetic one next.
- **Overloading by parameter type.** Members: two functions of one name and arity are different functions when the parameter types (as decorated) or the const-ness differ; the second gets the internal name `C::f/1~H` and its own decorated name (`?put@S@@QEAAHN@Z`); an out-of-class definition is matched by those types. Free functions: every free function is entered in a table by its full name; a second signature gets the internal name `f#<decorated parameters>`, and then every function of that name (the first one too) is decorated (`?f@@YAHH@Z`), as an operator function always is. The call picks the best match by rank (exact 0, promotion 1, conversion 2, user conversion 3; derived to base and 0 to pointer 2; const object prefers a const member): `f(3)`, `f(2.5)`, `f(char*)`, `f(P)`, `f(P*)`, `g(5LL)`, `g(1.5f)` pick different functions. A tie goes to the one declared first for free functions and the one declared last for members (the order the old lookup used). A name with one signature is left as it was (undecorated in the global namespace).
- **`f().x`.** A field of a struct returned by a call goes through a hidden local (made before the statement, destroyed after it if the class has a destructor), like the member call on a returned object already did. Found while testing: a call that is the right side of `q.in = mk()` (a big struct stored into a member, element or `*p`) had no destination ("large struct return has no destination"), now the callee writes there or the bytes are copied; `return mk(...);` in a function that returns a big struct passed the address of its own hidden slot instead of the pointer in it (wrong result since Round 13); an `int` or `double` argument converted to a `float` parameter was left in the integer register for the call (wrong for any `float` parameter after the first and for every one in a call that returns a big struct); `return c ? a : b;` of a big struct ("not an address").
- **Classes with destructors and `return`.** `return {a, b};` works for a class with a constructor and a destructor. `return x;` with a local `x` of the result class, `return C(args);` and `return {...}` hand the object itself to the caller: its destructor does not run in the function (it did, and the caller's copy was destroyed again: a count of live objects went wrong). A function with several `return`s of different locals destroys the others. A temporary `C{a, b}` (constructor or aggregate), `x = {a, b}`, `C x{a, b};`, a static local `static C w{a, b};`, a member default initializer `C m{a, b};` (arguments to the constructor), an inline static member `inline const V V::Zero {0, 0};`, and a member made from an object of its own class without a copy constructor (bytes copied) were added because the engine headers need them.
- **`auto`.** `auto x = e;`, `auto& r = x;`, `auto* p = &x;`, `const auto k = ...;`, `auto a = 1, b = 2;`: the initializer is read first and its type (the parser's static type, without a reference or an array) becomes the variable's type. Types of function names, assignments and calls through function pointers are known to it now. When the type cannot be worked out the error says so.
- **Range `for`.** `for (T x : arr)`, `for (auto& x : arr)`, `for (const P& e : arr)` over an array of known length (a local, a global or a member): an index runs over it, the variable is declared from the element in each pass (a reference for `&`), `break` and `continue` work. A range of a class with `begin()`/`end()`, a pointer, or an array captured by reference in a lambda is not supported.
- **Lambdas.** `[captures](params) -> ret { body }`. A lambda that captures nothing is a function (a closure class with a static `operator()`); its value is the function's address, a function pointer: it can be called (`f(3)`), passed where an `int (*)(int)` is wanted, called at once (`[]{...}()`), and given to a function template. A lambda that captures gets a closure class with a field per captured variable (`[x]` a copy, `[&x]` the address, `[=]` and `[&]` take every visible local the body names, `[=, &x]`), and its value is a hidden local of that class filled before the statement; `f(args)` is its `operator()`. The return type is the written one, else the type of the first `return e;` (the body is read once to find it), else void. A lambda in a loop, in a template argument, with a captured struct or a double, and `auto f = [..]{..}` all work. Not supported: `[this]`, init-captures (`[x = 1]`), generic lambdas (`auto` parameters), a lambda that captures an array by value, a lambda nested in a capturing lambda that uses its captures, converting a capturing lambda to a function pointer (not allowed by the standard either), `mutable` is read and ignored (captured copies can always be changed).
- **Smaller fixes made on the way:** an error from code generation now names the function (`(in Vec3::Normalized/0)`); `= default` copy assignment is still recognised with the new operator token.

Measurement, third run (the same 36 files, same command, our own headers in `tools/okayms/stdinc/`, 2026-11-18). Round 18: 5. Round 19: 15. After Round 20: **21 of 36 compile to an object** (the 15 from Round 19, plus Wind, Vec2, Vec3, Quat, Ray, Rect: the math headers that stopped at `operator[]`). Compiling is all this measures: none of these objects was linked or run, and "compiles" does not mean the code is right.

| File | Result | First error |
|---|---|---|
| `engine/include/okay/Physics/ForceMode.hpp` | compiles |  |
| `engine/include/okay/Physics/PhysicsQuality.hpp` | compiles |  |
| `engine/include/okay/VisualScript/VsPinType.hpp` | compiles |  |
| `engine/src/Audio/Mp3Tables.hpp` | compiles |  |
| `shared/LauncherTips.hpp` | compiles |  |
| `engine/include/okay/Components/Stance.hpp` | compiles |  |
| `engine/include/okay/Core/ClockCursor.hpp` | compiles |  |
| `engine/include/okay/Environment/Wind.hpp` | compiles |  (new in Round 20) |
| `engine/include/okay/Input/InputBuffer.hpp` | compiles |  |
| `engine/include/okay/Math/Bounds.hpp` | fails | okay/Math/Mat4.hpp:11: an aggregate default member initializer is not supported |
| `engine/include/okay/Math/ColorSpace.hpp` | fails | cannot open include string |
| `engine/include/okay/Math/Curves.hpp` | fails | cannot open include vector |
| `engine/include/okay/Math/Easing.hpp` | fails | cannot open include cctype |
| `engine/include/okay/Math/Mat3.hpp` | fails | okay/Math/Mat4.hpp:11: an aggregate default member initializer is not supported |
| `engine/include/okay/Math/Mat4.hpp` | fails | okay/Math/Mat4.hpp:11: an aggregate default member initializer is not supported |
| `engine/include/okay/Math/Mathf.hpp` | compiles |  |
| `engine/include/okay/Math/Quat.hpp` | compiles |  (new in Round 20) |
| `engine/include/okay/Math/Ray.hpp` | compiles |  (new in Round 20) |
| `engine/include/okay/Math/Rect.hpp` | compiles |  (new in Round 20) |
| `engine/include/okay/Math/Vec2.hpp` | compiles |  (new in Round 20) |
| `engine/include/okay/Math/Vec3.hpp` | compiles |  (new in Round 20) |
| `engine/include/okay/Math/VecInt.hpp` | fails | cannot open include functional |
| `engine/include/okay/Physics/PhysicsMaterial.hpp` | compiles |  |
| `engine/include/okay/Render/Color.hpp` | fails | cannot open include string |
| `engine/include/okay/UI/TooltipPlace.hpp` | compiles |  |
| `engine/src/Core/RandomExtras.cpp` | fails | cannot open include atomic |
| `engine/src/Crypto/Blake2b.cpp` | compiles |  |
| `engine/src/Crypto/ChaCha20.cpp` | compiles |  |
| `engine/src/Crypto/Sha512.cpp` | compiles |  |
| `engine/src/Crypto/Util.cpp` | compiles |  |
| `engine/src/Input/GestureRecognizer.cpp` | fails | cannot open include string |
| `engine/src/Input/PlayerInput.cpp` | fails | cannot open include array |
| `engine/src/Optimize/LodGroup.cpp` | fails | cannot open include string |
| `engine/src/Optimize/PortalCells.cpp` | fails | cannot open include string |
| `engine/src/UI/FocusGraph.cpp` | fails | cannot open include vector |
| `shared/OkfDecode.hpp` | fails | cannot open include vector |

The remaining first errors, counted: the standard library (`<string>` 5 files, `<vector>` 4 incl. headers, `<functional>`, `<cctype>`, `<atomic>`, `<array>`: 12 files) and one language gap (3 files: `Mat3`, `Mat4`, `Bounds` stop at `Mat4.hpp:11`, `float m[16] = {1, 0, ...};` an array member with a braced default initializer: "an aggregate default member initializer is not supported").

Not done / unverified / found and not fixed (Round 20):

- Aggregate default member initializers (`float m[16] = {...};`, `Vec3 v = {1, 2, 3};` for a member that is an aggregate or an array) are still refused. It is the next thing between `Mat3`, `Mat4`, `Bounds` and an object.
- The standard library (`<string>`, `<vector>`, `<functional>`, `<array>`, `<atomic>`, `<cctype>`) is still not there. A lambda cannot yet be stored in a `std::function`.
- Overload choice is a simple ranking, not the full rules of [over.match]: no ambiguity error, no template-vs-function rule, no user-defined conversion by a converting constructor (`V v = 5;` through `V(int)` as an argument conversion), no overloading on a function's reference-ness of the object (`&` / `&&`). A call whose argument type the parser cannot work out counts as an acceptable match.
- Operators: no `operator->`, `operator->*`, `operator,`, `operator new` / `delete`, user-defined literals; `&&` and `||` are never overloaded; no free `operator[]` (not allowed); an operator on a class returned by a conditional `?:` is not looked at. The comparison `a == b` with a class on the left and an unrelated type on the right and no matching operator falls to the built-in operator and may not give an error where g++ does. Out-of-class definitions of conversion functions (`C::operator int()`) are not read.
- Class temporaries with a destructor made inside a loop condition still hit the old limit ("a reference bound to a temporary in a loop condition is not supported"), so `while (a + b == c)` with objects returned by value is refused.
- `return x;` hands over the local itself: where the result class has a user-written copy constructor with side effects other than counting, g++ (which may or may not elide) can show different output; the final count of live objects agrees.
- A lambda that names a captured variable is found by scanning the body's tokens: a name that is also a local declared inside the lambda is captured too (harmless, but the capture list is larger than needed).
- The `auto` deduction depends on the parser's static type: an initializer made of an expression kind it does not know (for example some template instance results, a `new` expression) gives "cannot work out the type of the auto variable".
- Run on Linux with Wine only; not run on Windows. The measurement compiles to an object only. The comparison with g++ covers the 13 programs of `test_round20.cpp` and the small programs written while debugging; the rest of the language was checked only by the older tests.


### Round 21: aggregate member initializers, our own standard headers (`<array>`, `<cctype>`, `<atomic>`, `<functional>`, `<vector>`, `<string>`), and the language gaps they showed

Tried with `sh tools/okayms/build.sh` on branch `claude/okayms-3` (head 57750d54 plus this round) on Linux with Wine, 2026-11-18. New test: `tests/test_round21.cpp` (11 test functions: 14 programs, each at three levels, plus the example program; 211 checks). Each program returns 0 when all of its checks hold, or the number of the first check that failed; it is built by okay-cl at `/O0`, `/O1` and `/O2`, linked, run under Wine, and built again by MinGW g++ (with g++'s own library) and run under Wine; all four exit codes must be 0. Each test was written before its code (first errors seen: "an aggregate default member initializer is not supported", "expected '>'" for `size_t N`, "cannot open include array", "expected a type" for `new (p) T`, and a wrong exit code for the others). okay-ms is a separate tool and off by default; nothing in the engine build calls it.

**Where each new file comes from (rule 19a).** The six headers in `tools/okayms/stdinc/` were written from ISO/IEC 14882 only: `<array>` [array], `<cctype>` [cctype.syn] with the character classes of ISO/IEC 9899 7.4 (ASCII, "C" locale), `<atomic>` [atomics], `<functional>` [function.objects], `<vector>` [vector], `<string>` [string.classes], and `<new>` [expr.new]. Nothing was copied, ported or followed from libstdc++, libc++, the MSVC STL or any other implementation; the data structures are the plainest ones the standard text allows (a pointer, a size and a capacity). They are our own minimal versions, not the whole of the standard types.

How to try it by hand (host tool, POSIX; Wine only for running the result):

1. `sh tools/okayms/build.sh` builds `tools/okayms/build/okay-cl` and runs every test (about 5 minutes). `test_round21` must print `tests 11, checks passed 211, failed 0`.
2. The example program: real engine headers (`okay/Math/Vec3.hpp`, `okay/Math/Mat4.hpp`) plus our own `<vector>`, `<string>`, `<array>`, `<atomic>`: `tools/okayms/build/okay-cl tools/okayms/examples/engine_math.cpp /Feengine_math.exe /I engine/include /I engine/src /I shared /I tools/okayms/stdinc`, then `wine engine_math.exe; echo $?`. It must print `0`; a number is the first failed check (count the `CK(` lines). It is the same text that MinGW g++ builds with its own library (`x86_64-w64-mingw32-g++ -static -I engine/include tools/okayms/examples/engine_math.cpp`), and that also exits 0. The program brings its own `malloc` and square root because no C runtime is linked.
3. The engine measurement, one file: `printf '#include "okay/Math/Mat4.hpp"\n' > /tmp/w.cpp; tools/okayms/build/okay-cl /c /tmp/w.cpp /Fo/tmp/w.obj /I engine/include /I engine/src /I shared /I tools/okayms/stdinc`. No output and an object file means it compiled (before this round: "an aggregate default member initializer is not supported").

What changed in the compiler:

- **Aggregate member default initializers.** `float m[16] = {1, 0, ...};`, `int n[3] = {5};`, `P p = {3, 4};`, `P arr[2] = {{1, 2}, {5, 6}};`, `T x[4]{};`: the list is read again in the constructor and stored element by element, the rest zero ([dcl.init.aggr]). A member that has a constructor still gets the list as constructor arguments. This unblocked `Mat3`, `Mat4`, `Bounds`.
- **Brace elision.** `std::array<int, 4> a = {1, 2, 3, 4};` (a struct whose one member is an array) and flat lists for nested aggregates are written out with all braces before they are stored (locals, statics, globals, members).
- **Class templates with a non-type parameter of a named type** (`template <typename T, size_t N>`; it stopped at the type name). A plain struct as a template argument (`array<P, 2>`) is read as a type.
- **Range `for` over a class with `begin()` and `end()` that return pointers** (our `vector`, `string`, `array`), over a temporary (`for (char c : std::string("xyz"))`; the temporary is made before the loop and destroyed after it), and over a braced list (`for (const char* d : {"inout", "in", "out"})`).
- **Placement new** `new (p) T(args)` (a class, a scalar, a plain struct copy), **explicit destructor calls** `p->~T()` and `o.~T()` (nothing for a scalar), `T()` for a scalar (zero) and for a class without constructors, `int x(5);`, `T x = "abc";` through a converting constructor (it was a raw copy of the pointer), `T x(other)` and `new (p) T(other)` for a class that has constructors but no copy constructor (members copied; before, some other one-argument constructor could be picked).
- **Overload choice for constructors** by the ranks of Round 20 (`string(3, 'z')` took `string(const char*, size_t)`); two constructors of one arity no longer share an internal name (the second one was never called).
- **Conditional `c ? a : b` of class objects** where a branch is a call or a temporary (it said "not an address").
- **Qualified class names** `std::string("abc")`, `(Vec3::Dot(a, b) == 1)` after a typedef (read as a cast), `T&&` read as `T&` (no move semantics), `static const char* k[] = {"a", "b"};` in a function, a global function whose parameter is of a named type (`std::string ToName(Ease e)` was read as a global object).
- **Bugs found by the new tests and fixed (all older than this round):**
  - A function with **more than four parameters never read the fifth and later ones** from the stack (the callee used uninitialised slots); a `float`/`double`/struct parameter after the fourth was also wrong. Found by an engine function with nine `float` arguments.
  - A **`const float&` parameter** was read as a `float` in an XMM register (it is a pointer in an integer register).
  - **Array members and static arrays used as values** (`return elems_;`, `int* p = s.a;`, `static int a[4]; return a;`) loaded the first element instead of the address.
  - **Float comparisons with a NaN**: `==`, `<`, `<=` were true and `!=` was false when a side was a NaN (the parity flag was not looked at). `if (nan)` as a condition is still wrong (see below).
  - A big struct assigned from a call into a **reference parameter** (`rotation = Quat::From...(...)`) had no destination; an expression nested more than 8 deep ("expression is too deep") now has 16 temporary slots.
  - `std::isnan`, `std::isfinite`, `std::isinf` were missing from `<cmath>` (written here from the IEEE-754 binary32 definition).
- **Intrinsics:** none. `<atomic>` is plain reads and writes (single thread), see below.

The standard headers (what is there, and what is not):

- `<array>`: `array<T, N>` with `[]`, `at`, `front`, `back`, `data`, `begin`, `end`, `size`, `empty`, `fill`. Aggregate initialisation works.
- `<cctype>`: `isdigit isupper islower isalpha isalnum isxdigit isspace isblank iscntrl isprint isgraph ispunct tolower toupper` (ASCII, "C" locale).
- `<atomic>`: `atomic<T>` (integers, `bool`), `atomic<float>`, `atomic<double>`, `atomic<T*>`: `load store exchange compare_exchange_strong/weak fetch_add/sub/and/or/xor`, the operators, `memory_order` (ignored). **Not safe between threads**: every operation is a plain read and write, because the compiler has no lock-prefixed intrinsics yet. It is correct for one thread. A multi-thread build needs real atomic instructions first.
- `<functional>`: `hash` for the integer types, `less greater equal_to plus minus multiplies divides negate` and friends. **`std::function` is not there** (it needs a type-erased call through a function type).
- `<vector>`: `vector<T>` with constructors (empty, count, count and value, copy), `=`, `size capacity empty [] at front back data begin end reserve push_back pop_back clear resize assign insert erase swap`; storage from the global `malloc`/`free`; elements made with placement new. No allocator, no iterator classes (iterators are pointers), no initializer list, no `emplace_back`, no move constructor.
- `<string>`: `string` with constructors (empty, `const char*`, pointer and length, count and char, copy), `=`, `+=`, `+`, `==`, `!=`, `<`, `>`, `<=`, `>=`, `[] at front back c_str data size length empty begin end reserve clear resize push_back pop_back append assign substr find rfind find_first_of find_first_not_of find_last_not_of erase insert compare swap`, `npos`, `to_string` for the integer types. No `to_string(double)`, no `string_view`, no `wstring`, no `getline`, no streams.
- `<cstdlib>` now declares the global `malloc` and `free` (what `new` and `delete` call) and the C functions `strtol strtoul strtof strtod atoi atof` in `std` as declarations only. `<new>` is a comment (placement new is in the compiler).

Measurement, fourth run (the same 36 files, same command, our own headers in `tools/okayms/stdinc/`, 2026-11-18). Round 18: 5. Round 19: 15. Round 20: 21. After Round 21: **28 of 36 compile to an object** (new: Bounds, Mat3, Mat4, VecInt, Curves, Easing, Color). Compiling is all this measures for these 36. In addition, one program that uses the real `Vec3` and `Mat4` headers with our `vector`, `string`, `array` and `atomic` was **linked and run** (`examples/engine_math.cpp`, exit 0 at `/O0`, `/O1`, `/O2`, and the same text built by g++ also exits 0); it supplies its own `malloc` and `std::sqrt`/`sin`/`cos`/`atan2` and the other `<cmath>` functions, because a function declared in `namespace std` has a decorated name that is not the C runtime's.

| File | Result | First error |
|---|---|---|
| `engine/include/okay/Physics/ForceMode.hpp` | compiles |  |
| `engine/include/okay/Physics/PhysicsQuality.hpp` | compiles |  |
| `engine/include/okay/VisualScript/VsPinType.hpp` | compiles |  |
| `engine/src/Audio/Mp3Tables.hpp` | compiles |  |
| `shared/LauncherTips.hpp` | compiles |  |
| `engine/include/okay/Components/Stance.hpp` | compiles |  |
| `engine/include/okay/Core/ClockCursor.hpp` | compiles |  |
| `engine/include/okay/Environment/Wind.hpp` | compiles |  |
| `engine/include/okay/Input/InputBuffer.hpp` | compiles |  |
| `engine/include/okay/Math/Bounds.hpp` | compiles |  (new in Round 21) |
| `engine/include/okay/Math/ColorSpace.hpp` | fails | unknown function Color (in ColorSpace::ParseHex/2) |
| `engine/include/okay/Math/Curves.hpp` | compiles |  (new in Round 21) |
| `engine/include/okay/Math/Easing.hpp` | compiles |  (new in Round 21) |
| `engine/include/okay/Math/Mat3.hpp` | compiles |  (new in Round 21) |
| `engine/include/okay/Math/Mat4.hpp` | compiles |  (new in Round 21) |
| `engine/include/okay/Math/Mathf.hpp` | compiles |  |
| `engine/include/okay/Math/Quat.hpp` | compiles |  |
| `engine/include/okay/Math/Ray.hpp` | compiles |  |
| `engine/include/okay/Math/Rect.hpp` | compiles |  |
| `engine/include/okay/Math/Vec2.hpp` | compiles |  |
| `engine/include/okay/Math/Vec3.hpp` | compiles |  |
| `engine/include/okay/Math/VecInt.hpp` | compiles |  (new in Round 21) |
| `engine/include/okay/Physics/PhysicsMaterial.hpp` | compiles |  |
| `engine/include/okay/Render/Color.hpp` | compiles |  (new in Round 21) |
| `engine/include/okay/UI/TooltipPlace.hpp` | compiles |  |
| `engine/src/Core/RandomExtras.cpp` | fails | cannot open include chrono |
| `engine/src/Crypto/Blake2b.cpp` | compiles |  |
| `engine/src/Crypto/ChaCha20.cpp` | compiles |  |
| `engine/src/Crypto/Sha512.cpp` | compiles |  |
| `engine/src/Crypto/Util.cpp` | compiles |  |
| `engine/src/Input/GestureRecognizer.cpp` | fails | engine/src/Input/GestureRecognizer.cpp:45: expected a name |
| `engine/src/Input/PlayerInput.cpp` | fails | cannot open include deque |
| `engine/src/Optimize/LodGroup.cpp` | fails | cannot open include cwctype |
| `engine/src/Optimize/PortalCells.cpp` | fails | engine/src/Optimize/PortalCells.cpp:67: no matching constructor in vector<UVisPlane@@> |
| `engine/src/UI/FocusGraph.cpp` | fails | okay/UI/FocusGraph.hpp:109: expected an expression |
| `shared/OkfDecode.hpp` | fails | assignment to const p (in okf::Unpack) |

The remaining first errors, counted: three missing headers (`<chrono>`, `<deque>`, `<cwctype>`: 3 files), and language gaps (5 files): a default argument written `= {}` for a class reference (`FocusGraph.hpp:109`), a definition whose return type is a nested class named through its outer class (`GestureRecognizer::Finger* GestureRecognizer::Find(...)`), a constructor error inside a range `for` over a member `std::vector<VisPlane>` in `PortalCells.cpp`, `Color(...)` in a member function of `ColorSpace` ("unknown function Color"; cause not looked into), and `OkfDecode.hpp:115` ("assignment to const p"; cause not looked into, unconfirmed).

Not done / unverified / found and not fixed (Round 21):

- Not run on Windows; Linux with Wine only. 28 of the 36 files were only compiled to an object, not linked or run; one program (the example) was linked and run.
- `if (nan)` as a plain float condition is still false (the standard says true); only comparisons were fixed.
- `<atomic>` is not thread safe; `std::function`, `std::deque`, `std::chrono`, `<cwctype>`, `std::unique_ptr` (`<memory>`), `std::map`, `std::unordered_map`, streams are not there. `vector` and `string` do not move (a copy is made when the storage grows), do not check an index or an allocation failure, and have no exceptions.
- `typedef T& reference;` (a typedef of a reference type) is not read, so the headers do not declare `reference` / `const_reference`.
- `T&&` is read as `T&`: there is no move semantics and no perfect forwarding; a call with a temporary binds it to a hidden local.
- The decorated name of a `const T&` return type is written `AEAM`, not `AEBM` (for example `array<float, 3>::operator[]() const`): it does not matter inside this tree, it would for a link with MSVC objects.
- Functions of the C runtime that a header declares inside `namespace std` (`<cmath>`, `<cstring>`, `<cstdio>`, `<cstdlib>`) are still not found by the linker; `extern "C"` inside a namespace is not read. The example defines them itself.
- Hoisting: a temporary made inside a conditional `c ? f(x) : g(y)`, `a && f(b)` or a loop condition is made before the statement, not where the expression is evaluated (a conditional of class objects now evaluates only the chosen branch, but a placement `new` or a temporary elsewhere in the statement is still made unconditionally).
- `new (p) T[n]` (placement array new), `delete` as an expression, `new int(5)` without a place, overloading `operator new`, and `using okay::Vec3;` inside a function are not supported.
- A `for` over a class whose `begin()` returns a class iterator (not a pointer) is refused.
- The new headers are minimal: what the engine files need is there, much of the standard is not (see the lists above).


### Round 22: the eight failures, rvalue references, our own `<algorithm>` `<deque>` `<chrono>` `<set>` `<memory>` and more, and the first real engine files linked and run

Tried with `sh tools/okayms/build.sh` on branch `claude/okayms-3` (head 98e8a937 plus this round) on Linux with Wine, 2026-11-18. The whole run finishes with exit 0: **182 test functions, 2113 checks, 0 failed**. Two new test files: `tests/test_round22.cpp` (10 test functions, 168 checks: 12 programs, each built by okay-cl at `/O0`, `/O1` and `/O2`, run under Wine, and built by MinGW g++ and run, all four exit codes 0) and `tests/test_engine_link.cpp` (2 programs, 52 checks, see "Link and run" below). Each language change was written as a failing program first. okay-ms is a separate tool and off by default; nothing in the engine build calls it.

**Where each new file comes from (rule 19a).** The headers added to `tools/okayms/stdinc/` were written from ISO/IEC 14882 and ISO/IEC 9899 only: `<algorithm>` (find, find_if, count, any_of and friends, remove, remove_if, reverse, fill, copy, min/max_element, lower/upper_bound, binary_search, sort as a heap sort, stable_sort as an insertion sort) from [alg.*]; `<deque>` from [deque]; `<chrono>` from [time.duration], [time.point], [time.clock]; `<cwctype>` from [cwctype.syn] and 9899 7.30; `<climits>` from 9899 5.2.4.2.1; `<set>` and `<unordered_set>` from [set] and [unord.set]; `<memory>` (`unique_ptr`, `make_unique` only) from [unique.ptr]; `<numeric>` from [accumulate], [iota], [inner.product], [partial.sum]; `<utility>` (`move`, `forward`, `swap`, `pair`, `make_pair`) from [forward], [utility.swap], [pairs]; `<initializer_list>` from [support.initlist]; `<cmath>` is now **defined**, not only declared, from the mathematical definitions (Newton for the square root, Taylor series after range reduction for sine and cosine, halving of the angle for the arc tangent, `2^k * series` for the exponential, `atanh` series for the logarithm). The runtime additions (`memmove`, `memcmp`, `strncmp`, `strchr`, `calloc`, `strtoul`, `strtol`, `strtod`, `strtof`, `atof`, `printf` length modifiers `l ll z j t h` and `%X %i`) were written from ISO/IEC 9899 7.21, 7.22 and 7.21.6.1. Nothing was copied, ported or followed from libstdc++, libc++, the MSVC STL, glibc or any other implementation. They are minimal versions of the standard types; the differences from the standard are listed below.

How to try it by hand (host tool, POSIX; Wine only to run the result):

1. `sh tools/okayms/build.sh` builds `tools/okayms/build/okay-cl` and runs every test (about 3 minutes). `test_round22` must print `tests 10, checks passed 168, failed 0` and `test_engine_link` must print `tests 2, checks passed 52, failed 0`.
2. Compile one of the files that failed before: `printf '#include "okay/Math/ColorSpace.hpp"\n' > /tmp/w.cpp; tools/okayms/build/okay-cl /c /tmp/w.cpp /Fo/tmp/w.obj /I engine/include /I engine/src /I shared /I tools/okayms/stdinc`. No output and an object file means it compiled. The same for `shared/OkfDecode.hpp`, and for the `.cpp` files `engine/src/Input/GestureRecognizer.cpp`, `engine/src/Optimize/PortalCells.cpp`, `engine/src/UI/FocusGraph.cpp` (copy the file next to a temporary name if it includes a header by a quoted name).
3. Link and run a real engine file. After step 1 the test leaves `/tmp/okayms-el/okayrt.lib` and `/tmp/okayms-el/kernel32.lib`. Then: `for s in engine/src/Crypto/Sha512.cpp engine/src/Crypto/Blake2b.cpp engine/src/Crypto/ChaCha20.cpp engine/src/Crypto/Util.cpp tools/okayms/linktests/link_crypto.cpp; do tools/okayms/build/okay-cl /c /O2 /Fo/tmp/$(basename $s .cpp).obj $s /I tools/okayms/linktests /I engine/include /I engine/src /I shared /I tests /I tools/okayms/stdinc; done`, then `tools/okayms/build/okay-cl /ENTRY:mainCRTStartup /Fe/tmp/crypto.exe /tmp/Sha512.obj /tmp/Blake2b.obj /tmp/ChaCha20.obj /tmp/Util.obj /tmp/link_crypto.obj /tmp/okayms-el/okayrt.lib /tmp/okayms-el/kernel32.lib` and `wine /tmp/crypto.exe`. It prints the three digests and `23 checks, 0 failures`; the exit code is 0. The same program built by `x86_64-w64-mingw32-g++ -static` prints the same text.

**The eight failures, one by one (cause found, then fixed, with a test):**

1. `<chrono>`, 2. `<deque>`, 3. `<cwctype>`: own headers (above). Each file then stopped at the next missing header (`<climits>`, `<set>`, `<unordered_set>`, `<filesystem>`); `<climits>`, `<set>`, `<unordered_set>`, `<memory>`, `<numeric>` were added. `<filesystem>` is not done (see the table).
4. **`= {}` default argument for a class reference** (`FocusGraph.hpp:109`, `const FocusOptions& o = {}`): a default argument that is a braced list, or a class name followed by `(`, is kept as a position in the token stream and read again at each call, where the temporary can be made. (Reading `Color(1, 1, 1, 1)` at the place of the declaration also gave the "unknown function Color" error in `ColorSpace`: same cause, same fix.)
5. **Definition with return type `Outer::Nested*`** (`GestureRecognizer::Finger* GestureRecognizer::Find`): nested classes are kept under their plain name; `Outer::Nested` as a type now finds one.
6. **Range `for` over a member vector, constructor error** (`PortalCells.cpp`): the real cause was `r.planes = {make(1, r0), ...}`: a braced list assigned to a `vector` has no `std::initializer_list` to go through. Now there is one: `std::initializer_list<T>` (a pointer and a count), built by the compiler from a hidden array in the caller's frame, for a parameter, an assignment, `T x = {..}`, `T x{..}` and `T{..}`; `vector` has the constructor and `operator=` for it. A nested list for an aggregate (`queue.push_back({cell, -1, 0, region, {cell}})`) and `Trap{group, restore}` for a struct that has default member initializers (it was read as a constructor call) were needed too. (`FocusGraph.cpp` also needed: a range `for` over a call result `for (n : f())`, and a `begin()` that returns a pointer to a pointer.)
7. **`Color(...)` in `ColorSpace`**: see 4.
8. **OkfDecode "assignment to const p"** (not the lambda): a local `const T* p` was treated as a const variable, so `p += 2` and `p++` were refused; the const belongs to what `p` points at. The same for a global.

**Language changes (all with a test):** real **rvalue references**: `T&&` is its own kind of reference (`Type::rref`), a `T&&` parameter binds only to an rvalue (a call returning a class by value, `std::move(x)`, a hidden temporary), the choice between `C(const C&)` and `C(C&&)` and between `operator=(const C&)` and `operator=(C&&)` uses it, the decorated name is `$$QEA...`, `C x = std::move(y)` and a by-value argument `f(std::move(y))` call the move constructor. `std::move` is `T&& move(T& x)`. **`typedef T& reference;`**, **`new int(5)`**, **`using ns::name;`** in a function (read as a `using namespace ns;` directive for `ns`, which is a little wider than the standard), **NaN in a condition** (`if (x)` for a float or double NaN is true now), **`if (T* p = f())`** (a declaration in an `if` condition), **`operator->`**, **`extern "C"`** (block and single declaration, also inside `namespace std`), **`friend class X;`** (skipped), **`::name`**, **`__LINE__`**, **`int* p(&v);`**, **`T(x)` and `T()` for a pointer type `T`**, an expression statement that discards a class returned in memory (`s.insert(x);`), `Box<int>(args)` as an expression, class-name lookup of `Easing::Linear(x)` through `using namespace okay;`, deferring the bodies of a nested class until the enclosing class is complete.

**Bugs found by linking and running real engine code (all older than this round, all with a test in `test_round22.cpp`):**

- **A parameter written `T x[16]` was a copy of the array, not a pointer** (`ChaCha20.cpp`: `Rounds(std::uint32_t x[16])` changed a private copy, so every ChaCha20 keystream was wrong). Found by the first run of `link_crypto`. Only `T x[]` was a pointer before.
- **The same variable name in two namespaces** was one variable (`Sha256.hpp` has `kK[64]` of 32-bit words, `Sha512.cpp` has `kK[80]` of 64-bit words: Sha512 read the SHA-256 table). The later one now gets its own internal name and what is read after it means that one. Not a real namespace scope: a name read after the second declaration always means the second one.
- **The preprocessor replaced macro parameters one after the other**, so `CHECK_NEAR(Vec3::Dot(a, b), 32.0f, 1e-4)` (parameters named `a` and `b`) became `Vec3::Dot(a, 32.0f)`. Parameters are now replaced in one pass.
- **`/O2`: a branch with a constant condition left the scope numbers out of step** ("unknown name i" in a function that had `if (e & 1)` before a `for`), and **inlining copied the callee's scope blocks** into the caller (the same error). Found by `sqrt` in the new `<cmath>`.
- **Class-template members read in the caller's names** (`To::scale_den()` called inside a function template whose own parameter was also called `Period`: the constant-folding of `return expr;` read the class's body with the caller's template arguments). The instance's own arguments are bound now.
- **A member initializer such as `p(new int(k))`** lost the statements `new` needs before it (the allocation and the store), so `p` was uninitialised. **`(To::name())`** after a cast was read as a type when `To` was a template parameter bound to a class. **`T x(c);` for a pointer type** was read as a function declaration. **`Foo(std::vector<int>& n)`** as the parameter list of a function returning a class was read as constructor arguments of a global object. **A function template called with the name of a local variable** (`count < 5` with `std::count` declared) was read as a template call.
- **Linking several files:** `const` objects at namespace scope, anything in an unnamed namespace, `inline` functions, members written inside their class, template instances and virtual tables now have internal symbols, so two files that include the same header link (before, `duplicate symbol`). A member function that is declared in its class and defined in another file can be called (an undefined symbol with its decorated name is made).

**Link and run: the first real engine code.** `tools/okayms/linktests/` holds `mini.hpp` (the `CHECK`, `CHECK_NEAR`, `RUN_SUITE` macros of `tests/test_framework.hpp` on `<cstdio>` only) and two programs. `link_crypto.cpp` is built with four real engine files, `engine/src/Crypto/Sha512.cpp`, `Blake2b.cpp`, `ChaCha20.cpp` and `Util.cpp`, and runs the engine's own known-answer vectors (`tests/crypto_rfc_vectors.hpp`: ChaCha20 blocks of RFC 8439, SHA-512 of FIPS 180-4 messages, BLAKE2b of RFC 7693) through one-shot and streaming calls, plus `ConstantTimeEqual` and `MemZero`; the checks follow `tests/test_crypto.cpp` for these four files. `link_math.cpp` runs the checks of `tests/test_math.cpp` and `tests/test_easing.cpp` (Vec3, Quat, Mat4, Mathf, all 18 first easings) and a few of `tests/test_core_math_extra.cpp` against the real headers. `tests/test_engine_link.cpp` builds the C runtime (`tools/okayms/runtime/*.c`) into `okayrt.lib`, writes a kernel32 import library, compiles every file at `/O0`, `/O1`, `/O2`, links, runs under Wine, builds the same sources with MinGW g++ and runs that too, and requires: exit code 0 for all four builds, and the printed text (the digests, and numbers in millionths) **byte for byte the same** for all four.

Result: **both programs pass at `/O0`, `/O1` and `/O2` with the same output as g++.** These are the first engine `.cpp` files that okay-ms compiled, linked and ran (the Round 21 example was a program written for the purpose). Four engine `.cpp` files plus the math headers are not the engine: nothing else was linked, and the other test programs of `tests/` include `<Okay.hpp>`, `<iostream>` and more.

Measurement, fifth run (the same 36 files, same command, 2026-11-18). After Round 21: 28. After Round 22: **33 of 36 compile to an object** (new: ColorSpace, GestureRecognizer, PortalCells, FocusGraph, OkfDecode; `engine/src/Crypto/Util.cpp` was compiled from its own folder, because it includes `"CtAnnotate.hpp"` from its neighbour; the other `.cpp` files were copied to a temporary name). Of the eight files that failed in the fourth run, five compile; the three that do not now stop at a different place than before:

| File | Result | First error |
|---|---|---|
| `engine/include/okay/Components/Stance.hpp` | compiles |  |
| `engine/include/okay/Core/ClockCursor.hpp` | compiles |  |
| `engine/include/okay/Environment/Wind.hpp` | compiles |  |
| `engine/include/okay/Input/InputBuffer.hpp` | compiles |  |
| `engine/include/okay/Math/Bounds.hpp` | compiles |  |
| `engine/include/okay/Math/ColorSpace.hpp` | compiles |  |
| `engine/include/okay/Math/Curves.hpp` | compiles |  |
| `engine/include/okay/Math/Easing.hpp` | compiles |  |
| `engine/include/okay/Math/Mat3.hpp` | compiles |  |
| `engine/include/okay/Math/Mat4.hpp` | compiles |  |
| `engine/include/okay/Math/Mathf.hpp` | compiles |  |
| `engine/include/okay/Math/Quat.hpp` | compiles |  |
| `engine/include/okay/Math/Ray.hpp` | compiles |  |
| `engine/include/okay/Math/Rect.hpp` | compiles |  |
| `engine/include/okay/Math/Vec2.hpp` | compiles |  |
| `engine/include/okay/Math/Vec3.hpp` | compiles |  |
| `engine/include/okay/Math/VecInt.hpp` | compiles |  |
| `engine/include/okay/Physics/ForceMode.hpp` | compiles |  |
| `engine/include/okay/Physics/PhysicsMaterial.hpp` | compiles |  |
| `engine/include/okay/Physics/PhysicsQuality.hpp` | compiles |  |
| `engine/include/okay/Render/Color.hpp` | compiles |  |
| `engine/include/okay/UI/TooltipPlace.hpp` | compiles |  |
| `engine/include/okay/VisualScript/VsPinType.hpp` | compiles |  |
| `engine/src/Audio/Mp3Tables.hpp` | compiles |  |
| `engine/src/Core/RandomExtras.cpp` | fails | okay/Core/Random.hpp:214: no member function GateLock |
| `engine/src/Crypto/Blake2b.cpp` | compiles |  |
| `engine/src/Crypto/ChaCha20.cpp` | compiles |  |
| `engine/src/Crypto/Sha512.cpp` | compiles |  |
| `engine/src/Crypto/Util.cpp` | compiles |  |
| `engine/src/Input/GestureRecognizer.cpp` | compiles |  |
| `engine/src/Input/PlayerInput.cpp` | fails | okay/Input/Input.hpp:367: global initializer must be a constant |
| `engine/src/Optimize/LodGroup.cpp` | fails | cannot open include filesystem |
| `engine/src/Optimize/PortalCells.cpp` | compiles |  |
| `engine/src/UI/FocusGraph.cpp` | compiles |  |
| `shared/LauncherTips.hpp` | compiles |  |
| `shared/OkfDecode.hpp` | compiles |  |

What the three remaining ones stop at: `RandomExtras.cpp` (after `<chrono>`, `<climits>`, `<set>`, `<unordered_set>` were added: a nested class calls a function of its enclosing class declared later, which works now, and then `Random.hpp:264` "no member or base class named Random", not looked into); `PlayerInput.cpp` (after `<deque>`, `<set>`, `<unordered_set>`: `Input.hpp:367` "global initializer must be a constant", not looked into); `LodGroup.cpp` (after `<cwctype>`: `TexturePath.hpp` includes `<filesystem>`, which is not written).

Not done / unverified / found and not fixed (Round 22):

- Not run on Windows; Linux with Wine only. 33 of 36 files only compile; the four crypto files and the math headers are the only engine code linked and run. No test of `tests/` was built as it is.
- Differences of our headers from the standard: `deque` keeps its elements in one block with room at both ends, so a push at either end can invalidate references (the standard keeps them valid); `set` and `unordered_set` are sorted arrays (linear insert and erase, no comparator, `unordered_set` iterates in ascending order and needs `operator<`, not `std::hash`); `stable_sort` is quadratic; `sort` is a heap sort; `unique_ptr` has no deleter and no array form; no `shared_ptr`, `weak_ptr`, `map`, `unordered_map`, `std::function`, `<filesystem>`, `<iostream>`, `<sstream>`, `<random>`. `vector` has no `emplace_back` and its elements are not constructed by a move when the object has no move constructor (they are copied).
- `<cmath>` is our own and not correctly rounded (a few units of the last place of float in the ranges tried: the test compares with tolerances 1e-4 and prints millionths); `pow`, `exp`, `log` for huge or tiny arguments, the signed zero and some NaN cases were not checked. `strtod` builds the value from digits and powers of ten, so the last digit can differ from the nearest double. `printf` truncates `%f` instead of rounding and has no `%e`, `%g`, `%p` or `%o`.
- Internal symbols for inline functions, in-class members and template instances mean one copy per file (more code, a function-local `static` in such a function is one per file, the address of such a function differs between files). No COMDAT folding is written by the compiler (the linker can read it).
- `global_alias` is not a namespace scope: a variable name read after a second declaration of the same name always means the second one, also in code of the first namespace that is read later. `using ns::name;` is read as `using namespace ns;`. A declaration in a `while` condition (`while (T x = f())`) is refused with a message. `friend` declarations are skipped (access is not checked anywhere here); `friend` functions with a body are not read. `extern "C"` for a variable and a plain `extern int x;` are not read. A class name is not forward-declared (`struct X;`).
- A braced argument is read by the parameter it goes to: a call whose callee is chosen by overload resolution on that argument, or a constructor argument, may not find the type; `{...}` as an argument to a function template parameter is not tried.
- `initializer_list<T>` of a class with a destructor is refused. A move constructor is used for `T x = std::move(y)` and for a by-value argument; the return of a local by value still hands over the local itself (no move or copy call), and a class with a user-written move constructor but no copy constructor does not get its copy constructor deleted (a copy of such a class is not refused).
- The decorated name of `const T&` is still `AEA...` for a return type (see Round 21).

### Round 23 (unverified: not run under Wine, Wine was broken in the container this was written in)

Everything below was only compiled and the object inspected (symbols, `objdump -d`); nothing was run, and the full `sh tools/okayms/build.sh` was not green in that container because every Wine run aborted. Before this branch (`claude/okayms-3-r23b`) is merged into `claude/okayms-3` the full build and the link-and-run checks must be run where Wine works. New test: `tests/test_round23.cpp` (compile-only). Sources: ISO/IEC 14882 only, as before.

Step 1 (front end): delegating constructors `C() : C(args) {}` ([class.base.init]/6); function-local statics with non-constant initializers (`static int n = f();`, `static const E tab[] = {{"a", K(1)}}`) built the first time control passes, with a guard flag, no lock; `thread_local` read as `static` (one copy, no thread-local storage); default arguments of function templates (they were dropped) and `= {}` in a template parameter list.

Step 2 (front end, compile-only): reference members `T& x;` (the field holds the address and reads go through it, checked by disassembly of a `b = b + 1` member); `struct X;` forward declarations and pointers to a class before its definition (the definition fills the same entry); `extern int x;` and `extern "C" int x;` (an undefined symbol, only when used; a later definition in the same file wins); `while (T x = f())` (read as `while (true) { T x = f(); if (!x) break; body }`, `break` and `continue` destroy x); the copy constructor is deleted when only a move constructor is written (an error only in a function that is reached, so `vector<unique_ptr<T>>`'s unused copy constructor does not stop a file); a nested class finds the static members and static functions of its enclosing class; two classes may each have a nested `State` (lookup prefers the class being read); `Class::Enum` as a type; `using F = R (*)(A)` and `typedef R (*F)(A)` inside a class; typedefs of a class are visible in the parameter list and body of an out-of-class member definition; `static inline` members with floating-point, array or mutable values are real variables (one copy per file); `static int a = 0, b = 1;`; `T g = expr;` for a global object with a constructor; `int x{};`. Pending (codegen-changing, not done here): implicit class assignment still copies bytes (a struct with a `std::string` member is assigned wrongly), string literal to `const std::string&` parameter is not converted, `T x = T(args)` makes a temporary and a copy, virtual calls through `operator->`, COMDAT/dead-strip, the `.CRT$XCU` startup table (a file with global constructors in a second translation unit), a conditional used as an lvalue.

Step 3 (headers and the compiler changes they need, compile-only). Written from the standard text only (no other implementation): `<functional>` now has `std::function` (the compiler reads `function<R(A, B)>` as `function<R, A, B>` before parsing, so the class is `template <typename R, typename... Args>`; type-erased through a virtual base; copying copies the callable); `<memory>` has `shared_ptr`, `weak_ptr`, `make_shared` (single-thread counts, no custom deleter); `<map>` has `map` and `unordered_map` (a sorted vector, linear insert, iterates in key order, needs `operator<`); `<filesystem>` (Windows-flavoured `path`, `lexically_normal`, `absolute`, `weakly_canonical` without following links, `is_directory`, ...); `<system_error>` (`error_code` only); `<atomic>` gets `atomic_flag`, `ATOMIC_FLAG_INIT` and `compare_exchange_*` with two memory orders; `<cmath>` gets `double` overloads, `fmin`, `fmax`, `nextafter`, `log1p`, `lgamma` (Stirling series with shift and reflection); `<vector>` gets `emplace_back`; `getenv` is in `runtime/env.c` (so a program that never calls it imports nothing; `test_engine_link` adds `env` and the kernel32 names to its libraries). Compiler changes for them: constructor templates (a class with only one is not an aggregate; copy and move constructors do not block them; a lambda or other class object in `T x = value;` goes through the converting constructor), pack expansion `f(a...)` in a call through a function pointer or object, reference types as template arguments (`Box<const int&>`), a virtual call through `operator->` evaluates the object once into a hidden pointer, a reference parameter in a virtual call is passed as a pointer, an argument that is not an object of the parameter's class is made into a temporary by a converting constructor (a string literal for `std::string` or `path`; overload ranking 4), `namespace fs = std::filesystem;` aliases, `using F = R (*)(A)` and class-scope function-pointer typedefs. The suite status: `tests/test_round23.cpp` 11 tests, 15 checks, 0 failed; every other failure in `sh tools/okayms/build.sh` here is a run under Wine (the same lines fail on the unmodified head in this container). `engine/src/Core/RandomExtras.cpp` and `engine/src/Input/PlayerInput.cpp` now compile (they stopped at: delegating constructor, non-constant function-local statics, `std::function`, `emplace_back`, the deleted copy rule). `engine/src/Optimize/LodGroup.cpp` still does not: `Mesh.hpp` needs `<fstream>`, `<sstream>`, `<tuple>` (as `std::map` keys) and `unordered_map` with a hash.

Not done / pending: a range `for` over a class whose `begin()` returns a class iterator (only pointers work; `path` and `TexturePath.hpp` need it); generic lambdas (`auto` parameters, blocks `Undo.cpp`); implicit class assignment still copies bytes (a struct with a `std::string` member is assigned wrongly; found by the `std::map<std::string, int>` work); `T x = T(args)` still makes a temporary and a copy; `f = lambda;` assignment to a `std::function` is not converted; COMDAT/dead-strip of unused inline functions; the `.CRT$XCU` startup table (two files that both have global constructors, e.g. through `Vec2::Zero`, give a duplicate `__okay_init`, so multi-file engine programs that include `Vec2.hpp` in two files do not link); a conditional used as an lvalue; the link-and-run comparison of `Random.hpp`/`RandomExtras.cpp` with MinGW g++ (the program text was written and the MinGW build printed 4165 checks, 0 failures, but it was lost in a container restart and not re-created). All of this must be re-checked under Wine before merging into `claude/okayms-3`.

### Round 24 (unverified: not run under Wine, Wine broken in this container)

Wine check (30 minutes): the Ubuntu archive only has `wine` 9.0 (the same build that aborts with `free(): invalid pointer` here). A WineHQ stable 11.0 package could be downloaded (dl.winehq.org is reachable) but the permission system refused to run a downloaded third-party binary, so it was not tried; nothing else was tried (no qemu-user Windows loader is installed). The decision to run a downloaded Wine belongs to the owner.

Step 1: a range `for` over a class whose `begin()` and `end()` return an iterator class (not a pointer): the loop uses the iterator's `operator!=`, prefix `operator++` and `operator*`. Checked by the calls in the object (`begin`, `end`, `!=`, `*`, `++` per loop). The iterator objects are copied from the calls and their destructors are not run (an iterator that owns memory, like `std::filesystem::path::iterator`, leaks it).

Step 2 (headers, compile-only): `<tuple>` (up to three elements, `make_tuple`, `get<N>` through a helper class, lexicographic `<`/`==`, so `std::map<std::tuple<...>, int>` compiles); `<unordered_map>` is now a real hash map (an entry vector plus chained slot table, hash = third template argument, default `std::hash<Key>`, keys compared with `==`; iteration in insertion order; the compiler now lets a template default name the parameters before it, `typename H = hash<K>`); `<sstream>` (`istringstream`, `ostringstream`, `stringstream`, `>>` for words, ints and floats, `getline`, `<<` for strings, integers and floats written as up to six decimals without trailing zeros, not `%g`); `<fstream>` (`ifstream` reads the whole file when opened, `ofstream` collects the text and writes it on close or destruction; they use `runtime/file.c`, which needs CreateFileA, GetFileSize, ReadFile, WriteFile, CloseHandle; `test_engine_link` builds `file` into the runtime library and adds those imports). Found on the way: a stream parameter named `in` clashed with the enumerator `ios_base::in` (enumerators of a class are global names here). `engine/src/Optimize/LodGroup.cpp` now gets as far as `TexturePath.hpp:81`: `for (; r != root.end(); ++r, ++q)` needs a temporary bound to a `const` reference in a loop condition, which is refused ("a reference bound to a temporary in a loop condition is not supported").

Steps 3 and 4 (compile-only, objdump-checked): `T x = T(args);` is made in place by the constructor (one `??0Tok` call, no temporary, no copy); `f = lambda;` for a class with a converting constructor makes a temporary of the left class first (calls: converting constructor, `operator=`, destructor of the temporary); the temporaries a `for` or `while` condition needs are made again in each pass (`while (c) body` is read as `while (true) { make; if (!c) break; body }`, their destructors still run once after the loop; this was the stop at `TexturePath.hpp:81`); a braced index `m[{a, b}]`; `vector::insert(pos, {a, b, c})`; `lround`, `llround`; errors "no matching constructor", "cannot deduce template arguments" and "cannot work out the type of the auto variable" now name the function they were found in. Enumerators of a class are global names here and hid locals called `in` and `out`, so `ios_base` uses `static const int` constants.
`engine/src/Optimize/LodGroup.cpp` now stops in `Mesh.hpp` at "cannot work out the type of the auto variable (in __lam41::operator()/2)": a lambda in `Mesh.hpp` that declares an `auto` variable whose type is not found. Generic lambdas (`auto` parameters, step 3 of the task) were NOT started, so `Undo.cpp` still stops; remaining ones in this round: generic lambdas, the `auto` variable above, everything under "Not done / pending" of Round 23.

### Round 25 (unverified: not run under Wine, Wine broken in this container)

Compile-only; each item checked by the object (symbols, calls, `objdump -d`). Written from ISO/IEC 14882 only.

1. **Generic lambdas** (`[](auto a, const auto& b) {...}`, [expr.prim.lambda]/5): the closure remembers where its parameter list and body are; each call with new argument types makes one `operator()` from them by reading the parameter list again with `auto` bound to the argument type, and the body is compiled for it (the result type by the same probe as for other lambdas). Checked: `id(4)` and `id(1.5f)` make `??R__lam1@@QEAAHH@Z` and `??R__lam1@@QEAAMM@Z`; a generic lambda passed to `std::remove_if` works. A generic lambda always makes a closure object (a captureless one does not decay to a function pointer).
2. **Implicit class assignment is member-wise** when a member class has an assignment of its own (`std::string`, containers, a nested such class): the compiler writes `operator=` that assigns each member by its own assignment. Checked in the object: `V::operator=` calls `W::operator=` which calls `string::operator=`. Members without one are still copied as bytes; arrays of class members are still copied as bytes.
3. **Range `for` iterator objects are destroyed** when the loop ends (`??1It` after the loop). Not on a `break` out of an enclosing scope or a `return` inside the body.
4. **Conditional as lvalue** `(c ? x : y)++`: already supported by the code generator; the disassembly shows the chosen address then one increment. A conditional whose branches are class values with destructors (`c ? std::string() : s`) now goes through a hidden object (copy constructor for an lvalue branch, the call result built in place for a call).
Also: `<iterator>` (`reverse_iterator<T*>`), `rbegin`/`rend` for `vector` and `deque`, `std::minmax` (holds copies), `set(first, last)`; this made `engine/src/Core/Undo.cpp` compile.

`engine/src/Optimize/LodGroup.cpp` is still the one file that does not compile: it stops inside `Mesh.hpp` (preprocessed line 8540, in the 66th lambda, `expected an expression` after `v ] =`); not identified further. Everything before that (`TexturePath.hpp`, `<fstream>`, tuples, `unordered_map`, `std::minmax`, `set` ranges, generic-lambda-free `auto`) now passes.

Measurement of the 36 files of the tables above (compile to an object with okay-cl, `/c`, this branch, nothing run): **35 of 36 compile** (up from 33 at Round 22: `RandomExtras.cpp`, `PlayerInput.cpp` and `Undo.cpp` now compile; `LodGroup.cpp` does not).

| file | result |
|---|---|
| `engine/include/okay/Components/Stance.hpp` | compiles |
| `engine/include/okay/Core/ClockCursor.hpp` | compiles |
| `engine/include/okay/Environment/Wind.hpp` | compiles |
| `engine/include/okay/Input/InputBuffer.hpp` | compiles |
| `engine/include/okay/Math/Bounds.hpp` | compiles |
| `engine/include/okay/Math/ColorSpace.hpp` | compiles |
| `engine/include/okay/Math/Curves.hpp` | compiles |
| `engine/include/okay/Math/Easing.hpp` | compiles |
| `engine/include/okay/Math/Mat3.hpp` | compiles |
| `engine/include/okay/Math/Mat4.hpp` | compiles |
| `engine/include/okay/Math/Mathf.hpp` | compiles |
| `engine/include/okay/Math/Quat.hpp` | compiles |
| `engine/include/okay/Math/Ray.hpp` | compiles |
| `engine/include/okay/Math/Rect.hpp` | compiles |
| `engine/include/okay/Math/Vec2.hpp` | compiles |
| `engine/include/okay/Math/Vec3.hpp` | compiles |
| `engine/include/okay/Math/VecInt.hpp` | compiles |
| `engine/include/okay/Physics/ForceMode.hpp` | compiles |
| `engine/include/okay/Physics/PhysicsMaterial.hpp` | compiles |
| `engine/include/okay/Physics/PhysicsQuality.hpp` | compiles |
| `engine/include/okay/Render/Color.hpp` | compiles |
| `engine/include/okay/UI/TooltipPlace.hpp` | compiles |
| `engine/include/okay/VisualScript/VsPinType.hpp` | compiles |
| `engine/src/Audio/Mp3Tables.hpp` | compiles |
| `engine/src/Core/RandomExtras.cpp` | compiles |
| `engine/src/Crypto/Blake2b.cpp` | compiles |
| `engine/src/Crypto/ChaCha20.cpp` | compiles |
| `engine/src/Crypto/Sha512.cpp` | compiles |
| `engine/src/Crypto/Util.cpp` | compiles |
| `engine/src/Input/GestureRecognizer.cpp` | compiles |
| `engine/src/Input/PlayerInput.cpp` | compiles |
| `engine/src/Optimize/LodGroup.cpp` | okay/Render/Mesh.hpp:5036: expected an expression (in __lam66::operator()/1, after kinds 1 v/35 /16 , line 8540, token ) |
| `engine/src/Optimize/PortalCells.cpp` | compiles |
| `engine/src/UI/FocusGraph.cpp` | compiles |
| `shared/LauncherTips.hpp` | compiles |
| `shared/OkfDecode.hpp` | compiles |

Still not done: COMDAT/dead-strip, the `.CRT$XCU` startup table, destructors on early exit from a range `for`, the Random link-and-run comparison, and everything must be re-run under Wine before merging into `claude/okayms-3`.

### Round 26 (unverified: not run under Wine, Wine broken in this container)

Compile-only; checked by the object and `objdump -d`. Written from ISO/IEC 14882 and 9899 only.

1. **Preprocess dump:** setting the environment variable `OKAY_PREPROCESS_TO=<file>` makes okay-cl write the text after preprocessing (there is no `/E` flag; the lines in parse errors that say `line N` refer to that text). It found the `v ] =` construct in `Mesh.hpp`: `vertices[v] = {a, b, c}` inside a `[&]` lambda in a member function, i.e. a lambda using the members of its class. Fixed: a lambda that names a member of the enclosing class (or says `[this]`) captures `this` as a field `__this` and reaches fields, methods and statics through it (also in a static member function, where only statics can be named). Then, on the way through `Mesh.hpp`, in order: unary plus (`+1`); `std::max({a, b, c})` (an overload per element type, chosen by the first element's type), `std::cbrt`, `std::abs(float)`; `std::array<int, 3>` from `{a, b, c}` as a temporary (brace elision); an aggregate with a reference member `struct { V& r; } x{v};`; `a = b = Vec3{...}` for class values; `std::unique`, `std::is_sorted`; `fopen`/`fread`/`fwrite`/`fseek`/`ftell`/`fclose` (runtime/file.c, whole-file); `string::find_last_of`/`find_last_not_of`; a range `for` over `{&a, &b}` (a pointer element was refused as "initializer must be a constant"); and a real bug: the probe that finds a lambda's result type threw away template instances and class members the body needed, so `make_tuple` in a lambda gave "unknown function tuple<...>::<ctor>". Now only the lambda's own probe function is dropped.
   **`LodGroup.cpp` still does not compile**: it now stops in `Mesh.hpp` with "large struct return has no destination (in __lam53::operator()/3)" (a lambda with two parameters returning a struct larger than 8 bytes; small reproductions of the shape compile, so the exact body is not yet found).
2. **Whole library, `engine/src` (348 `.cpp` files), compile to an object, nothing run:** **22 compile, 326 do not.** Failures by kind (first error of each file):

| kind | files |
|---|---|
| `cannot open include typeinfo` (`typeid`, `std::type_info`) | 80 |
| `cannot open include typeindex` | 26 |
| `cannot open include variant` | 23 |
| `cannot open include iostream` | 19 |
| `cannot open include locale` | 17 |
| `cannot open include random` | 8 |
| `cannot open include condition_variable` / `thread` (not allowed in Windows builds anyway, rule 21a) | 7 |
| `expected an expression` or `expected a type` (about 37 + 13, mostly in `okay/Core`, `okay/Modeling`, `okay/Debug`; includes 19 in `Utf*.hpp`) | about 70 |
| `large struct return has no destination` | 6 |
| `expected a class template body` | 5 |
| `no matching constructor in LocArg`, `cannot deduce template arguments of Barrier`, `expected a base class`, `expected a name`, `unknown name/function` and single ones | about 15 |

   Added this round for it: `<clocale>`, `<cerrno>`, `<iosfwd>`, `<ctime>` (declarations only), `<string_view>`, `<queue>` (queue and a heap priority_queue). Before them the same run gave 21 compiling files and 236 files stopped on a missing header (`clocale` 35, `ctime` 25, `string_view` 15, `queue` 4, ...). The next steps by size: `<typeinfo>` and `typeid` (80 files, needs compiler support), `<typeindex>`, `<variant>`, `<iostream>`, `<locale>`, `<random>`, then the parse errors in `Utf*.hpp`.
3. **Not done this round:** destruction of range-for iterators on `break`/`return` (still only at normal loop end), COMDAT-style dedup and the `.CRT$XCU` startup table (design above in Round 22/23 notes: an own PE-reader check would be the way to verify them; not started), the Random link-and-run comparison. All of it must be re-run under Wine before merging into `claude/okayms-3`.

### Round 27 (unverified: not run under Wine, Wine broken in this container)

Compile-only; checked by objects. Whole-library measurement of `engine/src` (348 `.cpp`, compile to an object, nothing run): **23 compile** (22 before this round). The first error moved for hundreds of files, which unmasks the next one; the numbers below are the first error of each failing file after the round.

1. **`typeid`, `<typeinfo>`, `<typeindex>`** ([expr.typeid], [type.info], [type.index]): the compiler makes one `type_info` object per type per file (the address of the type's decorated name; equality compares the name text, because there is one object per file); a class with virtual functions gets the address of its object in the slot before its virtual table (vtable[-1], checked in the object: a `ADDR64 __ti_UD@@` relocation at offset 0 before `??_7D@@6B@`, and every vtable symbol now sits 8 bytes later), and `typeid(*p)` reads it. `std::type_index` (with a default constructor, a non-standard extra, so `pair<type_index, V>` works) and `std::hash<type_index>` make it usable as a key of `std::map` and `std::unordered_map`. A minimal `<type_traits>` (`is_same`, `is_integral`, `is_floating_point`, `is_pointer`, `remove_reference`, `remove_const`, `remove_pointer`, `conditional`, `enable_if`; the traits are plain classes with `::value`, no `_v`/`_t` forms, no `is_base_of`) took away 106 missing-header stops.
2. **`Utf*.hpp`:** `char16_t`, `char32_t`, `wchar_t` (integers: 16, 32, 16 bits) and `std::u16string`, `std::u32string`, `std::wstring` (one small growable array class; no conversions between strings, no find), and `if constexpr (c)` read as `if (c)` (both branches are compiled). `Utf8.hpp` compiles.
3. **`<variant>`: not working.** A first version (aligned buffer, index, `get`, `get_if`, `holds_alternative`) was written but not kept: calling `get<T>(v)` needs deducing a pack from a parameter `variant<Ts...>&` while `T` is given, and that gives "cannot deduce template arguments" (reduced to `template <typename T, typename... Ts> int pick(L<Ts...>& v)` called as `pick<int>(v)`). The work file is not in the tree. A compiler change that lets a function template take its own pack from a class-template parameter (`match_type` with a trailing pack) is needed first; I only made the pattern parse (`variant<Ts...>&` in a function template parameter list).
4. **Not done:** `<iostream>`/`<locale>`/`<random>` (39 + 19 + 11 files first stop there), the six `large struct return has no destination` cases, the remaining parse errors (47 `expected ...` + 26 `expected a type`), `<mutex>`/`<thread>`/`<condition_variable>` (rule 21a: intentionally excluded, 17 files).

First error of each failing file now (tool: okay-cl /c on every `engine/src/**/*.cpp`):

| first error | files |
|---|---|
| `cannot open include variant` | 65 |
| `expected ...` (various) | about 52 |
| `cannot open include iostream` | 39 |
| `expected a type` | 26 |
| `cannot open include locale` | 19 |
| `cannot open include random` | 11 |
| `no member function assign with N arguments` (`vector::assign(first, last)`) | 8 |
| `cannot open include thread` / `condition_variable` / `mutex` (excluded, rule 21a) | 17 |
| `large struct return has no destination` | 6 |
| `expected a class template body` / `expected a base class` | 10 |
| other single errors | rest |
