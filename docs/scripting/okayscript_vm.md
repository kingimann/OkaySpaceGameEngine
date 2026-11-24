# OkayScript bytecode machine

> **What this is:** the design note for running OkayScript as bytecode on a stack machine.
>
> **Status:** internal record. Work in progress on branch `claude/okayscript-vm-1`, local only (2026-11-24).
> Function, lambda and top-level bodies run as bytecode by default. Some nodes still run through the tree walker
> (`okayscript/include/okayscript/detail/Eval.ipp`). The walker is deleted once no node needs it (rule 19c).

This was written from general compiler and VM design: code generation over the syntax tree, an operand stack, and
jump targets patched once they are known. No code or instruction set was taken from another VM (rule 19a).

## Files

- `okayscript/include/okayscript/Bytecode.hpp`: the instructions, `Chunk` and the per-instruction caches.
- `okayscript/src/Compile.cpp`: the compiler and `Disassemble`. The compiler does not depend on the value type.
- `okayscript/include/okayscript/detail/Vm.ipp`: `BasicRuntime<V>::RunChunk`.
- `FunctionDecl::code`, `LambdaExpr::code` and `Program::topCode` hold the chunk. It is compiled the first time
  the body runs and is freed with the syntax tree.
- `okayscript/tests/test_vm.cpp`: every program gives the walker's exact output, errors, error lines and globals.
  This includes every CLI case script. Folding and fusing are checked on listings.

## Variables

- Variables stay in the runtime's scopes, by name, with dynamic scoping as before. The walker, builtins and the
  host see the same variables.
- A `Load` or `Store` remembers where it found its variable (`NameCache`). That memory is used while
  `BasicRuntime::varEpoch` stays the same.
- The epoch changes in these cases:
  - a scope opens or closes;
  - a new name is declared;
  - a type rule changes;
  - a static is set;
  - the host moves scopes (coroutine parking, through `BumpVars`).
- An epoch number is never reused, even across runtimes.
- Not done yet: slots for locals. Each call still makes a scope map, and reads after a call look the name up again.

## Calls

- A `Call` remembers the script function it found (`CallCache`). That memory is used while three things stay the
  same: `callEpoch`, the number of builtins and the number of natives. The call must also not be inside a class
  method.
- `callEpoch` changes when the functions are defined again (`BumpCalls`). This happens in `Interpreter::Parse` and
  in the engine's `Load`.
- A remembered call goes straight to `Invoke` with the arguments on the operand stack. It makes no argument list and
  does no lookups.
- `return`, `break` and `continue` are jumps or a returned `Flow`. They are never C++ exceptions.

## Instructions

- Number maths and comparisons have a fast path. It writes the result in place (`SetNumber` and `SetBool` on both
  value types).
- A number constant on the right of an operator is carried by the instruction (`binary_k`).
- A comparison that feeds an `if` or a loop test becomes one compare-and-jump.
- Constants are folded at compile time: number arithmetic and comparisons, with the runtime's 32-bit float rules
  (`x / 0` and `x % 0` give 0).

## Still run by the walker

- Expressions: dotted and `this.` names, named arguments, `?.` method calls, an index that is not a name or a
  constant, interpolated text, members, lambdas, `new`, casts, compound and nullish assignments.
- Statements: typed, const and static declarations, `switch`, `try`, `throw` and `yield`.
- A walker statement inside a compiled loop still reaches that loop with its `break` and `continue`.
