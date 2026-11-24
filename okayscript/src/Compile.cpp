// The OkayScript compiler: a body's syntax tree to bytecode (Bytecode.hpp). Written for this project from general
// code-generation ideas: each node emits its operands' code then its own instruction, jumps are patched once their
// target is known, and the operand stack depth is tracked to size the frame. Constants are folded with the
// runtime's own number rules (32-bit floats, x / 0 and x % 0 give 0).
#include "okayscript/Bytecode.hpp"
#include <cmath>
#include <sstream>

namespace okayscript {

namespace {

struct LoopTargets {
    std::vector<std::size_t> breaks;      ///< jumps to patch to the loop's end
    std::vector<std::size_t> continues;   ///< jumps to patch to the loop's continue point
};

class Compiler {
public:
    Chunk c;

    void Body(const StmtList& list) {
        List(list);
        Emit(Op::End);
    }

private:
    int depth = 0;
    std::vector<LoopTargets> loops;

    std::size_t Emit(Op op, int a = 0, const void* p = nullptr, Tok tok = Tok::End) {
        Instr in;
        in.op = op; in.a = a; in.p = p; in.tok = tok;
        c.code.push_back(in);
        return c.code.size() - 1;
    }
    void Push(int n = 1) { depth += n; if (depth > c.maxStack) c.maxStack = depth; }
    void Drop(int n = 1) { depth -= n; }
    int lastLabel = -1;   ///< the latest position a jump goes to: no instruction there may be fused away
    int Here() { lastLabel = (int)c.code.size(); return lastLabel; }
    static bool IsCompare(Tok t) {
        return t == Tok::Lt || t == Tok::Gt || t == Tok::Le || t == Tok::Ge || t == Tok::Eq || t == Tok::Ne;
    }
    /// Pop the test value and jump when it is false. A comparison just before it becomes one compare-and-jump.
    std::size_t JumpIfFalse() {
        Drop();
        if (!c.code.empty() && lastLabel != (int)c.code.size() && IsCompare(c.code.back().tok)) {
            Instr& last = c.code.back();
            if (last.op == Op::Binary) { last.op = Op::CompareJump; return c.code.size() - 1; }
            if (last.op == Op::BinaryK) { last.op = Op::CompareJumpK; return c.code.size() - 1; }
        }
        return Emit(Op::JumpIfFalse);
    }
    int NewCall() { c.calls.emplace_back(); return (int)c.calls.size() - 1; }
    void PatchTo(std::size_t at, int target) { c.code[at].a = target; }
    int NewCache() { c.caches.emplace_back(); return (int)c.caches.size() - 1; }

    // ---- Constant folding: numbers only, with the runtime's rules (detail::Arith on two numbers) ----
    enum class K { None, Num, Bool };
    struct Const { K k = K::None; float n = 0; bool b = false; };
    static Const Fold(const Expr& ex) {
        Const r;
        switch (ex.kind) {
            case ExprKind::Number: r.k = K::Num; r.n = (float)static_cast<const NumberExpr&>(ex).v; return r;
            case ExprKind::Bool: r.k = K::Bool; r.b = static_cast<const BoolExpr&>(ex).v; return r;
            case ExprKind::Unary: {
                auto& e = static_cast<const UnaryExpr&>(ex);
                const Const x = Fold(*e.e);
                if (x.k == K::Num && e.op == Tok::Minus) { r.k = K::Num; r.n = -x.n; }
                return r;
            }
            case ExprKind::Binary: {
                auto& e = static_cast<const BinaryExpr&>(ex);
                const Const a = Fold(*e.l), b = Fold(*e.r);
                if (a.k != K::Num || b.k != K::Num) return r;
                const float x = a.n, y = b.n;
                r.k = K::Num;
                switch (e.op) {
                    case Tok::Plus: r.n = x + y; return r;
                    case Tok::Minus: r.n = x - y; return r;
                    case Tok::Star: r.n = x * y; return r;
                    case Tok::Slash: r.n = y != 0 ? x / y : 0.0f; return r;
                    case Tok::Percent: r.n = y != 0 ? std::fmod(x, y) : 0.0f; return r;
                    default: break;
                }
                r.k = K::Bool;
                switch (e.op) {
                    case Tok::Lt: r.b = x < y; return r;
                    case Tok::Gt: r.b = x > y; return r;
                    case Tok::Le: r.b = x <= y; return r;
                    case Tok::Ge: r.b = x >= y; return r;
                    case Tok::Eq: r.b = x == y; return r;
                    case Tok::Ne: r.b = x != y; return r;
                    default: break;
                }
                r.k = K::None;
                return r;
            }
            default: return r;
        }
    }

