// The OkayScript compiler: a body's syntax tree to bytecode (Bytecode.hpp). Written for this project from general
// code-generation ideas: each node emits its operands' code then its own instruction, jumps are patched once their
// target is known, and the operand stack depth is tracked to size the frame. Constants are folded with the
// runtime's own number rules (32-bit floats, x / 0 and x % 0 give 0). A function's locals get frame slots, worked
// out from its declarations before its code is made.
#include "okayscript/Bytecode.hpp"
#include <cstdint>
#include <cmath>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace okayscript {

namespace {

struct LoopTargets {
    std::vector<std::size_t> breaks;      ///< jumps to patch to the loop's end
    std::vector<std::size_t> continues;   ///< jumps to patch to the loop's continue point
    bool isSwitch = false;                ///< a switch: break ends it, continue belongs to the loop around it
    int tryDepth = 0;                     ///< try bodies open where it starts (a jump out leaves the ones inside)
};

const std::string kEmptyText;

class Compiler {
public:
    Chunk c;

    void Body(const StmtList& list) {
        List(list);
        Emit(Op::End);
    }
    /// A function or lambda: parameters take the first slots, then each name the body declares.
    /// A local a lambda inside uses gets no slot: it lives in the frame's box (Chunk::boxNames), by name.
    void Function(const std::vector<std::string>& params, const StmtList& body) {
        std::unordered_set<std::string> captured;
        CollectCapturedNames(params, body, captured);
        for (const std::string& p : params) {
            if (captured.count(p)) { c.paramSlot.push_back(-1); continue; }
            c.paramSlot.push_back(SlotOf(p, true));
        }
        std::vector<std::string> names(params.begin(), params.end());
        std::unordered_set<std::string> statics;
        CollectBodyLocals(body, names, statics);
        for (const std::string& n : names) {
            if (statics.count(n)) continue;
            if (captured.count(n)) { c.boxNames.push_back(n); continue; }
            SlotOf(n, true);
        }
        Body(body);
    }
    void OneExpression(const Expr& e) {
        Expression(e);
        Emit(Op::Return);
        Drop();
        Emit(Op::End);
    }

private:
    int depth = 0;
    int tryDepth = 0;
    std::vector<LoopTargets> loops;
    std::unordered_map<std::string, int> slots;
    /// The `finally` bodies around the code being compiled: a return, break or continue that leaves them runs each
    /// one first (innermost first), with the try handlers outside it.
    struct FinallyScope { int tryDepth; const StmtList* body; };
    std::vector<FinallyScope> finallies;

    /// Leave the finally bodies opened at try depth >= `downTo` (a jump or return out of them): drop the handlers
    /// above each one and run its code. Returns the try depth that is left.
    int LeaveFinallies(int downTo) {
        int cur = tryDepth;
        for (int i = (int)finallies.size() - 1; i >= 0 && finallies[(std::size_t)i].tryDepth >= downTo; --i) {
            const FinallyScope f = finallies[(std::size_t)i];
            if (cur > f.tryDepth) Emit(Op::TryPop, cur - f.tryDepth);
            cur = f.tryDepth;
            RunFinally(f, (std::size_t)i);
        }
        return cur;
    }
    /// The code of a finally body, compiled as if only the finallies outside it (below index `at`) were open.
    void RunFinally(const FinallyScope& f, std::size_t at) {
        std::vector<FinallyScope> saved(finallies.begin() + (std::ptrdiff_t)at, finallies.end());
        finallies.resize(at);
        const int savedDepth = tryDepth;
        tryDepth = f.tryDepth;
        List(*f.body);
        tryDepth = savedDepth;
        finallies.insert(finallies.end(), saved.begin(), saved.end());
    }
    /// Declare (or, for `[a, b] = ...`, assign) the name `n` from the value on top of the stack, popping it.
    void DeclareFromTop(const std::string& n, bool declare) {
        const int slot = SlotOf(n);
        if (declare) {
            if (slot >= 0) Emit(Op::DefineSlot, slot, &n);
            else Emit(Op::Define, 0, &n);
        } else {
            c.code[Emit(slot >= 0 ? Op::StoreSlotPop : Op::StorePop, slot, &n)].b = NewCache();
        }
        Drop();
    }

