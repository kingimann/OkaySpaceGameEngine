# Our own C and C++ compiler (okay-cc)

Internal note. Not a user guide. Nothing here is a promise that a stage is finished.
The program is **off by default**: it is **not in the engine CMake**, not in CI, and not in
releases. The engine does not include it and does not link it. It lives in `tools/okaycc/`
and is built only by `tools/okaycc/build.sh`.

This is a long-term research project: our own version of the job MSVC and Clang do, so we
have something of our own to build toward. It does not replace them.

## Status of this branch

- Lexer: implemented in `tools/okaycc/` (`okay-cc --lex`). Tested: identifiers, keywords,
  integer and floating constants, character and string literals, punctuators, comments,
  line splices, and header names.
- Preprocessor, parser, type checker, and every later stage: not started.

## Rules for the code

Every line is written here. We do not copy, port, or closely follow GCC, Clang/LLVM, MSVC,
tcc, chibicc, Cranelift, or any other compiler, in code, comments, or tests. Ideas,
algorithms, and the layout of a file format are not owned by anyone; a particular program's
source is. Where a textbook describes an algorithm, we write our own code from that
description.

No third-party libraries. C++17 and the C++ standard library only. No cryptography.
The first host is POSIX (files, paths, a terminal). Windows is a later target for the
*output* of the compiler, not a requirement for the host that runs it.

## Stages

Each stage below says what it has to do, what it needs before it can start, which document
it is written from, and how big the work is. "Small", "medium", "large", and "very large"
are engineering size, not a calendar.

### 1. Lexer

Break a source file into tokens: identifiers, keywords, integer and floating constants,
character constants, string literals, and punctuators. Track the physical line and column.
Replace comments with whitespace. Join lines that end in a backslash. Report a clear error
for an unterminated string, an unterminated comment, a bad escape, or a malformed number.

Needs: nothing before it.

Written from: ISO/IEC 9899:2018 (C17, which is C11 plus defect reports), clause 5.1.1.2
(translation phases 1 to 3), clause 5.2.1 (source and execution character sets), and
clause 6.4 (lexical elements: keywords 6.4.1, identifiers 6.4.2, constants 6.4.4,
string literals 6.4.5, punctuators 6.4.6, header names 6.4.7, comments in 6.4.9).
C++ keywords, raw string literals, and user-defined literals come later from ISO/IEC 14882:2017
(C++17), the lexical-conventions clauses. Trigraphs (C17 5.2.1.1) are not implemented;
C++ dropped them and C23 dropped them.

Size: small.

### 2. Preprocessor

Directives and macro expansion, run as translation phase 4 (C17 5.1.1.2).

What it has to do:

- `#include` with `"..."` and `<...>`, search paths, and a hard stop on runaway nesting
  so a file that includes itself cannot loop forever.
- Object-like and function-like `#define`, including `#` (stringize) and `##` (paste),
  and `#undef`.
- `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif`, with `defined` and integer
  constant expressions.
- `#error`.
- `__LINE__` and `__FILE__`.

Needs: the lexer. Header names are a lexer mode, because `<file.h>` is one name in an
include and five tokens anywhere else (C17 6.4.7).

Written from: C17 clause 6.10 (preprocessing directives), in particular 6.10.1
(conditional inclusion), 6.10.2 (source file inclusion), 6.10.3 (macro replacement,
including argument substitution, `#`, and `##`), 6.10.4 (`#line`, later), 6.10.5
(`#error`), and 6.10.8 (predefined macro names). The expression rules inside `#if` are
C17 6.6 (constant expressions), with the extra `#if` rules in 6.10.1: leftover identifiers
become 0, and the arithmetic is done at `intmax_t` / `uintmax_t` width. We use 64-bit
two's complement for that. The C++ preprocessor is the same idea; differences are taken
from ISO/IEC 14882:2017, the preprocessing-directives clauses, when we get there.

Size: medium. This is the hardest part of a C front end. Expansion that is "almost right"
is easy; the hide set on a macro name, the order of argument expansion versus `##`, and
empty arguments are where it goes wrong.

### 3. Parser

Turn the preprocessed token stream into a tree.

First slice: a C subset, not all of C. Types are `void`, `char`, and `int`, plus pointers
and arrays. Declarations and function definitions. Statements: compound, expression, `if`
/`else`, `while`, `for`, `return`, `break`, `continue`. Expressions with the C precedence
and associativity (comma, assignment, `?:`, `||`, `&&`, bitwise, equality, relational,
shift, additive, multiplicative, casts, unary, postfix). `okay-cc --ast` prints the tree.

Later: the rest of C17 (structs, unions, enums, typedef, all initializers, `switch`,
`goto`, bit-fields, qualified types, linkage). After that, C++: classes, references,
overloading, namespaces, templates, exceptions, lambdas, `constexpr`.

Needs: lexer and preprocessor. The parser sees phase 7 tokens (C17 5.1.1.2): keywords
are keywords only after preprocessing, so the parser classifies them, and the lexer
already tags the spellings.