    static bool SimpleIndex(const Expr& e) {
        if (e.kind == ExprKind::Number || e.kind == ExprKind::String) return true;
        if (e.kind != ExprKind::Var) return false;
        auto& v = static_cast<const VarExpr&>(e);
        return !v.dotted && !v.thisPrefix;
    }

    // ---- Expressions: leave exactly one value on the stack ----
    void Expression(const Expr& ex) {
        const Const k = Fold(ex);
        if (k.k == K::Num) { c.code[Emit(Op::PushNum)].f = k.n; Push(); return; }
        if (k.k == K::Bool) { Emit(Op::PushBool, k.b ? 1 : 0); Push(); return; }
        switch (ex.kind) {
            case ExprKind::String: Emit(Op::PushStr, 0, &static_cast<const StringExpr&>(ex).v); Push(); return;
            case ExprKind::Var: {
                auto& e = static_cast<const VarExpr&>(ex);
                if (e.dotted || e.thisPrefix) break;
                c.code[Emit(Op::Load, 0, &e.n)].b = NewCache();
                Push();
                return;
            }
            case ExprKind::Assign: {
                auto& e = static_cast<const AssignExpr&>(ex);
                if (!e.plain) break;
                Expression(*e.value);
                c.code[Emit(Op::Store, 0, &e.n)].b = NewCache();
                return;
            }
            case ExprKind::Unary: {
                auto& e = static_cast<const UnaryExpr&>(ex);
                Expression(*e.e);
                Emit(Op::Unary, 0, nullptr, e.op);
                return;
            }
            case ExprKind::Binary: {
                auto& e = static_cast<const BinaryExpr&>(ex);
                Expression(*e.l);
                const Const k = Fold(*e.r);
                if (k.k == K::Num) {   // x op number: no push for the constant
                    c.code[Emit(Op::BinaryK, 0, nullptr, e.op)].f = k.n;
                    return;
                }
                Expression(*e.r);
                Emit(Op::Binary, 0, nullptr, e.op);
                Drop();
                return;
            }
            case ExprKind::Logical: {
                // a && b: false when a is false (b not worked out), else b as true/false. a || b likewise.
                auto& e = static_cast<const LogicalExpr&>(ex);
                Expression(*e.l);
                Emit(Op::ToBool);
                const std::size_t j = Emit(e.op == Tok::And ? Op::JumpIfFalseKeep : Op::JumpIfTrueKeep);
                Drop();
                Expression(*e.r);
                Emit(Op::ToBool);
                PatchTo(j, Here());
                return;
            }
            case ExprKind::Ternary: {
                auto& e = static_cast<const TernaryExpr&>(ex);
                Expression(*e.cond);
                const std::size_t jf = JumpIfFalse();
                Expression(*e.a);
                const std::size_t je = Emit(Op::Jump);
                Drop();
                PatchTo(jf, Here());
                Expression(*e.b);
                PatchTo(je, Here());
                return;
            }
            case ExprKind::Index: {
                // Only a side-effect-free index (a name or a constant), so working it out first changes nothing.
                auto& e = static_cast<const IndexExpr&>(ex);
                if (e.safe || !SimpleIndex(*e.index)) break;
                Expression(*e.arr);
                Expression(*e.index);
                Emit(Op::Index);
                Drop();
                return;
            }
            case ExprKind::IndexAssign: {
                auto& e = static_cast<const IndexAssignExpr&>(ex);
                if (!SimpleIndex(*e.index)) break;
                Expression(*e.arr);
                Expression(*e.value);
                Expression(*e.index);   // the walker works the index out last too
                Emit(Op::IndexStore);
                Drop(2);
                return;
            }
            case ExprKind::MethodCall: {
                auto& e = static_cast<const MethodCallExpr&>(ex);
                if (e.safe) break;   // `v?.M(x)` skips its arguments when v is null: the walker
                Expression(*e.obj);
                for (auto& a : e.args) Expression(*a);
                Emit(Op::MethodCall, (int)e.args.size(), &e);
                Drop((int)e.args.size());
                return;
            }
            case ExprKind::Call: {
                auto& e = static_cast<const CallExpr&>(ex);
                for (auto& n : e.argNames) if (!n.empty()) goto walker;   // named arguments: the walker
                for (auto& a : e.args) Expression(*a);
                c.code[Emit(Op::Call, (int)e.args.size(), &e)].b = NewCall();
                Drop((int)e.args.size());
                Push();
                return;
            }
            default: break;
        }
    walker:
        Emit(Op::Eval, 0, &ex);
        Push();
    }

