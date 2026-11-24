# OkayScript bytecode machine

> **What this is:** the design note for running OkayScript as bytecode on a stack machine.
>
> **Status:** internal record. Branch `claude/okayscript-vm-1` (2026-11-24). Every function, lambda, top-level body,
> default argument and field starting value runs as bytecode. This is the only way OkayScript runs: the tree walker
> is deleted (rule 19d).

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
- `okayscript/tests/test_vm.cpp` and `vm_expected.txt`: about 85 programs (every CLI case script among them) must give
  the recorded output, errors, error lines and globals. The outputs were recorded from the tree walker before it was
  deleted. Listings check folding, fusing, slots and that every node has its own instructions.

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
