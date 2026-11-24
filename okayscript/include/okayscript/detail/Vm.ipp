#pragma once
// The OkayScript stack machine: runs a Chunk (Bytecode.hpp) for any value type V. Written for this project. It is the
// only way OkayScript runs: each instruction does what its node means (the helpers in detail/Eval.ipp). Calls,
// return, break and continue are plain control flow, no C++ exceptions; a runtime error is a RuntimeError, and one
// raised inside a try body goes to its catch through the handler TryBegin set up.
#include "okayscript/detail/Runtime.ipp"
#include <cmath>
#include <memory>

namespace okayscript {

namespace detail {

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

/// One running foreach: the collection, where it is, and a map's keys.
template <class V> struct ForEachState {
    V coll;
    std::size_t i = 0, n = 0;
    std::vector<std::string> keys;
};

} // namespace detail

template <class V> Flow BasicRuntime<V>::RunChunk(const Chunk& c, V* const slots, unsigned char* const state) {
    auto& stack = detail::ValueStack<V>::ForThisThread();
    detail::StackSlots<V> frameSlots{stack, stack.Take((std::size_t)c.maxStack), (std::size_t)(c.maxStack ? c.maxStack : 1)};
    V* const bp = frameSlots.p;
    long long fixedCounters[8];
    std::unique_ptr<long long[]> moreCounters;
    long long* counters = fixedCounters;
    if (c.loops > 8) { moreCounters.reset(new long long[(std::size_t)c.loops]); counters = moreCounters.get(); }
    std::unique_ptr<detail::ForEachState<V>[]> iters;
    if (c.iterators) iters.reset(new detail::ForEachState<V>[(std::size_t)c.iterators]);
    // try bodies open now: where their catch starts, the stack depth to go back to, and whether it takes the message
    struct Handler { int pc, sp; bool message; };
    std::vector<Handler> handlers;
    NameCache* const caches = c.caches.data();
    const Instr* const code = c.code.data();
    int sp = 0;
    int pc = 0;
    // A local's declaration: the slot holds v from now on, with no type rule (as `var` always was).
    auto declare = [&](int i, V&& v) {
        slots[i] = std::move(v);
        if (state[i] != kDeclared) { state[i] = kDeclared; BumpVars(); }
    };
    // An assignment to a local slot: its type rule, or by name while its declaration has not run.
    auto assignSlot = [&](int i, const V& v, const std::string& name) {
        if (state[i] == kDeclared) slots[i] = v;
        else if (state[i] & kDeclared) detail::StoreTypedSlot<V>(slots[i], state[i], v, name);
        else Assign(name, v);
    };
    for (;;) {
        try {
            for (;;) {
                const Instr& in = code[pc++];
                switch (in.op) {
                    case Op::Line: curLine = in.a; break;
                    case Op::PushNum: bp[sp++].SetNumber(in.f); break;
                    case Op::PushBool: bp[sp++].SetBool(in.a != 0); break;
                    case Op::PushStr: bp[sp++] = V{*static_cast<const std::string*>(in.p)}; break;
                    case Op::PushNull: bp[sp++] = V{}; break;
                    case Op::PushDefault: bp[sp++] = DefaultForKind<V>((unsigned char)in.a); break;
                    case Op::Pop: --sp; break;
                    case Op::Load: {
                        NameCache& nc = caches[in.b];
                        if (nc.epoch == varEpoch) { detail::CopyValue(bp[sp++], *static_cast<const V*>(nc.value)); break; }
                        LoadName(*static_cast<const std::string*>(in.p), &nc, bp[sp++]);
                        break;
                    }
                    case Op::LoadSlot:
                        if (state[in.a] & kDeclared) { detail::CopyValue(bp[sp++], slots[in.a]); break; }
                        LoadName(*static_cast<const std::string*>(in.p), &caches[in.b], bp[sp++]);   // not declared yet
                        break;
                    case Op::Store:
                    case Op::StorePop: {
                        NameCache& nc = caches[in.b];
                        V& v = bp[sp - 1];
                        if (nc.epoch == varEpoch && nc.writable) {
                            V& to = *static_cast<V*>(nc.value);
                            if (const auto* f = v.NumberPtr()) to.SetNumber(*f);
                            else if (in.op == Op::StorePop) to = std::move(v);
                            else to = v;
                        } else {
                            AssignCached(*static_cast<const std::string*>(in.p), v, nc);
                        }
                        if (in.op == Op::StorePop) --sp;
                        break;
                    }
                    case Op::StoreSlot:
                    case Op::StoreSlotPop: {
                        V& v = bp[sp - 1];
                        if (state[in.a] == kDeclared) {
                            V& to = slots[in.a];
                            if (const auto* f = v.NumberPtr()) to.SetNumber(*f);
                            else if (in.op == Op::StoreSlotPop) to = std::move(v);
                            else to = v;
                        } else if (state[in.a] & kDeclared) {
                            detail::StoreTypedSlot<V>(slots[in.a], state[in.a], v, *static_cast<const std::string*>(in.p));
                        } else {
                            AssignCached(*static_cast<const std::string*>(in.p), v, caches[in.b]);
                        }
                        if (in.op == Op::StoreSlotPop) --sp;
                        break;
                    }
                    case Op::Define:
                        Define(*static_cast<const std::string*>(in.p), bp[sp - 1]);
                        --sp;
                        break;
                    case Op::DefineSlot: --sp; declare(in.a, std::move(bp[sp])); break;
                    case Op::DefineTyped: {
                        auto& s = *static_cast<const VarDeclStmt*>(in.p);
                        const V& v = bp[--sp];
                        if (in.a < 0) { DefineTyped(s.n, v, s.kind); break; }
                        slots[in.a] = (s.kind & 0x0F) ? CoerceToKind<V>(s.kind, v, s.n) : v;
                        state[in.a] = (unsigned char)(kDeclared | (s.kind & 0x1F));
                        BumpVars();
                        break;
                    }
                    case Op::StaticSetJump:
                        if (statics && statics->vars.count(*static_cast<const std::string*>(in.p))) pc = in.a;
                        break;
                    case Op::DefineStatic: {
                        auto& s = *static_cast<const VarDeclStmt*>(in.p);
                        --sp;
                        DefineStatic(s.n, bp[sp], (unsigned char)(s.kind & ~KStatic));
                        break;
                    }
                    case Op::LoadQualified: bp[sp] = ReadQualified(*static_cast<const VarExpr*>(in.p)); ++sp; break;
                    case Op::AssignQualified: WriteQualified(static_cast<const AssignExpr*>(in.p)->n, bp[sp - 1]); break;
                    case Op::GetName: bp[sp] = Get(*static_cast<const std::string*>(in.p)); ++sp; break;
                    case Op::AssignName: Assign(*static_cast<const std::string*>(in.p), bp[sp - 1]); break;
                    case Op::Binary: {
                        V& a = bp[sp - 2];
                        V& b = bp[sp - 1];
                        if (in.tok == Tok::Plus) {
                            if (std::string* s = a.StringPtr()) {   // text + anything: append in place (JoinText's rule)
                                if (const std::string* t = b.StringPtr()) { detail::CheckTextLength(s->size() + t->size()); s->append(*t); }
                                else { const std::string t2 = b.AsString(); detail::CheckTextLength(s->size() + t2.size()); s->append(t2); }
                                --sp;
                                break;
                            }
                        }
                        const auto* x = a.NumberPtr();
                        const auto* y = b.NumberPtr();
                        if (!(x && y && detail::NumberArith<V>(in.tok, *x, *y, a))) a = detail::Arith<V>(in.tok, a, b);
                        --sp;
                        break;
                    }
                    case Op::BinaryK: {
                        V& a = bp[sp - 1];
                        if (in.tok == Tok::Plus) {
                            if (std::string* s = a.StringPtr()) {
                                const std::string t = V{in.f}.AsString();
                                detail::CheckTextLength(s->size() + t.size());
                                s->append(t);
                                break;
                            }
                        }
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
                    case Op::JumpIfNullishNull: if (IsNullish(bp[sp - 1])) { bp[sp - 1] = V{}; pc = in.a; } break;
                    case Op::JumpUnlessNullishKeep: if (!IsNullish(bp[sp - 1])) pc = in.a; else --sp; break;
                    case Op::SkipUnlessIndexable: {
                        const V& a = bp[sp - in.b];
                        if (a.AsArray() || a.AsMap()) break;
                        V r = in.c == 1 ? std::move(bp[sp - 1]) : V{};
                        sp -= in.b;
                        bp[sp++] = std::move(r);
                        pc = in.a;
                        break;
                    }
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
                    case Op::CallNamed: {
                        auto& e = *static_cast<const CallExpr*>(in.p);
                        std::vector<V> vals;
                        std::vector<std::string> names;
                        vals.reserve((std::size_t)in.a);
                        for (int i = 0; i < in.a; ++i) {
                            names.push_back(i < (int)e.argNames.size() ? e.argNames[(std::size_t)i] : std::string());
                            vals.push_back(std::move(bp[sp - in.a + i]));
                        }
                        sp -= in.a;
                        bp[sp] = Call(e.name, vals, &names, e.line);
                        ++sp;
                        break;
                    }
                    case Op::Index: {
                        V& a = bp[sp - 2];
                        V r = IndexRead(a, bp[sp - 1]);
                        a = std::move(r);
                        --sp;
                        break;
                    }
                    case Op::IndexStore: {
                        IndexWrite(bp[sp - 3], bp[sp - 2], bp[sp - 1]);
                        bp[sp - 3] = std::move(bp[sp - 2]);
                        sp -= 2;
                        break;
                    }
                    case Op::IndexCompound: {
                        V r = IndexCompound(bp[sp - 3], bp[sp - 2], bp[sp - 1], *static_cast<const IndexCompoundAssignExpr*>(in.p));
                        sp -= 3;
                        bp[sp++] = std::move(r);
                        break;
                    }
                    case Op::Member: {
                        V r = ReadMember(bp[sp - 1], *static_cast<const MemberExpr*>(in.p));
                        bp[sp - 1] = std::move(r);
                        break;
                    }
                    case Op::MemberSet: {
                        WriteMember(bp[sp - 2], *static_cast<const MemberAssignExpr*>(in.p), bp[sp - 1]);
                        bp[sp - 2] = std::move(bp[sp - 1]);
                        --sp;
                        break;
                    }
                    case Op::Cast: bp[sp - 1] = CastValue(static_cast<const CastExpr*>(in.p)->type, bp[sp - 1]); break;
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
                    case Op::MakeList: {
                        V v = V::MakeArray();
                        auto arr = v.AsArray();
                        arr->reserve((std::size_t)in.a);
                        for (int k = sp - in.a; k < sp; ++k) arr->push_back(std::move(bp[k]));
                        sp -= in.a;
                        bp[sp++] = std::move(v);
                        break;
                    }
                    case Op::MakeLambda: bp[sp] = MakeLambda(*static_cast<const LambdaExpr*>(in.p)); ++sp; break;
                    case Op::New: {
                        std::vector<V> vals;
                        vals.reserve((std::size_t)in.a);
                        for (int k = sp - in.a; k < sp; ++k) vals.push_back(std::move(bp[k]));
                        sp -= in.a;
                        bp[sp] = MakeInstance(static_cast<const NewExpr*>(in.p)->type, vals);
                        ++sp;
                        break;
                    }
                    case Op::AppendLit: {
                        std::string* s = bp[sp - 1].StringPtr();
                        const std::string& t = *static_cast<const std::string*>(in.p);
                        detail::CheckTextLength(s->size() + t.size());
                        s->append(t);
                        break;
                    }
                    case Op::AppendText: {
                        const std::string t = bp[--sp].AsString();
                        std::string* s = bp[sp - 1].StringPtr();
                        detail::CheckTextLength(s->size() + t.size());
                        s->append(t);
                        break;
                    }
                    case Op::EventVar: {
                        V fn;
                        if (SubscribeByName(*static_cast<const EventOrCompoundExpr*>(in.p), fn)) {
                            bp[sp++] = std::move(fn);
                            pc = in.a;
                        }
                        break;
                    }
                    case Op::Subscribe: Subscribe(*static_cast<const EventOrCompoundExpr*>(in.p), bp[sp - 1]); break;
                    case Op::CompoundCur: bp[sp] = CompoundCurrent(*static_cast<const EventOrCompoundExpr*>(in.p)); ++sp; break;
                    case Op::CompoundStore: WriteQualified(static_cast<const EventOrCompoundExpr*>(in.p)->name, bp[sp - 1]); break;
                    case Op::SwitchCase: {
                        const V& m = bp[--sp];
                        const V& v = bp[sp - 1];
                        const bool eq = (v.IsString() || m.IsString()) ? v.AsString() == m.AsString() : v.AsFloat() == m.AsFloat();
                        if (eq) { --sp; pc = in.a; }
                        break;
                    }
                    case Op::Throw:
                        if (in.b) { --sp; throw RuntimeError(bp[sp].AsString(), Ln(in.a)); }
                        throw RuntimeError("error", Ln(in.a));
                    case Op::TryBegin: handlers.push_back(Handler{in.a, sp, in.b != 0}); break;
                    case Op::TryEnd: handlers.pop_back(); break;
                    case Op::TryPop: handlers.resize(handlers.size() - (std::size_t)in.a); break;
                    case Op::Yield: {
                        V v = std::move(bp[--sp]);
                        if (!hooks || !hooks->YieldValue(*this, v))
                            throw RuntimeError("'yield return' only works in a coroutine the host runs", Ln(in.a));
                        break;
                    }
                    case Op::Return: retVal = std::move(bp[--sp]); return Flow::Return;
                    case Op::ReturnNone: retVal = V{}; return Flow::Return;
                    case Op::EndFlow: return (Flow)in.a;
                    case Op::ForEachStart: {
                        auto& s = *static_cast<const ForEachStmt*>(in.p);
                        auto& st = iters[(std::size_t)in.a];
                        st.coll = std::move(bp[--sp]);
                        st.i = 0;
                        st.keys.clear();
                        // the loop variable lives in the current scope
                        if (in.c >= 0) declare(in.c, V{}); else Define(s.var, V{});
                        if (auto arr = st.coll.AsArray()) st.n = arr->size();
                        else if (auto m = st.coll.AsMap()) {
                            st.keys.reserve(m->size());
                            for (auto& kv : *m) st.keys.push_back(kv.first);
                            st.n = st.keys.size();
                        } else st.n = 0;
                        break;
                    }
                    case Op::ForEachNext: {
                        auto& s = *static_cast<const ForEachStmt*>(in.p);
                        auto& st = iters[(std::size_t)in.a];
                        auto put = [&](const V& v) { if (in.c >= 0) assignSlot(in.c, v, s.var); else Assign(s.var, v); };
                        if (auto arr = st.coll.AsArray()) {
                            if (st.i >= st.n || st.i >= arr->size()) { st.coll = V{}; pc = in.b; break; }
                            put((*arr)[st.i++]);
                        } else if (auto m = st.coll.AsMap()) {
                            if (st.i >= st.n) { st.coll = V{}; pc = in.b; break; }
                            const std::string& k = st.keys[st.i++];
                            if (s.pairs) {
                                V kv = V::MakeMap();
                                (*kv.AsMap())["Key"] = V{k};
                                auto f = m->find(k);
                                (*kv.AsMap())["Value"] = f != m->end() ? f->second : V{};
                                put(kv);
                            } else {
                                put(V{k});
                            }
                        } else {
                            pc = in.b;
                        }
                        break;
                    }
                    case Op::LoopInit: counters[in.a] = 0; break;
                    case Op::LoopCount: CountIteration(counters[in.a], static_cast<const char*>(in.p)); break;
                    case Op::End: return Flow::Normal;
                }
            }
        } catch (const std::exception& ex) {
            // An error inside a try body: go to its catch (with the message), or past it when it has none.
            if (handlers.empty()) throw;
            const Handler h = handlers.back();
            handlers.pop_back();
            sp = h.sp;
            pc = h.pc;
            if (h.message) bp[sp++] = V{std::string(ex.what())};
        }
    }
}

} // namespace okayscript
