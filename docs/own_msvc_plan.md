# Our own MSVC-style compiler and linker (okay-ms)

Internal note for issue #992. This is not a user guide. Nothing in this tree is wired into the engine, into CMake, into CI or into a release.

## What this is

`tools/okayms/` is a long-term research toolchain. It is the Windows side of writing our own tools: an MSVC-style command line, the Windows x64 calling convention, COFF object files, a PE/COFF linker and import libraries.

It is not a replacement for MSVC. The owner said on 2026-11-17 that this does not replace MSVC any time soon. It stays off by default. The engine does not call it. A release does not ship it.

The other long-term compiler is issue #987, in `tools/okaycc/`: a Clang-style C and C++ front end, its own intermediate form, x86-64 code, ELF first. okay-ms shares no source files with okaycc. Notes can talk about both. Code stays separate until the owner says otherwise.

The host is POSIX. The copy that was written here is built and tested on Linux. The linker's target is a Windows PE32+ executable, produced on Linux. That is a cross-tool. There is no cryptography in this tree.

## Rules for this tree

Every line is written in this tree. Nothing is copied, ported or closely followed from MSVC, Clang, LLVM, GCC, lld, MinGW, binutils or tcc, in code, comments or tests. Each part names the document it was written from. Those documents are listed below. Header sizes, relocation type numbers and opcode bytes come from those documents, and the tests check the bytes.

No third-party libraries. The language is C++17 and the C++ standard library. Build with `tools/okayms/build.sh`. The engine build does not call that script.

## Stages

The order below is fixed. A later stage starts only after the earlier stage's tests pass. Tests live in `tools/okayms/tests/`. Each stage's tests are run once while the stage is missing (they fail) and once after the stage is written (they pass).

### Stage 1 — COFF object writer and COFF object reader

Write a COFF object and read it back. The file holds a file header, section headers, section bytes, relocation entries, a symbol table and a string table.

This stage needs:

- The 20-byte file header, the 40-byte section header, the 10-byte relocation and the 18-byte symbol record. All of them are little-endian, as the PE Format document defines them.
- A name of 8 bytes or fewer stored in the 8-byte name field. A longer symbol name stored as four zero bytes and then an offset into the string table. A longer section name stored as a slash and the decimal offset.
- A reader that stops on a truncated header, a truncated table or a count that does not fit in the file, instead of reading past the buffer.
- Tests that compare the writer's output with an array calculated by hand from those sizes, and a round trip: write, read, write again, and the bytes match.

This stage does not need a linker, an import or a machine that runs Windows.

Written from: the PE Format document, the chapters on the COFF file header, section header, relocations, symbol table and string table. The AMD64 relocation type numbers used in the next stage are in the same document.

### Stage 2 — PE linker, static console executable

Link one or more hand-made COFF objects into one PE32+ console executable. There is no import table. The entry point is a symbol named `entry`. The machine code puts an exit code in `eax` and executes `ret`. On Windows the loader uses that return value as the process exit code.

This program was not run on Windows. Wine is not part of this stage. A later stage runs under Wine only when Wine is actually installed, and the report says which.

This stage needs:

- A DOS header (`MZ`, and `e_lfanew` at byte 60) and the four-byte `PE\0\0` signature.
- A COFF header with machine `0x8664` (AMD64), the executable-image and large-address flags, and a PE32+ optional header. The optional-header magic is `0x20B`. The size used here is 240 bytes, which is the fixed optional header plus 16 data-directory entries.
- Section alignment `0x1000` and file alignment `0x200`. `SizeOfHeaders` and `SizeOfImage` are rounded up as the PE Format document requires. The first section's file offset is `SizeOfHeaders`. Its address in the image is that size rounded up to the section alignment.
- Two passes. The first pass gives every piece of every section an address and resolves symbols. The second pass adds each relocation's result to the bytes already stored there. The AMD64 formulas used in this stage are `ADDR64` (add the 64-bit virtual address), `ADDR32` (the same, and it is an error if the sum does not fit in 32 bits), `ADDR32NB` (add the 32-bit address without the image base) and `REL32` (a 32-bit displacement from the byte after the relocation field).
- A second definition of the same external symbol is an error. A missing `entry` symbol is an error.
- COMDAT sections, weak externals and base relocations are refused with an error. They are not guessed. Until a base-relocation table exists, the image is marked "relocations stripped" and is not marked dynamic-base, so the loader is told to keep the preferred base.
- Tests that read the DOS header, the PE signature, the optional header, the section table, the zero padding from alignment, and the relocated instruction bytes, each at the file offset the layout rules give. `file` must report the text `PE32+ executable (console) x86-64`.

