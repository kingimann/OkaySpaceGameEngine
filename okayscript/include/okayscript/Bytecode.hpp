#pragma once
// OkayScript bytecode: a function, lambda or top-level body compiled for the stack machine in detail/Vm.ipp.
//
// Written for this project from general compiler design (code generation over a syntax tree, an operand stack, jump
// targets patched once known); no other VM's code or instruction set was used. Variables stay in the runtime's scopes
// (dynamic scoping, as before): an instruction that names a variable keeps a small cache of where it found it, which
// is thrown away whenever a scope opens or closes or a variable is declared (BasicRuntime::varEpoch). A node the
// compiler has no instruction for yet runs through the tree walker (Op::Eval / Op::Exec), so every program runs.
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
    Pop,           ///< drop the top value
    Load,          ///< push the plain variable *(const std::string*)p (cache b)
    Store,         ///< assign the top value to the plain variable p (cache b), keep it on the stack
    StorePop,      ///< the same, and drop it (an assignment used as a statement)
    Define,        ///< `var p = <top>`: declare in the current scope, drop the value
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
    Index,         ///< pop the index and a; push a[index]
    IndexStore,    ///< pop the index, the value and a; a[index] = value; push the value
    MethodCall,    ///< (const MethodCallExpr*)p on the value under its a arguments
    Call,          ///< call (const CallExpr*)p with the top a values as its positional arguments (call cache b)
    Eval,          ///< push the value of the expression (const Expr*)p, worked out by the tree walker
    Exec,          ///< run the statement (const Stmt*)p with the tree walker; its break goes to a, continue to b
    Return,        ///< pop into retVal and end the body with Flow::Return
    ReturnNone,    ///< retVal = 0 and end with Flow::Return
    EndFlow,       ///< end the body with Flow a (a stray break or continue: no loop around it here)
    ForEachStart,  ///< pop a list or map into iterator a; declare the loop variable p ((const ForEachStmt*) in f's place: b)
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
    int loops = 0;       ///< loop counters it needs
    int iterators = 0;   ///< foreach iterators it needs
    mutable std::vector<NameCache> caches;
    mutable std::vector<CallCache> calls;
};

/// Compile a body. Never fails: what has no instruction yet runs through the tree walker.
ChunkPtr CompileBody(const StmtList& body);

/// The compiled code of a body, compiled the first time.
inline const Chunk& CodeFor(const StmtList& body, ChunkPtr& slot) {
    if (!slot) slot = CompileBody(body);
    return *slot;
}

/// A readable listing of a chunk, one instruction per line (for tests and debugging).
std::string Disassemble(const Chunk& c);

} // namespace okayscript