    /// The slot of a local name (made when `make`), or -1 for a name that is not a local of this body.
    int SlotOf(const std::string& n, bool make = false) {
        auto it = slots.find(n);
        if (it != slots.end()) return it->second;
        if (!make) return -1;
        const int i = (int)c.slotNames.size();
        c.slotNames.push_back(n);
        slots.emplace(n, i);
        return i;
    }

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
            case ExprKind::Number:
                if (static_cast<const NumberExpr&>(ex).isInt) return r;   // 123L: a whole number, pushed as one
                r.k = K::Num; r.n = (float)static_cast<const NumberExpr&>(ex).v; return r;
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
                if (e.dotted || e.thisPrefix) { Emit(Op::LoadQualified, 0, &e); Push(); return; }
                const int slot = SlotOf(e.n);
                c.code[Emit(slot >= 0 ? Op::LoadSlot : Op::Load, slot, &e.n)].b = NewCache();
                Push();
                return;
            }
            case ExprKind::Assign: {
                auto& e = static_cast<const AssignExpr&>(ex);
                Expression(*e.value);
                if (!e.plain) { Emit(Op::AssignQualified, 0, &e); return; }
                const int slot = SlotOf(e.n);
                c.code[Emit(slot >= 0 ? Op::StoreSlot : Op::Store, slot, &e.n)].b = NewCache();
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
                // The index is worked out only when the value can be indexed (a list or a map), as before.
                auto& e = static_cast<const IndexExpr&>(ex);
                Expression(*e.arr);
                std::size_t skipNull = 0;
                if (e.safe) skipNull = Emit(Op::JumpIfNullishNull);
                std::size_t skip = 0;
                const bool simple = SimpleIndex(*e.index);
                if (!simple) { skip = Emit(Op::SkipUnlessIndexable); c.code[skip].b = 1; c.code[skip].c = 0; }
                Expression(*e.index);
                Emit(Op::Index);
                Drop();
                const int end = Here();
                if (!simple) PatchTo(skip, end);
                if (e.safe) PatchTo(skipNull, end);
                return;
            }
            case ExprKind::IndexAssign: {
                auto& e = static_cast<const IndexAssignExpr&>(ex);
                Expression(*e.arr);
                Expression(*e.value);
                const bool simple = SimpleIndex(*e.index);
                std::size_t skip = 0;
                if (!simple) { skip = Emit(Op::SkipUnlessIndexable); c.code[skip].b = 2; c.code[skip].c = 1; }
                Expression(*e.index);   // the index is worked out last
                Emit(Op::IndexStore);
                Drop(2);
                if (!simple) PatchTo(skip, Here());
                return;
            }
            case ExprKind::IndexCompoundAssign: {
                auto& e = static_cast<const IndexCompoundAssignExpr&>(ex);
                Expression(*e.arr);
                Expression(*e.value);
                const std::size_t skip = Emit(Op::SkipUnlessIndexable);
                c.code[skip].b = 2;
                c.code[skip].c = 0;
                Expression(*e.index);
                Emit(Op::IndexCompound, 0, &e);
                Drop(2);
                PatchTo(skip, Here());
                return;
            }
            case ExprKind::MethodCall: {
                auto& e = static_cast<const MethodCallExpr&>(ex);
                Expression(*e.obj);
                std::size_t skip = 0;
                if (e.safe) skip = Emit(Op::JumpIfNullishNull);   // `v?.M(x)` skips its arguments when v is null
                for (auto& a : e.args) Expression(*a);
                Emit(Op::MethodCall, (int)e.args.size(), &e);
                Drop((int)e.args.size());
                if (e.safe) PatchTo(skip, Here());
                return;
            }
            case ExprKind::Call: {
                auto& e = static_cast<const CallExpr&>(ex);
                bool named = false;
                for (auto& n : e.argNames) if (!n.empty()) named = true;
                for (auto& a : e.args) Expression(*a);
                const std::size_t at = Emit(named ? Op::CallNamed : Op::Call, (int)e.args.size(), &e);
                if (!named) c.code[at].b = NewCall();
                Drop((int)e.args.size());
                Push();
                return;
            }
            case ExprKind::InterpString: {
                auto& e = static_cast<const InterpStringExpr&>(ex);
                Emit(Op::PushStr, 0, &kEmptyText);
                Push();
                for (auto& g : e.segs) {
                    if (!g.isExpr) { Emit(Op::AppendLit, 0, &g.lit); continue; }
                    Expression(*g.expr);
                    Emit(Op::AppendText);
                    Drop();
                }
                return;
            }
            case ExprKind::Coalesce: {
                auto& e = static_cast<const CoalesceExpr&>(ex);
                Expression(*e.l);
                const std::size_t j = Emit(Op::JumpUnlessNullishKeep);
                Drop();
                Expression(*e.r);
                PatchTo(j, Here());
                return;
            }
            case ExprKind::NullishAssign: {
                auto& e = static_cast<const NullishAssignExpr&>(ex);
                Emit(Op::GetName, 0, &e.n);
                Push();
                const std::size_t j = Emit(Op::JumpUnlessNullishKeep);
                Drop();
                Expression(*e.value);
                Emit(Op::AssignName, 0, &e.n);
                PatchTo(j, Here());
                return;
            }
            case ExprKind::Member: {
                auto& e = static_cast<const MemberExpr&>(ex);
                Expression(*e.obj);
                std::size_t skip = 0;
                if (e.safe) skip = Emit(Op::JumpIfNullishNull);
                Emit(Op::Member, 0, &e);
                if (e.safe) PatchTo(skip, Here());
                return;
            }
            case ExprKind::MemberAssign: {
                auto& e = static_cast<const MemberAssignExpr&>(ex);
                Expression(*e.obj);
                Expression(*e.value);
                Emit(Op::MemberSet, 0, &e);
                Drop();
                return;
            }
            case ExprKind::Cast: {
                auto& e = static_cast<const CastExpr&>(ex);
                Expression(*e.e);
                Emit(Op::Cast, 0, &e);
                return;
            }
            case ExprKind::Array: {
                auto& e = static_cast<const ArrayExpr&>(ex);
                for (auto& el : e.elems) Expression(*el);
                Emit(Op::MakeList, (int)e.elems.size());
                Drop((int)e.elems.size());
                Push();
                return;
            }
            case ExprKind::Map: {
                auto& e = static_cast<const MapExpr&>(ex);
                for (auto& v : e.values) Expression(*v);
                Emit(Op::MakeMap, (int)e.values.size(), &e.keys);
                Drop((int)e.values.size());
                Push();
                return;
            }
            case ExprKind::Lambda: Emit(Op::MakeLambda, 0, &ex); Push(); return;
            case ExprKind::New: {
                auto& e = static_cast<const NewExpr&>(ex);
                for (auto& a : e.args) Expression(*a);
                Emit(Op::New, (int)e.args.size(), &e);
                Drop((int)e.args.size());
                Push();
                return;
            }
            case ExprKind::EventOrCompound: {
                // `name += f`: f a lambda or a function name subscribes it; anything else is name = name op rhs.
                auto& e = static_cast<const EventOrCompoundExpr&>(ex);
                if (e.rhs->kind == ExprKind::Lambda) {
                    Expression(*e.rhs);
                    Emit(Op::Subscribe, 0, &e);
                    return;
                }
                std::size_t taken = 0;
                const bool byName = e.rhs->kind == ExprKind::Var;
                if (byName) taken = Emit(Op::EventVar, 0, &e);
                Emit(Op::CompoundCur, 0, &e);
                Push();
                Expression(*e.rhs);
                Emit(Op::Binary, 0, nullptr, e.bin);
                Drop();
                Emit(Op::CompoundStore, 0, &e);
                if (byName) PatchTo(taken, Here());
                return;
            }
            case ExprKind::Number:   // only 123L gets here (other numbers are folded above)
                Emit(Op::PushInt, 0, &static_cast<const NumberExpr&>(ex).iv);
                Push();
                return;
            case ExprKind::Bool: break;   // folded above
        }
        // Number and Bool are always folded above.
        c.code[Emit(Op::PushNum)].f = 0.0f;
        Push();
    }

    /// An expression whose value is not used (a statement, a for loop's step).
    void StepExpression(const Expr& e) {
        if (e.kind == ExprKind::Assign && static_cast<const AssignExpr&>(e).plain) {
            auto& a = static_cast<const AssignExpr&>(e);
            Expression(*a.value);
            const int slot = SlotOf(a.n);
            c.code[Emit(slot >= 0 ? Op::StoreSlotPop : Op::StorePop, slot, &a.n)].b = NewCache();
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
                Emit(Op::Line, st.line);
                if (s.kind & KStatic) {   // set once, by the first run of the source; later runs skip the initializer
                    const std::size_t skip = Emit(Op::StaticSetJump, 0, &s.n);
                    StartValue(s);
                    Emit(Op::DefineStatic, 0, &s);
                    Drop();
                    PatchTo(skip, Here());
                    return;
                }
                StartValue(s);
                const int slot = SlotOf(s.n);
                if (s.kind != KAny) Emit(Op::DefineTyped, slot, &s);
                else if (slot >= 0) Emit(Op::DefineSlot, slot, &s.n);
                else Emit(Op::Define, 0, &s.n);
                Drop();
                return;
            }
            case StmtKind::Throw: {
                auto& s = static_cast<const ThrowStmt&>(st);
                Emit(Op::Line, st.line);
                if (s.e) { Expression(*s.e); c.code[Emit(Op::Throw, st.line)].b = 1; Drop(); }
                else Emit(Op::Throw, st.line);
                return;
            }
            case StmtKind::Yield: {
                auto& s = static_cast<const YieldStmt&>(st);
                Emit(Op::Line, st.line);
                if (s.e) Expression(*s.e); else { Emit(Op::PushNull); Push(); }
                Emit(s.generator ? Op::GenYield : Op::Yield, st.line);
                if (s.generator) c.generator = true;
                Drop();
                return;
            }
            case StmtKind::Destructure: {
                // The value once; each name takes its item (list pattern) or key (map pattern).
                auto& s = static_cast<const DestructureStmt&>(st);
                Emit(Op::Line, st.line);
                Expression(*s.init);
                for (std::size_t i = 0; i < s.names.size(); ++i) {
                    if (s.names[i].empty()) continue;
                    Emit(Op::Unpack, (int)i, s.isMap ? &s.keys[i] : nullptr);
                    Push();
                    DeclareFromTop(s.names[i], s.declare);
                }
                Emit(Op::Pop);
                Drop();
                return;
            }
            case StmtKind::Import: {
                auto& s = static_cast<const ImportStmt&>(st);
                Emit(Op::Line, st.line);
                Emit(Op::Import, st.line, &s);
                Push();
                DeclareFromTop(s.alias, true);
                return;
            }
            case StmtKind::Try: {
                // An error in the body goes to the catch (its variable holds the message or the thrown value, or an
                // error object when typed), or is dropped without one (engine rule) unless there is a finally: then
                // the finally runs and the error goes on. The finally code runs on every way out (it is emitted at
                // each: the end, the error path, and before a return, break or continue that leaves the body).
                auto& s = static_cast<const TryStmt&>(st);
                Emit(Op::Line, st.line);
                const int outer = tryDepth;
                if (s.hasFinally) finallies.push_back({outer, &s.finallyB});
                const std::size_t begin = Emit(Op::TryBegin);
                const bool binds = s.hasCatch && !s.catchVar.empty();
                c.code[begin].b = binds ? (s.typedCatch ? 2 : 1) : (!s.hasCatch && s.hasFinally ? 3 : 0);
                ++tryDepth;
                List(s.body);
                --tryDepth;
                Emit(Op::TryEnd);
                std::vector<std::size_t> toEnd{Emit(Op::Jump)};
                PatchTo(begin, Here());
                if (s.hasCatch) {
                    if (binds) {
                        Push();   // what the handler pushes
                        DeclareFromTop(s.catchVar, true);
                    }
                    std::size_t inner = 0;
                    if (s.hasFinally) { inner = Emit(Op::TryBegin); c.code[inner].b = 3; ++tryDepth; }
                    List(s.catchB);
                    if (s.hasFinally) {
                        --tryDepth;
                        Emit(Op::TryEnd);
                        toEnd.push_back(Emit(Op::Jump));
                        PatchTo(inner, Here());   // an error in the catch: the finally, then the error goes on
                    }
                }
                if (s.hasFinally) {   // the error path: the finally, then the error goes on
                    finallies.pop_back();
                    List(s.finallyB);
                    Emit(Op::Rethrow);
                }
                const int end = Here();
                for (std::size_t j : toEnd) PatchTo(j, end);
                if (s.hasFinally) List(s.finallyB);   // the normal way out
                return;
            }
            case StmtKind::Switch: {
                // The subject once; each case's value in order until one matches (text compares as text, anything
                // else as numbers); else the default. Bodies fall through until a break.
                auto& s = static_cast<const SwitchStmt&>(st);
                Emit(Op::Line, st.line);
                Expression(*s.subject);
                std::vector<std::size_t> toBody(s.cases.size(), (std::size_t)-1);
                for (std::size_t i = 0; i < s.cases.size(); ++i) {
                    if (s.cases[i].isDefault) continue;
                    Expression(*s.cases[i].match);
                    toBody[i] = Emit(Op::SwitchCase);
                    Drop();
                }
                Emit(Op::Pop);
                Drop();
                const std::size_t toDefault = Emit(Op::Jump);
                int defaultAt = -1;
                loops.emplace_back();
                loops.back().isSwitch = true;
                loops.back().tryDepth = tryDepth;
                for (std::size_t i = 0; i < s.cases.size(); ++i) {
                    const int at = Here();
                    if (s.cases[i].isDefault && defaultAt < 0) defaultAt = at;
                    if (toBody[i] != (std::size_t)-1) PatchTo(toBody[i], at);
                    List(s.cases[i].body);
                }
                const int end = Here();
                PatchTo(toDefault, defaultAt >= 0 ? defaultAt : end);
                LoopTargets t = std::move(loops.back());
                loops.pop_back();
                for (std::size_t j : t.breaks) PatchTo(j, end);
                return;
            }
            case StmtKind::Return: {
                auto& s = static_cast<const ReturnStmt&>(st);
                Emit(Op::Line, st.line);
                if (s.e) { Expression(*s.e); LeaveFinallies(0); Emit(Op::Return); Drop(); }
                else { LeaveFinallies(0); Emit(Op::ReturnNone); }
                return;
            }
            case StmtKind::Break:
            case StmtKind::Continue: {
                // break: the nearest loop or switch; continue: the nearest loop (a switch passes it on).
                Emit(Op::Line, st.line);
                const bool brk = st.kind == StmtKind::Break;
                LoopTargets* target = nullptr;
                for (auto it = loops.rbegin(); it != loops.rend(); ++it)
                    if (brk || !it->isSwitch) { target = &*it; break; }
                if (!target) { Emit(Op::EndFlow, (int)(brk ? Flow::Break : Flow::Continue)); return; }
                const int left = LeaveFinallies(target->tryDepth);
                if (left > target->tryDepth) Emit(Op::TryPop, left - target->tryDepth);
                const std::size_t j = Emit(Op::Jump);
                (brk ? target->breaks : target->continues).push_back(j);
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
                OpenLoop();
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
                OpenLoop();
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
                // The collection once, the variable declared here, a list's length taken at the start
                // (items added while looping are not visited), a map's keys copied first. No iteration limit.
                auto& s = static_cast<const ForEachStmt&>(st);
                Emit(Op::Line, st.line);
                Expression(*s.iterable);
                const int it = c.iterators++;
                const int slot = SlotOf(s.var);
                c.code[Emit(Op::ForEachStart, it, &s)].c = slot;
                Drop();
                const int top = Here();
                const std::size_t next = Emit(Op::ForEachNext, it, &s);
                c.code[next].c = slot;
                OpenLoop();
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
                OpenLoop();
                List(s.body);
                const int step = Here();
                if (s.step) StepExpression(*s.step);
                Emit(Op::LoopCount, counter, "for loop");
                Emit(Op::Jump, top);
                EndLoop(Here(), step);
                if (s.cond) PatchTo(jf, Here());
                return;
            }
        }
    }

    /// The value a declaration starts with: its initializer, or its kind's starting value.
    void StartValue(const VarDeclStmt& s) {
        if (s.init) Expression(*s.init);
        else { Emit(Op::PushDefault, s.kind); Push(); }
    }
    void OpenLoop() {
        loops.emplace_back();
        loops.back().tryDepth = tryDepth;
    }
    void EndLoop(int end, int cont) {
        LoopTargets t = std::move(loops.back());
        loops.pop_back();
        for (std::size_t j : t.breaks) PatchTo(j, end);
        for (std::size_t j : t.continues) PatchTo(j, cont);
    }
};

