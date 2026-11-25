# OkayScript bytecode machine

> **What this is:** the design note for running OkayScript as bytecode on a stack machine.
>
> **Status:** internal record. Branch `claude/okayscript-vm-1` (2026-11-24). Every function, lambda, top-level body,
> default argument and field starting value runs as bytecode. This is the only way OkayScript runs: the tree walker
> is deleted (rule 19d). Language stage 2 and the system library: branch `claude/okayscript-lang-2` (2026-11-24),
> sections "Language stage 2" and "Library" below.

This was written from general compiler and VM design: code generation over the syntax tree, an operand stack, frame
slots for locals, and jump targets patched once they are known. No code or instruction set was taken from another VM
(rule 19a).

## Files

- `okayscript/include/okayscript/Bytecode.hpp`: the instructions, `Chunk` and the per-instruction caches.
- `okayscript/src/Compile.cpp`: the compiler and `Disassemble`. The compiler does not depend on the value type.
- `okayscript/include/okayscript/detail/Vm.ipp`: `BasicRuntime<V>::RunChunk`, the machine.
- `okayscript/include/okayscript/detail/Eval.ipp`: what each node means once its operands are worked out (members,
  dotted names, casts, compound index assignment, events...). The instructions call these.
- `FunctionDecl::code`, `LambdaExpr::code`, `Program::topCode`, `FunctionDecl::defaultCode` and `FieldDef::initCode`
  hold the chunks. Each is compiled the first time it runs and is freed with the syntax tree.
- `okayscript/tests/test_vm.cpp` and `vm_expected.txt`: about 110 programs (every CLI case script among them) must
  give the recorded output, errors, error lines and globals. The first 85 were recorded from the tree walker before it
  was deleted and did not change in stage 2; the stage 2 ones were checked by hand when recorded. Listings check
  folding, fusing, slots, boxes and that every node has its own instructions.

## Variables

- A function's or lambda's locals live in slots of its frame: its parameters first (`Chunk::paramSlot`), then every
  name its body declares (`var`, typed and const declarations, `foreach`, `catch`; `CollectBodyLocals`). A call makes
  no scope map: it takes the slots from the thread's value stack and hangs them on the new scope.
- Each slot has a state byte: "not declared yet", or declared with its kind (`int`, `const`...). Before its
  declaration runs, a name reads and writes what it did before: an outer function's variable or a global.
- Scoping stays dynamic, as before: a function still sees the locals of the functions that called it. Lookup by name
  checks each scope's declared slots, then its map. So builtins, the host and called functions see slot variables.
- Top-level code has no slots: its declarations are globals, by name. `static` declarations are never slots.
- A by-name `Load` or `Store` remembers where it found its variable (`NameCache`), while `BasicRuntime::varEpoch` stays
  the same. The epoch changes when a scope opens or closes, a name or slot is first declared, a type rule changes, a
  static is set, or the host moves scopes (coroutine parking, through `BumpVars`). An epoch number is never reused.

## Calls

- A `Call` remembers the script function it found (`CallCache`), while `callEpoch`, the number of builtins and the
  number of natives stay the same, outside class methods. A remembered call goes straight to `Invoke` with the
  arguments on the operand stack.
