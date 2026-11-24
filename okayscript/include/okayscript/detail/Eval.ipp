#pragma once
// What each syntax-tree node does, for any value type V, once its operands are worked out: the stack machine
// (detail/Vm.ipp) runs these. Written for this project; the rules are the engine OkayScriptVM's: numbers and text mix
// with + as text, vectors do component maths, a division by zero gives 0, an index outside a list reads as 0, a name
// nobody set reads as 0. A runtime error without a line gets the line of the statement running (Stamp).
#include "okayscript/detail/Runtime.ipp"
#include "okayscript/detail/Vm.ipp"
#include <cmath>
#include <cstdlib>

namespace okayscript {

namespace detail {
// `this.hp` with no class object in play is the global `hp` (a script's fields are its globals).
// Returns `n` itself (no copy) unless it is stripped into `buf`.
template <class V> const std::string& StripThis(const std::string& n, const V& self, std::string& buf) {
    if (n.size() > 5 && n.compare(0, 5, "this.") == 0 && !IsClassInstance(self)) { buf = n.substr(5); return buf; }
    return n;
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
            if (a.IsString() || b.IsString()) return V{JoinText(a.AsString(), b.AsString())};
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

// `a[i]` and `a[i] = val`. An index with side effects is only worked out when `a` can be indexed (the compiler puts
// SkipUnlessIndexable before it).
template <class V> V BasicRuntime<V>::IndexRead(const V& a, const V& index) {
    using detail::SafeInt;
    if (detail::IsClassInstance(a)) return GetMember(a, index.AsString(), true);
    if (auto v = a.AsArray()) {
        const int i = SafeInt(index.AsFloat());
        if (i < 0 || i >= (int)v->size()) return V{};
        return (*v)[i];
    }
    if (auto m = a.AsMap()) {
        auto it = m->find(index.AsString());
        return it == m->end() ? V{} : it->second;
    }
    return V{};
}
template <class V> void BasicRuntime<V>::IndexWrite(const V& a, const V& val, const V& index) {
    using detail::SafeInt;
    if (detail::IsClassInstance(a)) { SetMember(a, index.AsString(), val, true); return; }
    if (auto v = a.AsArray()) {
        const int i = SafeInt(index.AsFloat());
        if (i >= 0 && i < (int)v->size()) (*v)[i] = val;
        else if (i == (int)v->size()) v->push_back(val);   // one past the end appends
    } else if (auto m = a.AsMap()) {
        (*m)[index.AsString()] = val;
    }
}
// `v.Name(args)` once v and the arguments are worked out: a class method, the host's, then the value's own.
template <class V> V BasicRuntime<V>::CallMethodOn(const V& v, const MethodCallExpr& e, std::vector<V>& vals) {
    V out;
    if (detail::IsClassInstance(v)) return CallMethod(v, e.name, vals, true);
    if (hooks && hooks->CallMember(*this, v, e.name, vals, out)) return out;
    detail::LambdaInvoker<V> inv = [this](const V& f, std::vector<V>& xs) { return CallLambda(f, xs); };
    if (detail::CallValueMethod<V>(v, e.name, vals, out, &inv)) return out;
    throw RuntimeError("no method '" + e.name + "' on this value", Ln(e.line));
}

// ---- What the nodes do, once their operands are worked out (the stack machine's instructions call these) ----

// A plain name that is not in a slot or a cache: the scopes, then (in a method) `this` and the object's fields, then
// the host's bare names (such as `gameObject`); a name never set reads as 0.
template <class V> void BasicRuntime<V>::LoadName(const std::string& n, NameCache* cache, V& out) {
    if (const V* p = Lookup(n, cache)) { out = *p; return; }
    if (!selfClass.empty()) {
        if (n == "this" && detail::IsClassInstance(self)) out = self;
        else out = Get(n);   // a field of the object a method runs on
        return;
    }
    V v;
    if (hooks && hooks->ReadName(*this, n, v)) { out = std::move(v); return; }
    out = V{};
}
// `xs.Count`, `Time.deltaTime`, `this.hp`: a property of a variable, the host's name, or (`this.` with no class
// object) the global.
template <class V> V BasicRuntime<V>::ReadQualified(const VarExpr& e) {
    std::string buf;
    const std::string& key = detail::StripThis(e.n, self, buf);
    if (key.find('.') != std::string::npos) {
        bool ok = false;
        V v = GetDotted(key, ok);
        if (ok) return v;
    }
    return Get(key);
}
template <class V> void BasicRuntime<V>::WriteQualified(const std::string& n, const V& v) {
    std::string buf;
    const std::string& key = detail::StripThis(n, self, buf);
    if (key.find('.') != std::string::npos && SetDotted(key, v)) return;
    Assign(key, v);
}
template <class V> V BasicRuntime<V>::ReadMember(const V& v, const MemberExpr& e) {
    V out;
    if (detail::IsClassInstance(v)) return GetMember(v, e.name, true);
    if (hooks && hooks->GetMember(*this, v, e.name, out)) return out;
    if (detail::ValueProperty<V>(v, e.name, out)) return out;
    throw RuntimeError("no property '" + e.name + "' on this value", Ln(e.line));
}
template <class V> void BasicRuntime<V>::WriteMember(const V& target, const MemberAssignExpr& e, const V& v) {
    if (detail::IsClassInstance(target)) { SetMember(target, e.name, v, true); return; }
    if (hooks && hooks->SetMember(*this, target, e.name, v)) return;
    if (auto m = target.AsMap()) { (*m)[e.name] = v; return; }
    throw RuntimeError(target.IsNumber() && target.AsFloat() == 0.0f
        ? "can't set '" + e.name + "': the object is null"
        : "can't assign '" + e.name + "' on this value", Ln(e.line));
}
template <class V> V BasicRuntime<V>::CastValue(const std::string& type, const V& v) {
    using detail::SafeInt;
    if (type == "string") return V{v.AsString()};
    if (type == "bool") return V{v.AsBool()};
    if (type == "char") return v.IsString() ? v : V{std::string(1, (char)SafeInt(v.AsFloat()))};
    if (type == "float" || type == "double" || type == "decimal") {
        if (v.IsString()) { try { return V{std::stof(v.AsString())}; } catch (...) { return V{0.0f}; } }
        return V{v.AsFloat()};
    }
    const float f = v.IsString() && v.AsString().size() == 1 ? (float)(unsigned char)v.AsString()[0] : v.AsFloat();
    return V{(float)(long long)f};   // integer casts truncate toward zero
}
template <class V> V BasicRuntime<V>::MakeLambda(const LambdaExpr& e) {
    lambdas[e.id] = LambdaBody{&e.params, &e.body, &e.defaults, &e.code, &e.defaultCode};
    V v = V::MakeMap();
    (*v.AsMap())["__lambda"] = V{(float)e.id};
    return v;
}
// `a[i] op= rhs` on a class object, a list or a map (the caller has checked it is one of those).
template <class V>
V BasicRuntime<V>::IndexCompound(const V& a, const V& rhs, const V& index, const IndexCompoundAssignExpr& e) {
    using detail::SafeInt;
    auto apply = [&](const V& cur) -> V {
        switch (e.op) {
            case Tok::Plus:  if (cur.IsString() || rhs.IsString()) return V{detail::JoinText(cur.AsString(), rhs.AsString())};
                             return V{cur.AsFloat() + rhs.AsFloat()};
            case Tok::Minus: return V{cur.AsFloat() - rhs.AsFloat()};
            case Tok::Star:  return V{cur.AsFloat() * rhs.AsFloat()};
            case Tok::Slash: { const float d = rhs.AsFloat(); return V{d != 0 ? cur.AsFloat() / d : 0.0f}; }
            case Tok::Percent: { const float d = rhs.AsFloat(); return V{d != 0 ? std::fmod(cur.AsFloat(), d) : 0.0f}; }
            default:         return rhs;
        }
    };
    if (detail::IsClassInstance(a)) {
        const std::string k = index.AsString();
        V next = apply(GetMember(a, k, true));
        SetMember(a, k, next, true);
        return next;
    }
    if (auto v = a.AsArray()) {
        const int i = SafeInt(index.AsFloat());
        if (i >= 0 && i < (int)v->size()) { (*v)[i] = apply((*v)[i]); return (*v)[i]; }
    } else if (auto m = a.AsMap()) {
        const std::string k = index.AsString();
        V cur = m->count(k) ? (*m)[k] : V{};
        (*m)[k] = apply(cur);
        return (*m)[k];
    }
    return V{};
}
// `name += f` / `name -= f`: subscribe or unsubscribe f for the event `name`.
template <class V> V BasicRuntime<V>::Subscribe(const EventOrCompoundExpr& e, const V& fn) {
    if (e.remove) RemoveSub(e.name, fn); else AddSub(e.name, fn);
    return fn;
}
// The right side is a name: a variable holding a lambda, or a script function no variable hides. False: neither, so
// it is plain compound assignment.
template <class V> bool BasicRuntime<V>::SubscribeByName(const EventOrCompoundExpr& e, V& out) {
    const std::string& n = static_cast<const VarExpr&>(*e.rhs).n;
    if (Has(n) && IsLambda(Get(n))) { out = Subscribe(e, Get(n)); return true; }
    if (!Has(n) && functions.count(n)) {
        V fn = V::MakeMap();
        (*fn.AsMap())["__fn"] = V{n};
        out = Subscribe(e, fn);
        return true;
    }
    return false;
}
// The current value of the name `name op= rhs` changes (dotted names and `this.` included).
template <class V> V BasicRuntime<V>::CompoundCurrent(const EventOrCompoundExpr& e) {
    std::string buf;
    const std::string& key = detail::StripThis(e.name, self, buf);
    bool ok = false;
    V cur;
    if (key.find('.') != std::string::npos) cur = GetDotted(key, ok);
    if (!ok) cur = (key == "this" && detail::IsClassInstance(self)) ? self : Get(key);
    return cur;
}
template <class V> V BasicRuntime<V>::EvalCode(const Expr& e, ChunkPtr& code) {
    if (!code) code = CompileExpr(e);
    RunChunk(*code);
    return std::move(retVal);
}

} // namespace okayscript