    /// An expression whose value is not used (a statement, a for loop's step).
    void StepExpression(const Expr& e) {
        if (e.kind == ExprKind::Assign && static_cast<const AssignExpr&>(e).plain) {
            auto& a = static_cast<const AssignExpr&>(e);
            Expression(*a.value);
            c.code[Emit(Op::StorePop, 0, &a.n)].b = NewCache();
            Drop();
            return;
        }
        Expression(e);
        Emit(Op::Pop);
        Drop();
    }

    // ---- Statements: leave the stack as they found it ----
    void List(const StmtList& list) { for (auto& s : list) Statement(*s); }

    void Statement(const Stmt& st) {
        switch (st.kind) {
            case StmtKind::Expr:
                Emit(Op::Line, st.line);
                StepExpression(*static_cast<const ExprStmt&>(st).e);
                return;
            case StmtKind::VarDecl: {
                auto& s = static_cast<const VarDeclStmt&>(st);
                if (s.kind != KAny) break;   // typed, const and static declarations: the walker
                Emit(Op::Line, st.line);
                if (s.init) Expression(*s.init); else { Emit(Op::PushNull); Push(); }
                Emit(Op::Define, 0, &s.n);
                Drop();
                return;
            }
            case StmtKind::Return: {
                auto& s = static_cast<const ReturnStmt&>(st);
                Emit(Op::Line, st.line);
                if (s.e) { Expression(*s.e); Emit(Op::Return); Drop(); }
                else Emit(Op::ReturnNone);
                return;
            }
            case StmtKind::Break:
            case StmtKind::Continue: {
                Emit(Op::Line, st.line);
                const bool brk = st.kind == StmtKind::Break;
                if (loops.empty()) { Emit(Op::EndFlow, (int)(brk ? Flow::Break : Flow::Continue)); return; }
                const std::size_t j = Emit(Op::Jump);
                (brk ? loops.back().breaks : loops.back().continues).push_back(j);
                return;
            }
            case StmtKind::Block: Emit(Op::Line, st.line); List(static_cast<const BlockStmt&>(st).body); return;
            case StmtKind::Multi: Emit(Op::Line, st.line); List(static_cast<const MultiStmt&>(st).list); return;
            case StmtKind::If: {
                auto& s = static_cast<const IfStmt&>(st);
                Emit(Op::Line, st.line);
                Expression(*s.cond);
                const std::size_t jf = JumpIfFalse();
                List(s.thenB);
                if (s.elseB.empty()) { PatchTo(jf, Here()); return; }
                const std::size_t je = Emit(Op::Jump);
                PatchTo(jf, Here());
                List(s.elseB);
                PatchTo(je, Here());
                return;
            }
            case StmtKind::While: {
                auto& s = static_cast<const WhileStmt&>(st);
                Emit(Op::Line, st.line);
                const int counter = c.loops++;
                Emit(Op::LoopInit, counter);
                const int top = Here();
                Expression(*s.cond);
                const std::size_t jf = JumpIfFalse();
                loops.emplace_back();
                List(s.body);
                const int count = Here();
                Emit(Op::LoopCount, counter, "while loop");
                Emit(Op::Jump, top);
                EndLoop(Here(), count);
                PatchTo(jf, Here());
                return;
            }
            case StmtKind::DoWhile: {
                auto& s = static_cast<const DoWhileStmt&>(st);
                Emit(Op::Line, st.line);
                const int counter = c.loops++;
                Emit(Op::LoopInit, counter);
                const int top = Here();
                loops.emplace_back();
                List(s.body);
                const int count = Here();
                Emit(Op::LoopCount, counter, "do-while loop");
                Expression(*s.cond);
                Emit(Op::JumpIfTrue, top);
                Drop();
                EndLoop(Here(), count);
                return;
            }
            case StmtKind::ForEach: {
                // As the walker: the collection once, the variable declared here, a list's length taken at the start
                // (items added while looping are not visited), a map's keys copied first. No iteration limit.
                auto& s = static_cast<const ForEachStmt&>(st);
                Emit(Op::Line, st.line);
                Expression(*s.iterable);
                const int it = c.iterators++;
                Emit(Op::ForEachStart, it, &s);
                Drop();
                const int top = Here();
                const std::size_t next = Emit(Op::ForEachNext, it, &s);
                loops.emplace_back();
                List(s.body);
                Emit(Op::Jump, top);
                EndLoop(Here(), top);
                c.code[next].b = Here();
                return;
            }
            case StmtKind::For: {
                auto& s = static_cast<const ForStmt&>(st);
                Emit(Op::Line, st.line);
                if (s.init) Statement(*s.init);
                const int counter = c.loops++;
                Emit(Op::LoopInit, counter);
                const int top = Here();
                std::size_t jf = 0;
                if (s.cond) { Expression(*s.cond); jf = JumpIfFalse(); }
                loops.emplace_back();
                List(s.body);
                const int step = Here();
                if (s.step) StepExpression(*s.step);
                Emit(Op::LoopCount, counter, "for loop");
                Emit(Op::Jump, top);
                EndLoop(Here(), step);
                if (s.cond) PatchTo(jf, Here());
                return;
            }
            default: break;
        }
        // Everything else runs through the tree walker. Its break and continue reach the loop around it here.
        const std::size_t at = Emit(Op::Exec, -1, &st);
        c.code[at].b = -1;
        if (!loops.empty()) {
            loops.back().breaks.push_back(at);
            c.code[at].b = -2;   // patched in EndLoop (marks "continue goes to the loop" too)
        }
    }