Written from: the PE Format document (DOS header, PE signature, COFF header, optional header for PE32+, section table, AMD64 relocations). The opcode bytes in the tests (`mov eax, imm32`, `ret`, `mov eax, dword ptr [rip+disp32]`, `call rel32`) are encoded from the Intel 64 and IA-32 Architectures Software Developer's Manual, Volume 2. The same long-mode forms are specified in the AMD64 Architecture Programmer's Manual, Volume 3. The two-pass shape (give everything an address, then patch) is the general model in John R. Levine, *Linkers and Loaders*. The field widths and the addends are from the PE Format document, not from that book.

### Stage 3 — Import tables (kernel32.dll)

A hand-made object calls `ExitProcess` in `kernel32.dll` through an import address table. The process then exits through Windows instead of only returning from `entry`.

This stage needs:

- Import data directory (directory index 1). One import descriptor per DLL, then a descriptor of zeros that ends the table.
- An import lookup table and an import address table. Each entry is an 8-byte thunk. The thunk holds the relative virtual address of a hint/name record: a 16-bit hint, the ASCII name, and a zero byte. The top bit of the thunk is clear, because this is an import by name, not by ordinal.
- The DLL name as ASCII with a zero byte after it.
- A symbol `__imp_ExitProcess` placed on the import-address-table slot. The object reaches it with a RIP-relative indirect call, and the COFF relocation is `REL32`, because the displacement is measured from the next instruction.
- The call follows the Windows x64 calling convention. The first integer argument is in `rcx` (written here as `ecx` when the value is small). The caller reserves 32 bytes of shadow space. The stack is 16-byte aligned at the `call`.
- If Wine is installed, run the executable and record the exit code and the command. If Wine is not installed, say that it was not run. Either way it was not run on Windows unless a report names a Windows machine, which this environment does not have.

Written from: the PE Format document (import directory, import descriptor, thunk data, hint/name, import address table). Microsoft's "x64 calling convention" document for `rcx`, the 32-byte shadow space and the alignment. Intel 64 and IA-32 Architectures Software Developer's Manual, Volume 2, for `sub rsp, imm8`, `mov ecx, imm32` and the indirect `call`.

### Stage 4 — Archive libraries (`.lib`) and the rest of relocation

Not started. There is no archive reader in this tree.

This stage will need, when it is written:

- A reader for a COFF archive (a `.lib` file): the archive signature, the member headers, the linker symbol members, and the COFF objects stored as members. The PE Format document describes that layout. It also describes the short import-library header, which is a small record in front of one imported symbol and is not a full COFF object.
- A search of those members when a symbol is still undefined.
- The AMD64 relocations that stage 2 does not apply yet (`REL32_1` through `REL32_5`, `SECTION`, `SECREL`, and any type this linker still refuses on purpose). A refused type must be an error, not a silent zero.
- COMDAT sections and weak externals, from the section flags and the symbol storage classes in the same document, each with a test that fails before the rule exists.
- A base-relocation table (data directory 5) so the image may load away from its preferred base. Until that table is written, stage 2's "relocations stripped" flag stays.

Written from: the PE Format document (archive files, the import-library format, base relocations, the remaining AMD64 relocation types, COMDAT, weak externals).

### Stage 5 — MSVC-style driver

Not started. There is no `cl`-style driver in this tree. The first flags, and only these, are:

| Flag | What it will mean |
|---|---|
| `/c` | Write an object and stop. Do not link. |
| `/Fo` | Path of the object file. |
| `/Fe` | Path of the executable. |
| `/I` | Add a directory to the include search. |
| `/D` | Define a macro for the preprocessor. |
| `/O2` | Optimise. Until an optimiser exists, a driver may accept `/O2` and must say that it did not optimise. Accepting the flag is not an optimiser. |
| `/link` | The words after `/link` are linker arguments. |