const char* OpName(Op op) {
    switch (op) {
        case Op::Line: return "line"; case Op::PushNum: return "push_num"; case Op::PushInt: return "push_int"; case Op::PushBool: return "push_bool";
        case Op::PushStr: return "push_str"; case Op::PushNull: return "push_null"; case Op::PushDefault: return "push_default";
        case Op::Pop: return "pop"; case Op::Load: return "load"; case Op::Store: return "store";
        case Op::StorePop: return "store_pop"; case Op::LoadSlot: return "load_slot"; case Op::StoreSlot: return "store_slot";
        case Op::StoreSlotPop: return "store_slot_pop"; case Op::Define: return "define";
        case Op::DefineSlot: return "define_slot"; case Op::DefineTyped: return "define_typed";
        case Op::StaticSetJump: return "static_set_jump"; case Op::DefineStatic: return "define_static";
        case Op::LoadQualified: return "load_qualified"; case Op::AssignQualified: return "assign_qualified";
        case Op::GetName: return "get_name"; case Op::AssignName: return "assign_name";
        case Op::Binary: return "binary"; case Op::Unary: return "unary";
        case Op::ForEachStart: return "foreach_start"; case Op::ForEachNext: return "foreach_next";
        case Op::Index: return "index"; case Op::IndexStore: return "index_store";
        case Op::IndexCompound: return "index_compound"; case Op::Member: return "member";
        case Op::MemberSet: return "member_set"; case Op::Cast: return "cast";
        case Op::MethodCall: return "method_call"; case Op::CallNamed: return "call_named";
        case Op::MakeList: return "make_list"; case Op::MakeMap: return "make_map"; case Op::MakeLambda: return "make_lambda"; case Op::New: return "new";
        case Op::AppendLit: return "append_lit"; case Op::AppendText: return "append_text";
        case Op::EventVar: return "event_var"; case Op::Subscribe: return "subscribe";
        case Op::CompoundCur: return "compound_cur"; case Op::CompoundStore: return "compound_store";
        case Op::SwitchCase: return "switch_case"; case Op::Throw: return "throw"; case Op::TryBegin: return "try_begin";
        case Op::TryEnd: return "try_end"; case Op::TryPop: return "try_pop"; case Op::Yield: return "yield";
        case Op::Rethrow: return "rethrow"; case Op::GenYield: return "gen_yield"; case Op::Unpack: return "unpack";
        case Op::Import: return "import";
        case Op::JumpIfNullishNull: return "jump_if_nullish_null";
        case Op::JumpUnlessNullishKeep: return "jump_unless_nullish_keep";
        case Op::SkipUnlessIndexable: return "skip_unless_indexable";
        case Op::BinaryK: return "binary_k"; case Op::CompareJump: return "compare_jump";
        case Op::CompareJumpK: return "compare_jump_k";
        case Op::ToBool: return "to_bool"; case Op::Jump: return "jump"; case Op::JumpIfFalse: return "jump_if_false";
        case Op::JumpIfTrue: return "jump_if_true"; case Op::JumpIfFalseKeep: return "jump_if_false_keep";
        case Op::JumpIfTrueKeep: return "jump_if_true_keep"; case Op::Call: return "call";
        case Op::Return: return "return"; case Op::ReturnNone: return "return_none";
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
ChunkPtr CompileFunction(const std::vector<std::string>& params, const StmtList& body) {
    Compiler comp;
    comp.Function(params, body);
    return std::make_shared<const Chunk>(std::move(comp.c));
}
ChunkPtr CompileExpr(const Expr& e) {
    Compiler comp;
    comp.OneExpression(e);
    return std::make_shared<const Chunk>(std::move(comp.c));
}

std::string Disassemble(const Chunk& c) {
    std::ostringstream o;
    for (std::size_t i = 0; i < c.code.size(); ++i) {
        const Instr& in = c.code[i];
        o << i << ' ' << OpName(in.op);
        switch (in.op) {
            case Op::PushNum: o << ' ' << in.f; break;
            case Op::PushInt: o << ' ' << *static_cast<const std::int64_t*>(in.p); break;
            case Op::PushBool: o << ' ' << (in.a ? "true" : "false"); break;
            case Op::PushStr: case Op::Load: case Op::Store: case Op::StorePop: case Op::Define: case Op::GetName:
            case Op::AssignName: case Op::AppendLit:
                o << ' ' << *static_cast<const std::string*>(in.p); break;
            case Op::LoadSlot: case Op::StoreSlot: case Op::StoreSlotPop: case Op::DefineSlot:
                o << ' ' << in.a << ' ' << *static_cast<const std::string*>(in.p); break;
            case Op::StaticSetJump: o << ' ' << *static_cast<const std::string*>(in.p) << ' ' << in.a; break;
            case Op::DefineTyped: o << ' ' << in.a << ' ' << static_cast<const VarDeclStmt*>(in.p)->n; break;
            case Op::DefineStatic: o << ' ' << static_cast<const VarDeclStmt*>(in.p)->n; break;
            case Op::LoadQualified: o << ' ' << static_cast<const VarExpr*>(in.p)->n; break;
            case Op::AssignQualified: o << ' ' << static_cast<const AssignExpr*>(in.p)->n; break;
            case Op::Member: o << ' ' << static_cast<const MemberExpr*>(in.p)->name; break;
            case Op::MemberSet: o << ' ' << static_cast<const MemberAssignExpr*>(in.p)->name; break;
            case Op::Cast: o << ' ' << static_cast<const CastExpr*>(in.p)->type; break;
            case Op::New: o << ' ' << static_cast<const NewExpr*>(in.p)->type << '/' << in.a; break;
            case Op::EventVar: case Op::Subscribe: case Op::CompoundCur: case Op::CompoundStore:
                o << ' ' << static_cast<const EventOrCompoundExpr*>(in.p)->name;
                if (in.op == Op::EventVar) o << ' ' << in.a;
                break;
            case Op::Binary: case Op::Unary: o << ' ' << TokName(in.tok); break;
            case Op::BinaryK: o << ' ' << TokName(in.tok) << ' ' << in.f; break;
            case Op::CompareJump: o << ' ' << TokName(in.tok) << ' ' << in.a; break;
            case Op::CompareJumpK: o << ' ' << TokName(in.tok) << ' ' << in.f << ' ' << in.a; break;
            case Op::Call: case Op::CallNamed: o << ' ' << static_cast<const CallExpr*>(in.p)->name << '/' << in.a; break;
            case Op::MethodCall: o << ' ' << static_cast<const MethodCallExpr*>(in.p)->name << '/' << in.a; break;
            case Op::Line: case Op::Jump: case Op::JumpIfFalse: case Op::JumpIfTrue: case Op::JumpIfFalseKeep:
            case Op::JumpIfTrueKeep: case Op::EndFlow: case Op::LoopInit: case Op::LoopCount: case Op::PushDefault:
            case Op::JumpIfNullishNull: case Op::JumpUnlessNullishKeep: case Op::SwitchCase: case Op::TryPop:
            case Op::MakeList: case Op::MakeMap: case Op::Throw: case Op::Yield: case Op::GenYield:
                o << ' ' << in.a; break;
            case Op::Unpack:
                o << ' ' << in.a;
                if (in.p) o << ' ' << *static_cast<const std::string*>(in.p);
                break;
            case Op::Import: o << ' ' << static_cast<const ImportStmt*>(in.p)->path; break;
            case Op::TryBegin: case Op::SkipUnlessIndexable: o << ' ' << in.a << ' ' << in.b; break;
            case Op::ForEachStart: case Op::ForEachNext: o << ' ' << in.a << ' ' << in.b << ' ' << in.c; break;
            default: break;
        }
        o << '\n';
    }
    return o.str();
}

} // namespace okayscript
