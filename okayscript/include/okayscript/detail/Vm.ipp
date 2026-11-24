#pragma once
// The OkayScript stack machine: runs a Chunk (Bytecode.hpp) for any value type V. Written for this project. Each
// instruction does what the tree walker (detail/Eval.ipp) does for the node it came from, with the same helpers, so a
// program gives the same results either way. Calls, return, break and continue are plain control flow, no C++
// exceptions; a runtime error is still a RuntimeError, caught where the walker's are.
#include "okayscript/detail/Runtime.ipp"
#include <cmath>
#include <memory>

namespace okayscript {

namespace detail {

/// The operand stacks of the running chunks on one thread. A chunk takes its slots on entry and gives them back on
/// exit (strictly nested), and slots never move, so a pointer to them stays good while the chunk calls out.
template <class V> class ValueStack {
public:
    V* Take(std::size_t n) {
        if (n == 0) n = 1;
        if (m_segs.empty()) m_segs.push_back(Seg(n));
        Seg* s = &m_segs[m_cur];
        if (s->used + n > s->size) {
            if (s->used != 0) ++m_cur;   // this segment stays in use: go on to the next one
            if (m_cur == m_segs.size()) m_segs.push_back(Seg(n));
            else if (m_segs[m_cur].size < n) m_segs[m_cur] = Seg(n);
            s = &m_segs[m_cur];
        }
        V* p = s->v.get() + s->used;
        s->used += n;
        return p;
    }
    void Give(V* p, std::size_t n) {
        if (n == 0) n = 1;
        for (std::size_t i = 0; i < n; ++i) p[i] = V{};   // drop what the slots still hold (lists, objects)
        m_segs[m_cur].used -= n;
        if (m_segs[m_cur].used == 0 && m_cur > 0) --m_cur;
    }
    static ValueStack& ForThisThread() {
        thread_local ValueStack s;
        return s;
    }

private:
    struct Seg {
        explicit Seg(std::size_t need) : size(need > 1024 ? need : 1024), v(new V[size]) {}
        std::size_t size;
        std::unique_ptr<V[]> v;
        std::size_t used = 0;
    };
    std::vector<Seg> m_segs;
    std::size_t m_cur = 0;
};

template <class V> struct StackSlots {
    ValueStack<V>& st;
    V* p;
    std::size_t n;
    ~StackSlots() { st.Give(p, n); }
};

/// Number maths on two numbers, the part of Arith (Eval.ipp) the hot loops use. False: not handled here.
template <class V> inline bool NumberArith(Tok op, float x, float y, V& out) {
    switch (op) {
        case Tok::Plus: out.SetNumber(x + y); return true;
        case Tok::Minus: out.SetNumber(x - y); return true;
        case Tok::Star: out.SetNumber(x * y); return true;
        case Tok::Slash: out.SetNumber(y != 0 ? x / y : 0.0f); return true;
        case Tok::Percent: out.SetNumber(y != 0 ? std::fmod(x, y) : 0.0f); return true;
        case Tok::Lt: out.SetBool(x < y); return true;
        case Tok::Gt: out.SetBool(x > y); return true;
        case Tok::Le: out.SetBool(x <= y); return true;
        case Tok::Ge: out.SetBool(x >= y); return true;
        case Tok::Eq: out.SetBool(x == y); return true;
        case Tok::Ne: out.SetBool(x != y); return true;
        default: return false;
    }
}
/// A comparison of two numbers (the fused compare-and-jump).
inline bool NumberCompare(Tok op, float x, float y) {
    switch (op) {
        case Tok::Lt: return x < y;
        case Tok::Gt: return x > y;
        case Tok::Le: return x <= y;
        case Tok::Ge: return x >= y;
        case Tok::Eq: return x == y;
        default: return x != y;
    }
}
/// Copy a value into a stack slot or a variable, numbers without the general copy.
template <class V> inline void CopyValue(V& to, const V& from) {
    if (const auto* f = from.NumberPtr()) to.SetNumber(*f);
    else to = from;
}

template <class V> V Arith(Tok op, const V& a, const V& b);

} // namespace detail

template <class V> Flow BasicRuntime<V>::RunChunk(const Chunk& c) {
    auto& stack = detail::ValueStack<V>::ForThisThread();
    detail::StackSlots<V> slots{stack, stack.Take((std::size_t)c.maxStack), (std::size_t)(c.maxStack ? c.maxStack : 1)};
    V* const bp = slots.p;
    long long fixedCounters[8];
    std::unique_ptr<long long[]> moreCounters;
    long long* counters = fixedCounters;
    if (c.loops > 8) { moreCounters.reset(new long long[(std::size_t)c.loops]); counters = moreCounters.get(); }
    NameCache* const caches = c.caches.data();
    const Instr* const code = c.code.data();
    int sp = 0;
    int pc = 0;
    for (;;) {
        const Instr& in = code[pc++];
        switch (in.op) {
            case Op::Line: curLine = in.a; break;
            case Op::PushNum: bp[sp++].SetNumber(in.f); break;
            case Op::PushBool: bp[sp++].SetBool(in.a != 0); break;
            case Op::PushStr: bp[sp++] = V{*static_cast<const std::string*>(in.p)}; break;
            case Op::PushNull: bp[sp++] = V{}; break;
            case Op::Pop: --sp; break;
            case Op::Load: {
                NameCache& nc = caches[in.b];
                if (nc.epoch == varEpoch) { detail::CopyValue(bp[sp++], *static_cast<const V*>(nc.value)); break; }
                // As the walker reads a plain name (ExprKind::Var).
                const std::string& n = *static_cast<const std::string*>(in.p);
                if (const V* p = Lookup(n, &nc)) { bp[sp++] = *p; break; }
                if (!selfClass.empty()) {
                    if (n == "this" && detail::IsClassInstance(self)) bp[sp++] = self;
                    else bp[sp++] = Get(n);   // a field of the object a method runs on
                    break;
                }
                V v;
                if (hooks && hooks->ReadName(*this, n, v)) { bp[sp++] = std::move(v); break; }
                bp[sp++] = V{};   // never set: 0
                break;
            }
            case Op::Store:
            case Op::StorePop: {
                NameCache& nc = caches[in.b];
                V& v = bp[sp - 1];
                if (nc.epoch == varEpoch && nc.writable) {
                    V& to = *static_cast<V*>(nc.value);
                    if (const auto* f = v.NumberPtr()) to.SetNumber(*f);
                    else if (in.op == Op::StorePop) to = std::move(v);
                    else to = v;
                    if (in.op == Op::StorePop) --sp;
                    break;
                }
                AssignCached(*static_cast<const std::string*>(in.p), v, nc);
                if (in.op == Op::StorePop) --sp;
                break;
            }
            case Op::Define:
                Define(*static_cast<const std::string*>(in.p), bp[sp - 1]);
                --sp;
                break;
            case Op::Binary: {
                V& a = bp[sp - 2];
                const V& b = bp[sp - 1];
                const auto* x = a.NumberPtr();
                const auto* y = b.NumberPtr();
                if (!(x && y && detail::NumberArith<V>(in.tok, *x, *y, a))) a = detail::Arith<V>(in.tok, a, b);
                --sp;
                break;
            }
            case Op::BinaryK: {
                V& a = bp[sp - 1];
                const auto* x = a.NumberPtr();
                if (!(x && detail::NumberArith<V>(in.tok, *x, in.f, a))) a = detail::Arith<V>(in.tok, a, V{in.f});
                break;
            }
            case Op::CompareJump: {
                sp -= 2;
                const V& a = bp[sp];
                const V& b = bp[sp + 1];
                const auto* x = a.NumberPtr();
                const auto* y = b.NumberPtr();
                const bool yes = x && y ? detail::NumberCompare(in.tok, *x, *y) : detail::Arith<V>(in.tok, a, b).AsBool();
                if (!yes) pc = in.a;
                break;
            }
            case Op::CompareJumpK: {
                const V& a = bp[--sp];
                const auto* x = a.NumberPtr();
                const bool yes = x ? detail::NumberCompare(in.tok, *x, in.f) : detail::Arith<V>(in.tok, a, V{in.f}).AsBool();
                if (!yes) pc = in.a;
                break;
            }
            case Op::Unary: {
                V& v = bp[sp - 1];
                if (in.tok == Tok::Minus) {
                    if (v.IsVec3()) v = V{v.AsVec3() * -1.0f};
                    else v.SetNumber(-v.AsFloat());
                } else if (in.tok == Tok::Not) v.SetBool(!v.AsBool());
                break;
            }
            case Op::ToBool: { V& v = bp[sp - 1]; if (!v.IsBool()) v.SetBool(v.AsBool()); break; }
            case Op::Jump: pc = in.a; break;
            case Op::JumpIfFalse: --sp; if (!bp[sp].AsBool()) pc = in.a; break;
            case Op::JumpIfTrue: --sp; if (bp[sp].AsBool()) pc = in.a; break;
            case Op::JumpIfFalseKeep: if (!bp[sp - 1].AsBool()) pc = in.a; else --sp; break;
            case Op::JumpIfTrueKeep: if (bp[sp - 1].AsBool()) pc = in.a; else --sp; break;
            case Op::Call: {
                auto& e = *static_cast<const CallExpr*>(in.p);
                const int argc = in.a;
                CallCache& cc = c.calls[(std::size_t)in.b];
                if (cc.fn && cc.epoch == callEpoch && cc.builtins == builtins.size() && cc.natives == natives.size() &&
                    selfClass.empty()) {
                    // The same script function as last time: no lookups, no argument list.
                    V r = Invoke(*cc.fn, bp + sp - argc, (std::size_t)argc, nullptr);
                    sp -= argc;
                    bp[sp++] = std::move(r);
                    break;
                }
                if (selfClass.empty() && !builtins.count(e.name) && !natives.count(e.name)) {
                    auto f = functions.find(e.name);
                    if (f != functions.end()) {
                        cc.fn = f->second;
                        cc.epoch = callEpoch;
                        cc.builtins = builtins.size();
                        cc.natives = natives.size();
                    }
                }
                std::vector<V> vals;
                vals.reserve((std::size_t)argc);
                for (int i = sp - argc; i < sp; ++i) vals.push_back(std::move(bp[i]));
                sp -= argc;
                bp[sp] = Call(e.name, vals, nullptr, e.line);
                ++sp;
                break;
            }
            case Op::Index: {
                V& a = bp[sp - 2];
                const V& i = bp[sp - 1];
                a = IndexRead(a, [&i]() -> const V& { return i; });
                --sp;
                break;
            }
            case Op::IndexStore: {
                const V& i = bp[sp - 1];
                IndexWrite(bp[sp - 3], bp[sp - 2], [&i]() -> const V& { return i; });
                bp[sp - 3] = std::move(bp[sp - 2]);
                sp -= 2;
                break;
            }
            case Op::MethodCall: {
                auto& e = *static_cast<const MethodCallExpr*>(in.p);
                const int argc = in.a;
                std::vector<V> vals;
                vals.reserve((std::size_t)argc);
                for (int k = sp - argc; k < sp; ++k) vals.push_back(std::move(bp[k]));
                sp -= argc;
                V r = CallMethodOn(bp[sp - 1], e, vals);
                bp[sp - 1] = std::move(r);
                break;
            }
            case Op::Eval: bp[sp] = Eval(*static_cast<const Expr*>(in.p)); ++sp; break;
            case Op::Exec: {
                const Flow f = Run(*static_cast<const Stmt*>(in.p));
                if (f == Flow::Normal) break;
                if (f == Flow::Return) return f;
                const int target = f == Flow::Break ? in.a : in.b;
                if (target < 0) return f;   // no loop around it in this body
                pc = target;
                break;
            }
            case Op::Return: retVal = std::move(bp[--sp]); return Flow::Return;
            case Op::ReturnNone: retVal = V{}; return Flow::Return;
            case Op::EndFlow: return (Flow)in.a;
            case Op::LoopInit: counters[in.a] = 0; break;
            case Op::LoopCount: CountIteration(counters[in.a], static_cast<const char*>(in.p)); break;
            case Op::End: return Flow::Normal;
        }
    }
}

} // namespace okayscript
