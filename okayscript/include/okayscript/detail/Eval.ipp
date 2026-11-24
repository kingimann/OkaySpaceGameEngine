#pragma once
// What each syntax-tree node does when it runs, for any value type V. Written for this project; the rules are the
// engine OkayScriptVM's: numbers and text mix with + as text, vectors do component maths, a division by zero gives 0,
// an index outside a list reads as 0, a name nobody set reads as 0. Run gives an error the line of its statement.
#include "okayscript/detail/Runtime.ipp"
#include <cmath>
#include <cstdlib>

namespace okayscript {

namespace detail {
// `this.hp` with no class object in play is the global `hp` (a script's fields are its globals).
template <class V> std::string StripThis(const std::string& n, const V& self) {
    return (n.rfind("this.", 0) == 0 && !IsClassInstance(self)) ? n.substr(5) : n;
}

template <class V> V Arith(Tok op, const V& a, const V& b) {
    using Vec = VecOf<V>;
    // Vector maths: v + w, v - w, v * 2, 2 * v, v / 2, v * w (component-wise), v == w.
    if ((a.IsVec3() || b.IsVec3()) && !a.IsString() && !b.IsString()) {
        const Vec x = a.AsVec3(), y = b.AsVec3();
        const bool sa = !a.IsVec3(), sb = !b.IsVec3();   // which side is a plain number
        switch (op) {
            case Tok::Plus:  return sa ? V{Vec{a.AsFloat() + y.x, a.AsFloat() + y.y, a.AsFloat() + y.z}}
                                  : sb ? V{Vec{x.x + b.AsFloat(), x.y + b.AsFloat(), x.z + b.AsFloat()}} : V{x + y};
            case Tok::Minus: return sb ? V{Vec{x.x - b.AsFloat(), x.y - b.AsFloat(), x.z - b.AsFloat()}}
                                  : sa ? V{Vec{a.AsFloat() - y.x, a.AsFloat() - y.y, a.AsFloat() - y.z}} : V{x - y};
            case Tok::Star:  return sa ? V{y * a.AsFloat()} : sb ? V{x * b.AsFloat()} : V{Vec{x.x * y.x, x.y * y.y, x.z * y.z}};
            case Tok::Slash: {
                if (sb) { const float d = b.AsFloat(); return d != 0 ? V{x * (1.0f / d)} : V{Vec::Zero}; }
                auto dv = [](float n, float d) { return d != 0 ? n / d : 0.0f; };
                return sa ? V{Vec{dv(a.AsFloat(), y.x), dv(a.AsFloat(), y.y), dv(a.AsFloat(), y.z)}}
                          : V{Vec{dv(x.x, y.x), dv(x.y, y.y), dv(x.z, y.z)}};
            }
            case Tok::Eq: return V{!sa && !sb && x.x == y.x && x.y == y.y && x.z == y.z};
            case Tok::Ne: return V{!(!sa && !sb && x.x == y.x && x.y == y.y && x.z == y.z)};
            default: break;   // comparisons fall through to lengths below
        }
    }
    switch (op) {
        case Tok::Plus:
            if (a.IsString() || b.IsString()) return V{a.AsString() + b.AsString()};
            return V{a.AsFloat() + b.AsFloat()};
        case Tok::Minus:   return V{a.AsFloat() - b.AsFloat()};
        case Tok::Star:    return V{a.AsFloat() * b.AsFloat()};
        case Tok::Slash:   { const float d = b.AsFloat(); return V{d != 0 ? a.AsFloat() / d : 0.0f}; }
        case Tok::Percent: { const float d = b.AsFloat(); return V{d != 0 ? std::fmod(a.AsFloat(), d) : 0.0f}; }
        case Tok::BitAnd: return V{(float)(SafeInt64(a.AsFloat()) & SafeInt64(b.AsFloat()))};
        case Tok::BitOr:  return V{(float)(SafeInt64(a.AsFloat()) | SafeInt64(b.AsFloat()))};
        case Tok::BitXor: return V{(float)(SafeInt64(a.AsFloat()) ^ SafeInt64(b.AsFloat()))};
        case Tok::Lt: return V{a.AsFloat() <  b.AsFloat()};
        case Tok::Gt: return V{a.AsFloat() >  b.AsFloat()};
        case Tok::Le: return V{a.AsFloat() <= b.AsFloat()};
        case Tok::Ge: return V{a.AsFloat() >= b.AsFloat()};
        case Tok::Eq: return V{ValuesEqual(a, b)};
        case Tok::Ne: return V{!ValuesEqual(a, b)};
        default: return V{};
    }
}
} // namespace detail

template <class V> void BasicRuntime<V>::Run(const Stmt& s) {
    try {
        Exec(s);
    } catch (ScriptError& e) {
        if (e.line == 0) e.line = Ln(s.line);
        throw;
    }
}
template <class V> void BasicRuntime<V>::RunList(const StmtList& list) {
    for (auto& s : list) Run(*s);
}

template <class V> V BasicRuntime<V>::Eval(const Expr& ex) {
    using detail::SafeInt;
    using detail::SafeInt64;
    switch (ex.kind) {
        case ExprKind::Number: return V{(float)static_cast<const NumberExpr&>(ex).v};
        case ExprKind::String: return V{static_cast<const StringExpr&>(ex).v};
        case ExprKind::Bool: return V{static_cast<const BoolExpr&>(ex).v};
        case ExprKind::InterpString: {
            auto& e = static_cast<const InterpStringExpr&>(ex);
            std::string out;
            for (auto& s : e.segs) out += s.isExpr ? Eval(*s.expr).AsString() : s.lit;
            return V{out};
        }
        case ExprKind::Var: {
            auto& e = static_cast<const VarExpr&>(ex);
            if (e.n == "this" && detail::IsClassInstance(self)) return self;
            const std::string key = detail::StripThis(e.n, self);
            if (key.find('.') != std::string::npos) {
                bool ok = false;
                V v = GetDotted(key, ok);
                if (ok) return v;
            } else if (hooks && selfClass.empty() && !Has(key)) {
                V v;
                if (hooks->ReadName(*this, key, v)) return v;   // a bare host name, such as the engine's `gameObject`
            }
            return Get(key);
        }
        case ExprKind::Assign: {
            auto& e = static_cast<const AssignExpr&>(ex);
            V v = Eval(*e.value);
            const std::string key = detail::StripThis(e.n, self);
            if (key.find('.') != std::string::npos && SetDotted(key, v)) return v;
            Assign(key, v);
            return v;
        }
        case ExprKind::Unary: {
            auto& e = static_cast<const UnaryExpr&>(ex);
            V v = Eval(*e.e);
            if (e.op == Tok::Minus) return v.IsVec3() ? V{v.AsVec3() * -1.0f} : V{-v.AsFloat()};
            if (e.op == Tok::Not) return V{!v.AsBool()};
            return v;
        }
        case ExprKind::Logical: {
            auto& e = static_cast<const LogicalExpr&>(ex);
            V lv = Eval(*e.l);
            if (e.op == Tok::And) return V{lv.AsBool() ? Eval(*e.r).AsBool() : false};
            return V{lv.AsBool() ? true : Eval(*e.r).AsBool()};
        }
        case ExprKind::Coalesce: {
            auto& e = static_cast<const CoalesceExpr&>(ex);
            V lv = Eval(*e.l);
            return IsNullish(lv) ? Eval(*e.r) : lv;
        }
        case ExprKind::NullishAssign: {
            auto& e = static_cast<const NullishAssignExpr&>(ex);
            V cur = Get(e.n);
            if (!IsNullish(cur)) return cur;
            V v = Eval(*e.value);
            Assign(e.n, v);
            return v;
        }
        case ExprKind::Binary: {
            auto& e = static_cast<const BinaryExpr&>(ex);
            V a = Eval(*e.l), b = Eval(*e.r);
            return detail::Arith<V>(e.op, a, b);
        }
        case ExprKind::Member: {
            auto& e = static_cast<const MemberExpr&>(ex);
            V v = Eval(*e.obj), out;
            if (e.safe && IsNullish(v)) return V{};
            if (detail::IsClassInstance(v)) return GetMember(v, e.name, true);
            if (hooks && hooks->GetMember(*this, v, e.name, out)) return out;
            if (detail::ValueProperty<V>(v, e.name, out)) return out;
            throw RuntimeError("no property '" + e.name + "' on this value", Ln(e.line));
        }
        case ExprKind::MemberAssign: {
            auto& e = static_cast<const MemberAssignExpr&>(ex);
            V target = Eval(*e.obj), v = Eval(*e.value);
            if (detail::IsClassInstance(target)) { SetMember(target, e.name, v, true); return v; }
            if (hooks && hooks->SetMember(*this, target, e.name, v)) return v;
            if (auto m = target.AsMap()) { (*m)[e.name] = v; return v; }
            throw RuntimeError(target.IsNumber() && target.AsFloat() == 0.0f
                ? "can't set '" + e.name + "': the object is null"
                : "can't assign '" + e.name + "' on this value", Ln(e.line));
        }
        case ExprKind::Cast: {
            auto& e = static_cast<const CastExpr&>(ex);
            V v = Eval(*e.e);
            if (e.type == "string") return V{v.AsString()};
            if (e.type == "bool") return V{v.AsBool()};
            if (e.type == "char") return v.IsString() ? v : V{std::string(1, (char)SafeInt(v.AsFloat()))};
            if (e.type == "float" || e.type == "double" || e.type == "decimal") {
                if (v.IsString()) { try { return V{std::stof(v.AsString())}; } catch (...) { return V{0.0f}; } }
                return V{v.AsFloat()};
            }
            const float f = v.IsString() && v.AsString().size() == 1 ? (float)(unsigned char)v.AsString()[0] : v.AsFloat();
            return V{(float)(long long)f};   // integer casts truncate toward zero
        }
        case ExprKind::MethodCall: {
            auto& e = static_cast<const MethodCallExpr&>(ex);
            V v = Eval(*e.obj), out;
            if (e.safe && IsNullish(v)) return V{};
            std::vector<V> vals;
            vals.reserve(e.args.size());
            for (auto& a : e.args) vals.push_back(Eval(*a));
            if (detail::IsClassInstance(v)) return CallMethod(v, e.name, vals, true);
            if (hooks && hooks->CallMember(*this, v, e.name, vals, out)) return out;
            detail::LambdaInvoker<V> inv = [this](const V& f, std::vector<V>& xs) { return CallLambda(f, xs); };
            if (detail::CallValueMethod<V>(v, e.name, vals, out, &inv)) return out;
            throw RuntimeError("no method '" + e.name + "' on this value", Ln(e.line));
        }
        case ExprKind::Call: {
            auto& e = static_cast<const CallExpr&>(ex);
            std::vector<V> vals;
            std::vector<std::string> names;
            vals.reserve(e.args.size());
            bool anyNamed = false;
            for (std::size_t i = 0; i < e.args.size(); ++i) {
                std::string nm = i < e.argNames.size() ? e.argNames[i] : std::string();
                if (!nm.empty()) anyNamed = true;
                names.push_back(std::move(nm));
                vals.push_back(Eval(*e.args[i]));
            }
            return Call(e.name, vals, anyNamed ? &names : nullptr, e.line);
        }
        case ExprKind::Ternary: {
            auto& e = static_cast<const TernaryExpr&>(ex);
            return Eval(*e.cond).AsBool() ? Eval(*e.a) : Eval(*e.b);
        }
        case ExprKind::Array: {
            auto& e = static_cast<const ArrayExpr&>(ex);
            V v = V::MakeArray();
            auto arr = v.AsArray();
            for (auto& el : e.elems) arr->push_back(Eval(*el));
            return v;
        }
        case ExprKind::Index: {
            auto& e = static_cast<const IndexExpr&>(ex);
            V a = Eval(*e.arr);
            if (e.safe && IsNullish(a)) return V{};
            if (detail::IsClassInstance(a)) return GetMember(a, Eval(*e.index).AsString(), true);
            if (auto v = a.AsArray()) {
                const int i = SafeInt(Eval(*e.index).AsFloat());
                if (i < 0 || i >= (int)v->size()) return V{};
                return (*v)[i];
            }
            if (auto m = a.AsMap()) {
                auto it = m->find(Eval(*e.index).AsString());
                return it == m->end() ? V{} : it->second;
            }
            return V{};
        }
        case ExprKind::IndexAssign: {
            auto& e = static_cast<const IndexAssignExpr&>(ex);
            V a = Eval(*e.arr);
            V val = Eval(*e.value);
            if (detail::IsClassInstance(a)) { SetMember(a, Eval(*e.index).AsString(), val, true); return val; }
            if (auto v = a.AsArray()) {
                const int i = SafeInt(Eval(*e.index).AsFloat());
                if (i >= 0 && i < (int)v->size()) (*v)[i] = val;
                else if (i == (int)v->size()) v->push_back(val);   // one past the end appends
            } else if (auto m = a.AsMap()) {
                (*m)[Eval(*e.index).AsString()] = val;
            }
            return val;
        }
        case ExprKind::IndexCompoundAssign: {
            auto& e = static_cast<const IndexCompoundAssignExpr&>(ex);
            V a = Eval(*e.arr);
            V rhs = Eval(*e.value);
            auto apply = [&](const V& cur) -> V {
                switch (e.op) {
                    case Tok::Plus:  if (cur.IsString() || rhs.IsString()) return V{cur.AsString() + rhs.AsString()};
                                     return V{cur.AsFloat() + rhs.AsFloat()};
                    case Tok::Minus: return V{cur.AsFloat() - rhs.AsFloat()};
                    case Tok::Star:  return V{cur.AsFloat() * rhs.AsFloat()};
                    case Tok::Slash: { const float d = rhs.AsFloat(); return V{d != 0 ? cur.AsFloat() / d : 0.0f}; }
                    case Tok::Percent: { const float d = rhs.AsFloat(); return V{d != 0 ? std::fmod(cur.AsFloat(), d) : 0.0f}; }
                    default:         return rhs;
                }
            };
            if (detail::IsClassInstance(a)) {
                const std::string k = Eval(*e.index).AsString();
                V next = apply(GetMember(a, k, true));
                SetMember(a, k, next, true);
                return next;
            }
            if (auto v = a.AsArray()) {
                const int i = SafeInt(Eval(*e.index).AsFloat());
                if (i >= 0 && i < (int)v->size()) { (*v)[i] = apply((*v)[i]); return (*v)[i]; }
            } else if (auto m = a.AsMap()) {
                const std::string k = Eval(*e.index).AsString();
                V cur = m->count(k) ? (*m)[k] : V{};
                (*m)[k] = apply(cur);
                return (*m)[k];
            }
            return V{};
        }
        case ExprKind::Lambda: {
            auto& e = static_cast<const LambdaExpr&>(ex);
            lambdas[e.id] = LambdaBody{&e.params, &e.body, &e.defaults};
            V v = V::MakeMap();
            (*v.AsMap())["__lambda"] = V{(float)e.id};
            return v;
        }
        case ExprKind::New: {
            auto& e = static_cast<const NewExpr&>(ex);
            std::vector<V> vals;
            vals.reserve(e.args.size());
            for (auto& a : e.args) vals.push_back(Eval(*a));
            return MakeInstance(e.type, vals);
        }
        case ExprKind::EventOrCompound: {
            auto& e = static_cast<const EventOrCompoundExpr&>(ex);
            auto take = [&](const V& fn) {
                if (e.remove) RemoveSub(e.name, fn); else AddSub(e.name, fn);
                return fn;
            };
            if (e.rhs->kind == ExprKind::Var) {
                const std::string& n = static_cast<const VarExpr&>(*e.rhs).n;
                if (Has(n) && IsLambda(Get(n))) return take(Get(n));
                if (!Has(n) && functions.count(n)) {
                    V fn = V::MakeMap();
                    (*fn.AsMap())["__fn"] = V{n};
                    return take(fn);
                }
            } else if (e.rhs->kind == ExprKind::Lambda) {
                return take(Eval(*e.rhs));
            }
            // name = name <bin> rhs, the plain compound assignment (dotted names and this. included)
            const std::string key = detail::StripThis(e.name, self);
            V cur;
            bool ok = false;
            if (key.find('.') != std::string::npos) cur = GetDotted(key, ok);
            if (!ok) cur = (key == "this" && detail::IsClassInstance(self)) ? self : Get(key);
            V v = detail::Arith<V>(e.bin, cur, Eval(*e.rhs));
            if (key.find('.') != std::string::npos && SetDotted(key, v)) return v;
            Assign(key, v);
            return v;
        }
    }
    return V{};
}

template <class V> void BasicRuntime<V>::Exec(const Stmt& st) {
    switch (st.kind) {
        case StmtKind::Expr: Eval(*static_cast<const ExprStmt&>(st).e); return;
        case StmtKind::VarDecl: {
            auto& s = static_cast<const VarDeclStmt&>(st);
            if (s.kind & KStatic) {
                if (statics && statics->vars.count(s.n)) return;   // already set by another run
                DefineStatic(s.n, s.init ? Eval(*s.init) : DefaultForKind<V>(s.kind), (unsigned char)(s.kind & ~KStatic));
                return;
            }
            if (!s.kind) { Define(s.n, s.init ? Eval(*s.init) : V{}); return; }
            DefineTyped(s.n, s.init ? Eval(*s.init) : DefaultForKind<V>(s.kind), s.kind);
            return;
        }
        case StmtKind::Return: {
            auto& s = static_cast<const ReturnStmt&>(st);
            throw ReturnSignal<V>{s.e ? Eval(*s.e) : V{}};
        }
        case StmtKind::Break: throw BreakSignal{};
        case StmtKind::Continue: throw ContinueSignal{};
        case StmtKind::Throw: {
            auto& s = static_cast<const ThrowStmt&>(st);
            throw RuntimeError(s.e ? Eval(*s.e).AsString() : std::string("error"), Ln(s.line));
        }
        case StmtKind::Try: {
            auto& s = static_cast<const TryStmt&>(st);
            try {
                RunList(s.body);
            } catch (const std::exception& ex) {
                if (!s.hasCatch) return;   // try without catch: the error is dropped (engine rule)
                if (!s.catchVar.empty()) Define(s.catchVar, V{std::string(ex.what())});
                RunList(s.catchB);
            }
            return;
        }
        case StmtKind::Block: RunList(static_cast<const BlockStmt&>(st).body); return;
        case StmtKind::If: {
            auto& s = static_cast<const IfStmt&>(st);
            if (Eval(*s.cond).AsBool()) RunList(s.thenB);
            else RunList(s.elseB);
            return;
        }
        case StmtKind::While: {
            auto& s = static_cast<const WhileStmt&>(st);
            long long n = 0;
            while (Eval(*s.cond).AsBool()) {
                try { RunList(s.body); }
                catch (ContinueSignal&) {}
                catch (BreakSignal&) { break; }
                CountIteration(n, "while loop");
            }
            return;
        }
        case StmtKind::DoWhile: {
            auto& s = static_cast<const DoWhileStmt&>(st);
            long long n = 0;
            do {
                try { RunList(s.body); }
                catch (ContinueSignal&) {}
                catch (BreakSignal&) { break; }
                CountIteration(n, "do-while loop");
            } while (Eval(*s.cond).AsBool());
            return;
        }
        case StmtKind::Switch: {
            auto& s = static_cast<const SwitchStmt&>(st);
            auto eq = [](const V& a, const V& b) {
                return (a.IsString() || b.IsString()) ? a.AsString() == b.AsString() : a.AsFloat() == b.AsFloat();
            };
            V v = Eval(*s.subject);
            int start = -1;
            for (std::size_t i = 0; i < s.cases.size(); ++i)
                if (!s.cases[i].isDefault && eq(v, Eval(*s.cases[i].match))) { start = (int)i; break; }
            if (start < 0)
                for (std::size_t i = 0; i < s.cases.size(); ++i)
                    if (s.cases[i].isDefault) { start = (int)i; break; }
            if (start < 0) return;
            try {
                for (std::size_t i = (std::size_t)start; i < s.cases.size(); ++i) RunList(s.cases[i].body);   // falls through
            } catch (BreakSignal&) {}
            return;
        }
        case StmtKind::For: {
            auto& s = static_cast<const ForStmt&>(st);
            if (s.init) Run(*s.init);
            long long n = 0;
            while (!s.cond || Eval(*s.cond).AsBool()) {
                try { RunList(s.body); }
                catch (ContinueSignal&) {}
                catch (BreakSignal&) { break; }
                if (s.step) Eval(*s.step);
                CountIteration(n, "for loop");
            }
            return;
        }
        case StmtKind::ForEach: {
            auto& s = static_cast<const ForEachStmt&>(st);
            V coll = Eval(*s.iterable);
            Define(s.var, V{});   // the loop variable lives in the current scope
            if (auto arr = coll.AsArray()) {
                const std::size_t n = arr->size();   // items added while looping are not visited
                for (std::size_t i = 0; i < n && i < arr->size(); ++i) {
                    Assign(s.var, (*arr)[i]);
                    try { RunList(s.body); }
                    catch (ContinueSignal&) {}
                    catch (BreakSignal&) { break; }
                }
            } else if (auto m = coll.AsMap()) {
                std::vector<std::string> keys;
                keys.reserve(m->size());
                for (auto& kv : *m) keys.push_back(kv.first);
                for (auto& k : keys) {
                    if (s.pairs) {
                        V kv = V::MakeMap();
                        (*kv.AsMap())["Key"] = V{k};
                        auto it = m->find(k);
                        (*kv.AsMap())["Value"] = it != m->end() ? it->second : V{};
                        Assign(s.var, kv);
                    } else {
                        Assign(s.var, V{k});
                    }
                    try { RunList(s.body); }
                    catch (ContinueSignal&) {}
                    catch (BreakSignal&) { break; }
                }
            }
            return;
        }
        case StmtKind::Multi: RunList(static_cast<const MultiStmt&>(st).list); return;
        case StmtKind::Yield: {
            auto& s = static_cast<const YieldStmt&>(st);
            V v = s.e ? Eval(*s.e) : V{};
            if (!hooks || !hooks->Yield(*this, v))
                throw RuntimeError("'yield return' only works in a coroutine the host runs", Ln(s.line));
            return;
        }
    }
}

} // namespace okayscript