`/I`, `/D` and a real `/O2` need a front end. The flag parser itself is small. An optimiser is not.

Written from: the public MSVC compiler-option descriptions (the flag names and what each switch is for), reimplemented from those descriptions. When a front end exists, the C and C++ languages behind `/I` and `/D` are written from ISO/IEC 9899 (C) and ISO/IEC 14882 (C++). No other compiler's sources are the source for that work. The front end does not exist yet, so those two standards are not used by any code in this tree today.

### Stage 6 — Link the output of our own compiler (#987)

Not started, and blocked on okaycc (issue #987). When that compiler can write an object file, this linker should be able to link it. Today the two tools share no files, and this linker does not read okaycc's output.

The meeting point is likely COFF. Either okaycc emits the COFF that stage 1 already reads, or a converter sits between them. That choice is the owner's. This tree will not grow a second copy of okaycc's front end in order to get there.

Written from: issue #987, the notes for that compiler, and the COFF definition from stage 1.

### Later work, not a deliverable of this branch

- Unwind data, so a function that uses the stack can be unwound: a `RUNTIME_FUNCTION` table and `UNWIND_INFO` codes. Written from Microsoft's "x64 exception handling" document when it is written. It is not written. Stage 2's samples either do not touch the stack or use one fixed `sub rsp`, and they emit no unwind info. That is a gap. It is not a claim that unwinding works.
- A C front end, then a C++ front end (templates, exceptions, name mangling, and the Microsoft C++ ABI). The ABI work alone is large enough to be its own multi-year project.
- A C++ standard library. Not started. It is not part of the linker.
- DLLs, delay-load imports, resources, CodeView, PDB files, incremental linking and a real base-relocation table paired with dynamic base. None of these are in this branch.

## Source documents

| Part | Document it is written from |
|---|---|
| COFF and PE headers, sections, symbols, the string table, relocations, imports, archives | Microsoft, "PE Format" (the public Portable Executable and COFF specification) |
| `rcx`, shadow space, 16-byte stack alignment | Microsoft, "x64 calling convention" |
| Unwind codes and the function table (not implemented) | Microsoft, "x64 exception handling" |
| Instruction bytes in the tests | Intel 64 and IA-32 Architectures Software Developer's Manual, Volume 2. The same long-mode encodings are in the AMD64 Architecture Programmer's Manual, Volume 3 |
| The language, once a front end exists (none does) | ISO/IEC 9899 (C) and ISO/IEC 14882 (C++) |
| Driver flag names, once a driver exists (none does) | Microsoft's compiler-options documentation, from the descriptions of the switches |
| Two-pass linking | John R. Levine, *Linkers and Loaders*, for the general model only. The bytes are from the PE Format document |

No compiler or linker source was read in order to write this tree.

## Honest estimate

This is years of work. That sentence is the estimate. There is no date on it.

Small enough to finish as research in this branch, and only if the tests pass: stage 1 (the COFF object writer and the COFF object reader), stage 2 (a static PE32+ console image from hand-made objects) and stage 3 (one import from `kernel32.dll`). Each of those is a file format and a handful of relocation formulas. Together they are not a compiler.

Larger, and not started: stage 4 (archives, the rest of the relocations, COMDAT, weak externals), unwind info, and a base-relocation table.

The bulk, and the reason the owner called this a long-term project: a C front end is a large program; a C++ front end with templates, exceptions and the Microsoft ABI is work that takes years; a standard library is another project of that size; an optimiser that would make `/O2` true is another. None of that has been begun. A green test in stage 2 means the bytes on disk match the specification. It does not mean a Windows machine ran the program.

Wine, if a report says it was run, is still not Windows.

## What this branch does not do

- It does not open a pull request and it does not change `main`.
- It does not edit `tools/okaycc/`, CMake, CI, the feature map or the engine.
- It does not ship, and it is off by default.
- It does not contain a compiler, a preprocessor, an optimiser, a `.lib` reader, unwind info, a DLL writer or a C++ ABI.
- It was not run on Windows.