Written from: C17 clause 6.5 (expressions), 6.7 (declarations), 6.8 (statements), and
6.9 (external definitions), using only the productions in the subset. The C++ grammar,
when we add it, comes from ISO/IEC 14882:2017 (expressions, statements, declarations,
classes, overloading, templates). The precedence table is the one in C17 6.5, not a
table copied from another compiler's source.

Size: medium for the subset. Large for the rest of C. Very large for C++.

### 4. Type checker

Decide whether the tree is legal, and give every expression a type.

For the subset: integer promotions (`char` promotes to `int`), the usual arithmetic
conversions for `char` and `int`, pointer arithmetic, assignment compatibility, call
and return checks, array-to-pointer decay, lvalues, scopes, and redefinitions.
`void` is not a variable. A global initializer must be a constant. An array length
must be a positive constant (no variable-length arrays in the subset).

Needs: the parser's tree.

Written from: C17 6.2.5 (types), 6.2.7 (compatible types), 6.3 (conversions), the
constraints in 6.5 (each operator), 6.7.6 (declarators, including the adjustment of
an array parameter to a pointer in 6.7.6.3), and 6.8 (the constraint that `break`
and `continue` are inside a loop). C++ name lookup, overload resolution, and template
instantiation are later, from ISO/IEC 14882:2017, and they are a different size of job.

Size: medium for the subset. Very large for C++ (overload resolution and templates
are each bigger than this whole subset).

### 5. Intermediate form

Our own intermediate representation (IR), lowered from a typed tree. Three-address
instructions: a result, an operation, and operands. Basic blocks and a control-flow
graph. No exception-handling edges and no templates in the first version.

Needs: a typed tree for the subset. The IR does not have to wait for all of C++.

Written from: Aho, Lam, Sethi, and Ullman, *Compilers: Principles, Techniques, and
Tools* (2nd edition), the chapters on intermediate code generation and basic blocks.
The instruction set of the IR is our own; it is not LLVM IR and not GCC's GIMPLE.
Names of textbook ideas (three-address code, a basic block, a control-flow graph)
are used because those are the names of the ideas.

Size: medium.

### 6. Simple optimizer

Local passes on the IR: constant folding, algebraic simplifications that are identities
(for example adding zero), and deletion of an instruction whose result is never used.
No inlining, no loop optimization, and no interprocedural analysis in the first version.

Needs: the IR.

Written from: the same textbook, the chapter on optimization of basic blocks, and
Muchnick, *Advanced Compiler Design and Implementation*, for the later global passes
(we do not start those until the local ones are tested). We work out the rewrite
rules ourselves and test them on IR we built, not on dumped output from another compiler.

Size: medium for the local passes. Large if it grows into a real optimizer.

### 7. x86-64 code generator

Pick machine instructions for the IR, assign registers, and lay out a stack frame.
First target: System V AMD64 (Linux ELF). Second: the Windows x64 calling convention,
because the engine's programs are Windows programs.

Needs: the IR. An optimizer is helpful and not required; unoptimized code is a valid
first output.

Written from:

- Intel 64 and IA-32 Architectures Software Developer's Manual, Volume 2 (instruction
  encodings: opcode, ModR/M, SIB, REX, immediate, displacement).
- AMD64 Architecture Programmer's Manual, Volume 3.
- System V Application Binary Interface, AMD64 Architecture Processor Supplement
  (the System V x86-64 ABI): which registers pass arguments, stack alignment of 16,
  the red zone, caller-saved and callee-saved registers.
- Microsoft's x64 calling convention: the first four integer arguments in RCX, RDX,
  R8, and R9, XMM0 to XMM3 for floating arguments, 32 bytes of shadow space, and a
  different set of callee-saved registers.

Size: large. Instruction selection is a lot of careful encoding. Register allocation
is the hard part; the first version can be a stack machine that keeps every value in
memory, which is slow and correct, and a real allocator can replace it later.

### 8. Assembler

Turn the code generator's instructions into bytes. This is our own encoder, not a call
to an external assembler. It has to emit the same encodings Volume 2 describes, and
it has to leave relocations where an address is not known yet.

Needs: the code generator's instruction list, and the relocation kinds from the object
format we are writing.

Written from: the Intel manual, Volume 2, the encoding chapters. Not from the source
of gas, nasm, or any other assembler.

Size: medium for the instructions we actually emit. Large if we try to encode the
whole manual before we need it.

### 9. ELF object writer

Write a relocatable ELF64 object file: file header, section headers, `.text`, `.data`,
`.rodata`, `.bss`, a symbol table, a string table, and relocations. Not an executable
yet. Not a shared library yet.

Needs: bytes and symbols from the assembler.

Written from: the System V ABI ELF gABI (Elf64_Ehdr, Elf64_Shdr, Elf64_Sym, section
types, string tables) and the x86-64 processor supplement for relocation types
(`R_X86_64_64`, `R_X86_64_PC32`, and the others we actually emit). We fill in the
struct fields from that specification.

Size: medium.

### 10. COFF object writer

The same job for Windows: a COFF object the Windows linker could consume. File header,
section headers, symbol table, relocations. PE executables are a later step on top of
COFF; the first target is a relocatable object, matching the ELF stage.

