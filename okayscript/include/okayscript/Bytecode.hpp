#pragma once
// OkayScript bytecode: a function, lambda or top-level body compiled for the stack machine in detail/Vm.ipp.
//
// Written for this project from general compiler design (code generation over a syntax tree, an operand stack, jump
// targets patched once known); no other VM's code or instruction set was used. Variables stay in the runtime's scopes
// (dynamic scoping, as before): an instruction that names a variable keeps a small cache of where it found it, which
// is thrown away whenever a scope opens or closes or a variable is declared (BasicRuntime::varEpoch). A function's
// or lambda's own locals live in slots of its frame instead, and are still found by name from the functions it calls
// (dynamic scoping). Every node has instructions: this is the only way OkayScript runs.
// Design note: docs/scripting/okayscript_vm.md.
#include "okayscript/Ast.hpp"
#include <memory>
#include <string>
#include <vector>

namespace okayscript {

enum class Op : unsigned char {
    Line,          ///< curLine = a (the start of a statement)
    PushNum,       ///< push the number f
    PushBool,      ///< push a != 0
    PushStr,       ///< push the text *(const std::string*)p
    PushNull,      ///< push 0 (an unset value)
    PushDefault,   ///< push the starting value of a variable of kind a (DefaultForKind)
    Pop,           ///< drop the top value
    Load,          ///< push the variable *(const std::string*)p, found by name (cache b)
    Store,         ///< assign the top value to the variable p by name (cache b), keep it on the stack
    StorePop,      ///< the same, and drop it (an assignment used as a statement)
    LoadSlot,      ///< push local slot a; while the slot is not declared yet, as Load (name p, cache b)
    StoreSlot,     ///< assign the top value to local slot a (its type rule applies); while not declared, as Store
    StoreSlotPop,  ///< the same, and drop it
    Define,        ///< `var p = <top>` by name in the current scope (top-level code), drop the value
    DefineSlot,    ///< `var <slot a> = <top>`: declare local slot a, drop the value
    DefineTyped,   ///< a typed or const declaration (const VarDeclStmt*)p of the top value: slot a, or by name when a < 0
    StaticSetJump, ///< go to a when the static (const std::string*)p is already set (its initializer does not run again)
    DefineStatic,  ///< `static` declaration (const VarDeclStmt*)p of the top value, drop it
    LoadQualified, ///< push a dotted or `this.` name ((const VarExpr*)p)
    AssignQualified,   ///< assign the top value to a dotted or `this.` name ((const AssignExpr*)p), keep it
    GetName,       ///< push the variable or field *(const std::string*)p (no host names), for `x ??= v`
    AssignName,    ///< assign the top value to the name *(const std::string*)p, keep it
    Binary,        ///< a op b (op = tok), both popped, the result pushed
    BinaryK,       ///< top op f (a number constant on the right), in place
    CompareJump,   ///< pop b and a; go to a when `a tok b` is false (a comparison and an if/loop test, fused)
    CompareJumpK,  ///< pop a; go to a when `a tok f` is false
    Unary,         ///< -x / !x / +x (op = tok)
    ToBool,        ///< the top value as true/false
    Jump,          ///< go to a
    JumpIfFalse,   ///< pop; go to a when it is false
    JumpIfTrue,    ///< pop; go to a when it is true
    JumpIfFalseKeep,   ///< go to a when the top is false, keeping it; else pop it
    JumpIfTrueKeep,    ///< go to a when the top is true, keeping it; else pop it
    JumpIfNullishNull, ///< when the top is null (IsNullish), make it 0 and go to a (`?.`)
    JumpUnlessNullishKeep, ///< go to a when the top is not null, keeping it; else pop it (`??`, `??=`)
    SkipUnlessIndexable,   ///< the value b down is not a list or map: drop b values, push the top one (c == 1) or
                           ///< 0, and go to a (an index is then never worked out, as before)
    Index,         ///< pop the index and a; push a[index]
    IndexStore,    ///< pop the index, the value and a; a[index] = value; push the value
    IndexCompound, ///< pop the index, the value and a; a[index] op= value ((const IndexCompoundAssignExpr*)p); push the result
    Member,        ///< the top value's member (const MemberExpr*)p
    MemberSet,     ///< pop the value and the target; target.name = value ((const MemberAssignExpr*)p); push the value
    Cast,          ///< (type)top ((const CastExpr*)p)
    MethodCall,    ///< (const MethodCallExpr*)p on the value under its a arguments
    Call,          ///< call (const CallExpr*)p with the top a values as its positional arguments (call cache b)
    CallNamed,     ///< call (const CallExpr*)p with the top a values, some of them named
    MakeList,      ///< pop a values into a new list
    MakeLambda,    ///< push the lambda (const LambdaExpr*)p
    New,           ///< `new` (const NewExpr*)p with the top a values
    AppendLit,     ///< append the text *(const std::string*)p to the text on top
    AppendText,    ///< pop a value and append it as text to the text under it
    EventVar,      ///< `name += f` / `-= f` ((const EventOrCompoundExpr*)p) with f a lambda variable or a function
                   ///< name: subscribe, push it and go to a; else go on (plain compound assignment follows)
    Subscribe,     ///< subscribe / unsubscribe the top value for the event of (const EventOrCompoundExpr*)p, keep it
    CompoundCur,   ///< push the current value of the name `name op= rhs` changes ((const EventOrCompoundExpr*)p)
    CompoundStore, ///< store the top value back into that name, keep it
    SwitchCase,    ///< pop a case value; when it matches the subject under it, pop the subject too and go to a
    Throw,         ///< throw the top value's text as an error on line a (b == 0: "error", nothing on the stack)
    TryBegin,      ///< an error until the matching TryEnd goes to a (b == 1: its message pushed first)
    TryEnd,        ///< the try body ended normally: forget its handler
    TryPop,        ///< leave a try bodies (a break or continue out of them)
    Yield,         ///< pop the value and hand it to the host's coroutine (line a)
    Return,        ///< pop into retVal and end the body with Flow::Return
    ReturnNone,    ///< retVal = 0 and end with Flow::Return
    EndFlow,       ///< end the body with Flow a (a stray break or continue: no loop around it here)
    ForEachStart,  ///< pop a list or map into iterator a; declare the loop variable (slot c, or by name)
                   ///< ((const ForEachStmt*)p)
    ForEachNext,   ///< iterator a's next item into the loop variable, or go to b when there is none
    LoopInit,      ///< loop counter a = 0
    LoopCount,     ///< count one iteration of loop a ((const char*)p names it in the error)
    End            ///< the end of the body: Flow::Normal
};

struct Instr {
    Op op = Op::End;
    Tok tok = Tok::End;
    int a = 0;
    int b = 0;
    float f = 0.0f;
    int c = -1;
    const void* p = nullptr;
};

/// Where a Load/Store found its variable, valid while the runtime's varEpoch is the same.
struct NameCache {
    unsigned long long epoch = 0;
    void* value = nullptr;   ///< the variable's value (a V in one of the scopes)
    bool writable = false;   ///< no type or const rule on it: a Store may write straight through
};

/// What a Call found last time: a script function, while no function was (re)defined (BasicRuntime::callEpoch) and
/// no builtin or native was added (their counts), outside a class method.
struct CallCache {
    unsigned long long epoch = 0;
    std::size_t builtins = 0, natives = 0;
    const FunctionDecl* fn = nullptr;
};

struct Chunk {
    std::vector<Instr> code;
    int maxStack = 0;    ///< operand stack slots it needs
    /// The body's local variables, one slot each: parameters first (paramSlot), then every name a declaration in the
    /// body gives (var, a typed declaration, foreach, catch). A lambda's body is its own chunk. Top-level code has none.
    std::vector<std::string> slotNames;
    std::vector<int> paramSlot;   ///< parameter i lives in slot paramSlot[i]
    int loops = 0;       ///< loop counters it needs
    int iterators = 0;   ///< foreach iterators it needs
    mutable std::vector<NameCache> caches;
    mutable std::vector<CallCache> calls;
};

/// Compile top-level code (no local slots: its declarations are globals).
ChunkPtr CompileBody(const StmtList& body);
/// Compile a function or lambda body with these parameters: its locals get slots.
ChunkPtr CompileFunction(const std::vector<std::string>& params, const StmtList& body);
/// Compile one expression (a default argument, a field's starting value): the code returns its value.
ChunkPtr CompileExpr(const Expr& e);

/// The compiled code of a body, compiled the first time.
inline const Chunk& CodeFor(const StmtList& body, ChunkPtr& slot) {
    if (!slot) slot = CompileBody(body);
    return *slot;
}
inline const Chunk& CodeFor(const std::vector<std::string>& params, const StmtList& body, ChunkPtr& slot) {
    if (!slot) slot = CompileFunction(params, body);
    return *slot;
}

/// A readable listing of a chunk, one instruction per line (for tests and debugging).
std::string Disassemble(const Chunk& c);

} // namespace okayscript