- Inside a script, a call by name finds builtins and natives first, then script functions (unchanged). When the host
  calls an event by name (`CallFunction`: the engine's events, `+=` listeners that name a function), the script's own
  function wins over a builtin of the same name, such as `shake`.
- `return`, `break` and `continue` are jumps or a returned `Flow`. They are never C++ exceptions.

## Instructions

- Number maths and comparisons have a fast path that writes the result in place (`SetNumber` and `SetBool`).
- A number constant on the right of an operator is carried by the instruction (`binary_k`).
- A comparison that feeds an `if` or a loop test becomes one compare-and-jump.
- Constants are folded at compile time: number arithmetic and comparisons, with the runtime's 32-bit float rules
  (`x / 0` and `x % 0` give 0).
- `try`: `try_begin` records where the catch starts and the operand depth. An error raised inside the body (in this
  body or in anything it calls) goes there, with its message for the catch variable. A `break` or `continue` that
  leaves try bodies drops their handlers (`try_pop`).
- `switch`: the subject once, each case value in order (text compares as text, anything else as numbers), then the
  default; bodies fall through. `break` ends the switch; `continue` goes to the loop around it.
- An index with side effects is only worked out when the value can be indexed (`skip_unless_indexable`), as before.

## Language stage 2

What the language had before (checked in the parser and the tests): try/catch and throw of text, lambdas without
capture, plain classes with fields, methods and constructors, `for x in list`, switch, `$"..."` text, typed and const
variables, statics, named and default arguments, events. Added in stage 2, each with its own instructions:

- **Integers.** `okayscript::Value` has 64-bit whole numbers (`Value::Int`) next to the 32-bit float. A script makes
  one with a literal `123L` (`push_int`), `(long)x` or `long(x)` / `int64(x)` (text is read exactly). Maths with an
  Int on either side and a whole number on the other stays whole and exact: + - * wrap like two's complement, `/`
  truncates like C#'s long, `x / 0` and `x % 0` give 0 as for floats, comparisons and `==` are exact; anything with
  a fraction takes the float rules. JSON, `now()` and `file_size()` give Ints. Why opt-in and only the standalone
  value: the engine's value (`vs::VsValue`) is the visual-script value too, saved in scenes and graphs, and has no
  integer (adding one would touch every `AsFloat` caller in the engine); engine scripts and the recorded outputs
  depend on the float rules (`16777217` is `16777216`, `okayscript_engine_value`). So the core's integer paths are
  behind `detail::HasInt<V>`, false for `VsValue`: they compile away there, `123L` is the engine's float, and the
  float fast paths (`NumberArith`) are untouched (the bench did not move). `int x` still holds a float (an Int stays
  an Int in it).
- **Destructuring.** `var [a, b] = list`, `var {x, y} = map`, `var {x: px} = map`, `_` skips an item, and without
  `var` the same as assignments (`[a, b] = [b, a]`). One `unpack` per name (an item by index, or a key), read with the
  index rules (a missing one is 0).
- **Map literals.** `{"key": value, name: value}` (`make_map`). `{}` is still an empty list, as before.
- **Calling any value.** `f(1)(2)`, `fs[0](x)`, `(m.f)(a)`: a call on the value an expression gives (a method call
  named `Invoke`, which runs a lambda or a function value).
- **Closures, by reference.** Before, a lambda ran on the scopes of whoever called it (dynamic scoping), so a lambda
  returned from a function lost the function's locals. Now the compiler finds the locals a lambda inside a body uses
  (`CollectCapturedNames`): those get no slot but live by name in a box (a map) made for each call
  (`BasicEnvironment::env`, `Chunk::boxNames`). A lambda made there keeps the box (`BasicRuntime::closures`, by the
  lambda value's map, so printing a lambda still shows `{__lambda: id}`); a box's `__up` is the box the running lambda
  was made in, so nesting works. Lookup order: the frame's slots, its names, its box chain, then outwards as before
  (dynamic scoping is unchanged for everything else, so engine scripts behave the same). Locals that no lambda uses
  are still slots: no cost for them. A loop variable is one variable for the whole function, so lambdas made in a loop
  all see its last value (as `var` in JavaScript). Typed captured locals keep their type rule (`__k`). Limit: a lambda
  kept in a variable it captures (a recursive local lambda) holds its own box, so it is never freed (reference
  counting, no cycle collector).
- **Errors.** `throw` of any value (`ThrownError<V>` carries it): the catch variable is the value when it is not
  text, the message otherwise (as before). `catch (Error e)` / `catch (Exception e)` (any type word) gives an error
  object `{message, line, value}`. `finally` runs on every way out of the body: its code is emitted at the normal end,
  on the error path (a handler that keeps the error, then `rethrow`), and before each `return`, `break` or `continue`
  that leaves it (with the try handlers above it dropped first). `try { } finally { }` with no catch lets the error go
  on after the finally; `try { }` alone still drops it (the engine rule).
- **Generators and iterators.** A function or lambda with `yield value` (no `return`) is a generator: calling it
  returns `{__gen: id}` and runs nothing. `for x in gen()`, `g.next()` (0 once finished), `g.done()`, `g.to_list()`.
  At `gen_yield` the machine saves everything it has in flight (pc, operand stack, try handlers, pending errors,
  foreach iterators, loop counters: `GenRun`) and returns `Flow::Yield`; `Resume` puts it back and goes on. The frame's
  slots live in the generator, so they survive. `yield return x` is still the engine's coroutine wait. foreach also
  runs iterator objects: a class with `MoveNext()` and `Current` (a field, a property or a method), or with
  `GetEnumerator()` giving one (or giving a generator). A generator's state goes when no value refers to it (swept).
- **Modules.** `import "file.okay" [as name]` (`import`): the file runs once in a runtime of its own (its own globals
  and functions; the importer's natives, builtins, host and limits) and `name` (default: the file's stem) is a map of
  its globals and functions. `name.f(x)` calls the module's function in the module's runtime; a lambda made in a
  module carries `__mod` and runs there. Loaded once (cached by resolved path), an import cycle is an error that
  names the chain, a module that fails is not kept. The embedder decides how a path is found
  (`Interpreter::SetModuleLoader`): the CLI reads the file next to the importing file; the engine sets no loader, so
  `import` there is a runtime error (no file access from game scripts).
- **Strings.** `"""..."""` (many lines, no escapes, a line break right after the opening quotes dropped),
  `$"""...{x}"""`, C#'s `@"..."` (`""` is a quote). `format()` and `"...".format()` take format specs:
  `{0:.2f}`, `{:>8}`, `{:08.3f}`, `{:x}`, `{:,}`, `{:.0%}`, `{:e}`, `{{` and `}}` (`detail/Text.hpp`). Lower-case
  string methods: split, join (`", ".join(xs)`), replace, trim, trim_start, trim_end, find, contains, upper, lower,
  starts_with, ends_with, format, repeat, chars, reverse (the C#-style ones stay).
- **Classes.** `class Dog : Animal` where Animal is a plain class: fields and methods are inherited (a method found
  from the object's class up), `super(args)` runs the base's constructor and `super.M(args)` the base's method
  (`BasicRuntime::methodClass` is the class the running method is written in). A class with no constructors uses its
  base's. `static` methods (`Animal.Kind()`) and fields (`Animal.count`, or the bare name inside the class's
  methods), made the first time the class is used. Properties: `public int Hp { get { ... } set { ... value ... } }`
  and `get => expr;` (methods `get_Hp` / `set_Hp`). Operators: `public static V2 operator +(V2 a, V2 b)` (+ - * / %
  == != < > <= >=), used by the machine's slow path for any class object (`BinarySlow`). `ToString()` is used when a
  class object is printed by `print`, put in `$"..."` or added to text.
- **Script functions first (standalone).** The library has many builtins now (sum, map, sort, ...). In the standalone
  interpreter (`BasicInterpreter`) a script's own function is found before a builtin of the same name
  (`BasicRuntime::scriptFunctionsFirst`); natives, the embedder's, still come first. The engine keeps its rule
  (builtins first), as before.

## Library

The shared builtins are one copy in `okayscript/include/okayscript/CoreBuiltins.hpp`, installed by both the engine
(`OkayScriptVM_Math.cpp`, `_Input.cpp`, `_Network.cpp`) and the CLI (`InstallCoreLibrary`, `src/Stdlib.cpp`). The
engine's pure ones were moved there with their rules unchanged (and deleted from the engine): bits, JSON, strings,
lists, maps, the callback helpers, types, the maths helpers, random. New: trunc, log2, log10, cbrt, log(x, base),
long/int64, is_int, sort, zip, enumerate, flatten, the set_ functions (a set is a list with no item twice), ord, chr,
utf8_len, utf8_chars, base64 / hex / URL encode and decode, rand_seed, now, now_ms, clock, date_text, the JSON
\uXXXX escapes, exact whole numbers and an indent. Random numbers come from the embedder (`RandomSource`): the
engine's shared generator in the engine, our own SplitMix64 in the CLI. The callback helpers (map_fn, filter_fn, ...)
take a lambda as well as a function's name now.

What reaches outside the program is the CLI's only (`InstallSystemLibrary`): sleep, file_read, file_write,
file_append, file_exists, is_dir, file_delete, file_size, list_dir, make_dir, delete_dir, env; with `args`, exit
codes and import's loader. The engine never installs it: game scripts keep the access they had (their save and
prefs builtins, under the save folder), and no new file or process access.

Tests: `okayscript_lang_vm` (programs `ints` ... `lib_time`, listings "stage 2 instructions", "a generator body",
"a local a lambda captures"), `okayscript_engine_value` (the same features on the engine's value), CLI cases
`imports` (modules in `tests/okayscript_cli/modules/`) and `system`.