Needs: the same bytes and symbols as the ELF writer.

Written from: Microsoft's Portable Executable and Common Object File Format
Specification (COFF headers, section headers, symbol table, relocations).

Size: medium.

### 11. Linker

Resolve symbols across object files and apply relocations. First: our own ELF objects
into one relocatable object or a simple static executable. Later: the same for COFF,
and only then a PE executable. We do not shell out to `ld` or `link.exe`.

Needs: object writers that agree on symbol names and relocation kinds.

Written from: the ELF gABI rules for applying a relocation, the PE/COFF specification's
relocation and symbol rules, and the textbook description of symbol resolution in
Aho, Lam, Sethi, and Ullman (the linking chapter) and in Levine, *Linkers and Loaders*
(the concepts of a symbol table, relocation, and section merging). The code is ours.

Size: large. Real objects from other compilers have a long tail of section kinds and
symbol flags. The first linker only has to link objects we ourselves wrote.

### 12. Driver

A program `okay-cc` that runs the stages in order. Flags select how far to go:
`--lex`, `--pp`, `--ast`, `--check`, and later `--emit-ir`, an object file, and a link.
Include paths (`-I`) are passed to the preprocessor. The driver does not call another
compiler.

Needs: whichever stages exist. Early on it stops after the front end.

Written from: the ordinary pipeline in the textbook (preprocess, compile, assemble,
link). The flag names are ours.

Size: small on top of the stages. The hard part is the stages, not the driver.

## Decisions for the first code

These are choices the C standard leaves open, or places we deliberately do less than
the standard. They are written down so the tests have an oracle that is not "whatever
another compiler printed".

- The execution character set for the basic characters is ASCII. A universal character
  name in an ordinary string is encoded as UTF-8. A wide character constant (`L`, `u`,
  `U`) holds a Unicode code point. `L` is treated as 32-bit.
- A multi-character narrow constant packs up to four bytes, last byte in the low bits.
- An empty `()` parameter list means zero parameters. That is the C++ rule. It is not
  C's "unspecified arguments". `(void)` also means zero parameters.
- No trigraphs, no variable-length arrays, no `_Generic`, no statement expressions.
- `#if` / `#elif` arithmetic is 64-bit two's complement. Division or remainder by zero
  is an error. `&&`, `||`, and `?:` do not evaluate the side they skip.
- A source file that does not end in a newline is accepted. The standard says that
  case is undefined; we treat end of file as ending the last line.
- `#define` or `#undef` of `defined`, `__LINE__`, or `__FILE__` is an error. The
  standard makes it a constraint violation (C17 6.10.8).
- A variadic macro may be called with no extra argument (`F(1)` for `#define F(a, ...)`).
  C99's wording says there must be more arguments than named parameters; C++ allows
  the empty pack. We allow it, and `__VA_ARGS__` is then empty.
- `##` uses the standard placemarker rule for an empty argument. It does not delete
  a comma (that is a GNU extension, and we do not implement it).
- `__DATE__`, `__TIME__`, and `__STDC__` are not predefined yet.
- Include search is our choice of the implementation-defined rule (C17 6.10.2):
  `"..."` searches the directory of the file that contains the directive, then each
  `-I` path; `<...>` searches only the `-I` paths. There is no built-in system
  directory. We do not ship libc headers.
- Include nesting stops at 64 files. That is the recursion guard.
- Digraphs (`<:`, `:>`, `<%`, `%>`, `%:`, `%:%:`) are recognized, as in C17 6.4.6.
- A tab stop is every 8 columns, for the column number in a diagnostic.

## Honest estimate

A full C++ compiler is years of work for a team. This note exists so we do not pretend
otherwise.

What a single careful pass can finish is the front of the subset: the lexer, the
preprocessor, the subset parser, and a type checker for that subset, with tests. That
is a real program and it is not a compiler yet. It cannot emit an instruction.

Relative cost, stacked on that front end:

- The rest of C17 (the parser and the semantics, not the code generator) is several
  times the subset.
- C++ is larger than the whole C compiler. Templates, overload resolution, classes,
  and two-phase name lookup are each a project of their own. ISO/IEC 14882:2017 is
  the source, and it is a much bigger document than the C standard.
- An IR and a local optimizer are each about the size of the subset parser.
- An x86-64 code generator and a register allocator are large.
- An assembler for the instructions we emit, and an ELF writer, and a COFF writer,
  are each medium.
- A linker is large.
- A C library is not part of the compiler, and without one the compiler still cannot
  build a program a person would run (`printf`, startup, `memcpy`). That library is
  large, and it is a separate piece of work. It is named here so it is not forgotten
  and then treated as a surprise.

Nothing in this file is "ready", and none of it is a reason to switch the engine off
MSVC or Clang. The exit test for ever defaulting to okay-cc is the same one the rest
of our own-code work uses: it has to be finished, tested on real programs, and the
owner has to say so. That is not close.

## What is not done, on purpose, until a later stage

Code generation, assembly, object files, linking, C++, a C library, `__DATE__` and
`__TIME__`, `#line`, `#pragma` (rejected as unsupported rather than silently ignored),
trigraphs, and any GNU extension.