    void EndLoop(int end, int cont) {
        LoopTargets t = std::move(loops.back());
        loops.pop_back();
        for (std::size_t j : t.breaks) {
            if (c.code[j].op == Op::Exec) { c.code[j].a = end; c.code[j].b = cont; }
            else PatchTo(j, end);
        }
        for (std::size_t j : t.continues) PatchTo(j, cont);
    }
};

const char* OpName(Op op) {
    switch (op) {
        case Op::Line: return "line"; case Op::PushNum: return "push_num"; case Op::PushBool: return "push_bool";
        case Op::PushStr: return "push_str"; case Op::PushNull: return "push_null"; case Op::Pop: return "pop";
        case Op::Load: return "load"; case Op::Store: return "store"; case Op::StorePop: return "store_pop";
        case Op::Define: return "define"; case Op::Binary: return "binary"; case Op::Unary: return "unary";
        case Op::ForEachStart: return "foreach_start"; case Op::ForEachNext: return "foreach_next";
        case Op::Index: return "index"; case Op::IndexStore: return "index_store";
        case Op::MethodCall: return "method_call";
        case Op::BinaryK: return "binary_k"; case Op::CompareJump: return "compare_jump";
        case Op::CompareJumpK: return "compare_jump_k";
        case Op::ToBool: return "to_bool"; case Op::Jump: return "jump"; case Op::JumpIfFalse: return "jump_if_false";
        case Op::JumpIfTrue: return "jump_if_true"; case Op::JumpIfFalseKeep: return "jump_if_false_keep";
        case Op::JumpIfTrueKeep: return "jump_if_true_keep"; case Op::Call: return "call"; case Op::Eval: return "eval";
        case Op::Exec: return "exec"; case Op::Return: return "return"; case Op::ReturnNone: return "return_none";
        case Op::EndFlow: return "end_flow"; case Op::LoopInit: return "loop_init"; case Op::LoopCount: return "loop_count";
        case Op::End: return "end";
    }
    return "?";
}

} // namespace

ChunkPtr CompileBody(const StmtList& body) {
    Compiler comp;
    comp.Body(body);
    return std::make_shared<const Chunk>(std::move(comp.c));
}

std::string Disassemble(const Chunk& c) {
    std::ostringstream o;
    for (std::size_t i = 0; i < c.code.size(); ++i) {
        const Instr& in = c.code[i];
        o << i << ' ' << OpName(in.op);
        switch (in.op) {
            case Op::PushNum: o << ' ' << in.f; break;
            case Op::PushBool: o << ' ' << (in.a ? "true" : "false"); break;
            case Op::PushStr: case Op::Load: case Op::Store: case Op::StorePop: case Op::Define:
                o << ' ' << *static_cast<const std::string*>(in.p); break;
            case Op::Binary: case Op::Unary: o << ' ' << TokName(in.tok); break;
            case Op::BinaryK: o << ' ' << TokName(in.tok) << ' ' << in.f; break;
            case Op::CompareJump: o << ' ' << TokName(in.tok) << ' ' << in.a; break;
            case Op::CompareJumpK: o << ' ' << TokName(in.tok) << ' ' << in.f << ' ' << in.a; break;
            case Op::Call: o << ' ' << static_cast<const CallExpr*>(in.p)->name << '/' << in.a; break;
            case Op::MethodCall: o << ' ' << static_cast<const MethodCallExpr*>(in.p)->name << '/' << in.a; break;
            case Op::Line: case Op::Jump: case Op::JumpIfFalse: case Op::JumpIfTrue: case Op::JumpIfFalseKeep:
            case Op::JumpIfTrueKeep: case Op::EndFlow: case Op::LoopInit: case Op::LoopCount: o << ' ' << in.a; break;
            case Op::Exec: o << ' ' << in.a << ' ' << in.b; break;
            default: break;
        }
        o << '\n';
    }
    return o.str();
}

} // namespace okayscript
