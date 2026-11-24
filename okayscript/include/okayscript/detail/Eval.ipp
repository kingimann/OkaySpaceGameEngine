#pragma once
// What each syntax-tree node does, for any value type V, once its operands are worked out: the stack machine
// (detail/Vm.ipp) runs these. Written for this project; the rules are the engine OkayScriptVM's: numbers and text mix
// with + as text, vectors do component maths, a division by zero gives 0, an index outside a list reads as 0, a name
// nobody set reads as 0. A runtime error without a line gets the line of the statement running (Stamp).
#include "okayscript/detail/Runtime.ipp"
#include "okayscript/detail/Vm.ipp"
#include "okayscript/Parser.hpp"
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace okayscript {

namespace detail {
// `this.hp` with no class object in play is the global `hp` (a script's fields are its globals).
// Returns `n` itself (no copy) unless it is stripped into `buf`.
template <class V> const std::string& StripThis(const std::string& n, const V& self, std::string& buf) {
    if (n.size() > 5 && n.compare(0, 5, "this.") == 0 && !IsClassInstance(self)) { buf = n.substr(5); return buf; }
    return n;
}

/// v as a whole number for integer maths: an Int, or a float with no fraction (so `n + 1` stays whole when n is an
/// Int). False for anything else, which then takes the float rules.
template <class V> inline bool WholeOf(const V& v, std::int64_t& out) {
    if (const std::int64_t* i = IntOf(v)) { out = *i; return true; }
    if (const auto* f = v.NumberPtr()) {
        const float x = *f;
        if (x == x && x > -9.2e18f && x < 9.2e18f && x == (float)(std::int64_t)x) { out = (std::int64_t)x; return true; }
    }
    return false;
}
/// Integer maths on two whole numbers, written for this project: + - * wrap around (two's complement, no undefined
/// behaviour), / truncates toward zero like C#'s long, and x / 0 and x % 0 give 0 as for floats. False: not an
/// integer operator.
template <class V> inline bool IntArith(Tok op, std::int64_t x, std::int64_t y, V& out) {
    using U = std::uint64_t;
    const std::int64_t kMin = std::numeric_limits<std::int64_t>::min();
    switch (op) {
        case Tok::Plus: out = MakeInt<V>((std::int64_t)((U)x + (U)y)); return true;
        case Tok::Minus: out = MakeInt<V>((std::int64_t)((U)x - (U)y)); return true;
        case Tok::Star: out = MakeInt<V>((std::int64_t)((U)x * (U)y)); return true;
        case Tok::Slash: out = MakeInt<V>(y == 0 ? 0 : (x == kMin && y == -1) ? x : x / y); return true;
        case Tok::Percent: out = MakeInt<V>((y == 0 || y == -1) ? 0 : x % y); return true;
        case Tok::BitAnd: out = MakeInt<V>(x & y); return true;
        case Tok::BitOr: out = MakeInt<V>(x | y); return true;
        case Tok::BitXor: out = MakeInt<V>(x ^ y); return true;
        case Tok::Lt: out = V{x < y}; return true;
        case Tok::Gt: out = V{x > y}; return true;
        case Tok::Le: out = V{x <= y}; return true;
        case Tok::Ge: out = V{x >= y}; return true;
        case Tok::Eq: out = V{x == y}; return true;
        case Tok::Ne: out = V{x != y}; return true;
        default: return false;
    }
}

template <class V> V Arith(Tok op, const V& a, const V& b) {
    using Vec = VecOf<V>;
    if constexpr (HasInt<V>::value) {   // an Int on either side: whole-number maths when both sides are whole
        if ((IntOf(a) || IntOf(b)) && !a.IsVec3() && !b.IsVec3() && !a.IsString() && !b.IsString()) {
            std::int64_t x = 0, y = 0;
            V out;
            if (WholeOf(a, x) && WholeOf(b, y) && IntArith<V>(op, x, y, out)) return out;
        }
    }
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
    if (GeneratorMethod(v, e.name, vals, out)) return out;
    if (e.name == "Invoke" && detail::IsCallableValue(v)) return CallValue(v, vals);   // f(1)(2), fs[0](x)
    if (auto m = v.AsMap()) {   // a lambda kept in a map, a module's function: (mod).add(1, 2)
        auto it = m->find(e.name);
        if (it != m->end() && detail::IsCallableValue(it->second)) return CallValue(it->second, vals);
    }
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
    if (type == "long" || type == "ulong") {   // a 64-bit whole number where the value type has them (Value::Int)
        if (detail::IntOf(v)) return v;
        if constexpr (detail::HasInt<V>::value) {
            if (v.IsString()) {
                const std::string t = v.AsString();
                char* end = nullptr;
                errno = 0;
                const long long n = std::strtoll(t.c_str(), &end, 10);
                if (end != t.c_str() && errno == 0) return detail::MakeInt<V>((std::int64_t)n);
            }
        }
        const float lf = v.IsString() && v.AsString().size() == 1 ? (float)(unsigned char)v.AsString()[0] : v.AsFloat();
        return detail::MakeInt<V>(detail::SafeInt64(lf));
    }
    if (const std::int64_t* i = detail::IntOf(v)) return V{(float)*i};   // (int) of a whole number: the engine's int
    const float f = v.IsString() && v.AsString().size() == 1 ? (float)(unsigned char)v.AsString()[0] : v.AsFloat();
    return V{(float)(long long)f};   // integer casts truncate toward zero
}
// A lambda value: {"__lambda": id} ("__mod": the module that made it, when not the main program). Made in a body
// with a box (it captures, or runs in a lambda that does), it keeps that box: its variables stay alive and shared
// (closures by reference).
template <class V> V BasicRuntime<V>::MakeLambda(const LambdaExpr& e) {
    lambdas[e.id] = LambdaBody{&e.params, &e.body, &e.defaults, &e.code, &e.defaultCode};
    V v = V::MakeMap();
    (*v.AsMap())["__lambda"] = V{(float)e.id};
    if (moduleId) (*v.AsMap())["__mod"] = V{(float)moduleId};
    const V& env = scopes.back().env;
    if (env.IsMap()) {
        if (closures.size() >= closureSweepAt) {   // forget the boxes of lambdas that are gone
            for (auto it = closures.begin(); it != closures.end();) it = it->second.fn.expired() ? closures.erase(it) : std::next(it);
            CollectCycles();
            closureSweepAt = closures.size() * 2 > 64 ? closures.size() * 2 : 64;
        }
        auto m = v.AsMap();
        closures[m.get()] = ClosureRef{std::weak_ptr<const void>(std::shared_ptr<const void>(m)), env};
    }
    return v;
}

// ---- Cycles through the closure and generator tables ------------------------------------------------------------
// The tables hold a lambda's box (and a generator's frame) strongly and the lambda (the generator) weakly, so the
// entry goes when the lambda does. When the box holds the lambda itself, directly or through lists and maps, the
// lambda never goes: a leak of the box and everything in it. This finds them by counting references (trial
// deletion): walk every list and map the tables' entries reach, and count how many references to each come from
// inside that walk. One with more references (shared_ptr use count) than that is held from outside: a variable,
// the stack, a host. What it reaches is in use, and so is the box of a lambda in use, and so on. An entry whose lambda
// is not in use is dropped, which frees the box and the lambda with it. Values only reached through a raw pointer
// would be missed, so nothing here runs while script code holds one: it runs only from MakeLambda / MakeGenerator.
template <class V> std::size_t BasicRuntime<V>::CollectCycles() {
    if (closures.empty() && generators.empty()) return 0;
    using MapT = typename V::Map;
    using ArrT = typename V::Array;
    struct Node { long ext = 0, in = 0; bool map = false, live = false; };
    std::unordered_map<const void*, Node> nodes;
    std::vector<const void*> work;
    auto edge = [&](const V& v) {   // a reference to v from inside the walk
        const void* p = nullptr;
        long uses = 0;
        bool isMap = false;
        if (auto m = v.AsMap()) { p = m.get(); uses = m.use_count() - 1; isMap = true; }   // - 1: this copy
        else if (auto a = v.AsArray()) { p = a.get(); uses = a.use_count() - 1; }
        if (!p) return;
        auto ins = nodes.try_emplace(p);
        Node& n = ins.first->second;
        if (ins.second) { n.ext = uses; n.map = isMap; work.push_back(p); }
        ++n.in;
    };
    auto genRoots = [](const Generator<V>& g, auto&& f) {   // what a generator's entry holds
        f(g.env);
        f(g.self);
        for (const V& x : g.slots) f(x);
        for (const V& x : g.run.stack) f(x);
        f(g.run.value);
        for (const auto& it : g.run.iters) f(it.coll);
    };
    for (auto& kv : closures) if (!kv.second.fn.expired()) edge(kv.second.env);
    for (auto& kv : generators) if (kv.second.gen && !kv.second.owner.expired()) genRoots(*kv.second.gen, edge);
    while (!work.empty()) {   // every list and map those reach, and the references between them
        const void* p = work.back();
        work.pop_back();
        if (nodes[p].map) { for (const auto& kv : *static_cast<const MapT*>(p)) edge(kv.second); }
        else for (const V& x : *static_cast<const ArrT*>(p)) edge(x);
    }
    auto live = [&](const V& v) {
        const void* p = nullptr;
        if (auto m = v.AsMap()) p = m.get();
        else if (auto a = v.AsArray()) p = a.get();
        if (!p) return;
        auto it = nodes.find(p);
        if (it != nodes.end() && !it->second.live) { it->second.live = true; work.push_back(p); }
    };
    auto keepEntry = [&](const void* key) {   // the lambda or generator `key` is in use: so is what its entry holds
        auto c = closures.find(key);
        if (c != closures.end()) live(c->second.env);
        auto g = generators.find(key);
        if (g != generators.end() && g->second.gen) genRoots(*g->second.gen, live);
    };
    for (auto& kv : nodes)
        if (kv.second.ext > kv.second.in) { kv.second.live = true; work.push_back(kv.first); }
    // A lambda or generator nothing in the walk refers to is held from outside (its entry's weak reference is alive).
    for (auto& kv : closures) if (!kv.second.fn.expired() && !nodes.count(kv.first)) keepEntry(kv.first);
    for (auto& kv : generators)
        if (kv.second.gen && !kv.second.owner.expired() && (!nodes.count(kv.first) || kv.second.gen->running)) {
            auto n = nodes.find(kv.first);
            if (n != nodes.end() && !n->second.live) { n->second.live = true; work.push_back(kv.first); }
            keepEntry(kv.first);
        }
    while (!work.empty()) {
        const void* p = work.back();
        work.pop_back();
        const Node& n = nodes[p];
        if (n.map) { for (const auto& kv : *static_cast<const MapT*>(p)) live(kv.second); }
        else for (const V& x : *static_cast<const ArrT*>(p)) live(x);
        keepEntry(p);
    }
    std::vector<const void*> deadClosures, deadGens;
    for (auto& kv : closures) {
        auto n = nodes.find(kv.first);
        if (n != nodes.end() && !n->second.live) deadClosures.push_back(kv.first);
    }
    for (auto& kv : generators) {
        auto n = nodes.find(kv.first);
        if (n != nodes.end() && !n->second.live) deadGens.push_back(kv.first);
    }
    // Move the entries out first and free them after: freeing one box can free another entry's lambda.
    std::vector<ClosureRef> freedC;
    std::vector<GenEntry> freedG;
    freedC.reserve(deadClosures.size());
    freedG.reserve(deadGens.size());
    for (const void* k : deadClosures) { auto it = closures.find(k); freedC.push_back(std::move(it->second)); closures.erase(it); }
    for (const void* k : deadGens) { auto it = generators.find(k); freedG.push_back(std::move(it->second)); generators.erase(it); }
    nodes.clear();
    return deadClosures.size() + deadGens.size();
}

// ---- Generators ----------------------------------------------------------------------------------------------------
template <class V>
V BasicRuntime<V>::MakeGenerator(ChunkPtr code, V* slots, unsigned char* state, std::size_t n, const V& env,
                                 const std::vector<std::string>* boxed) {
    auto g = std::make_unique<Generator<V>>();
    g->code = std::move(code);
    g->slots.assign(slots, slots + n);
    g->state.assign(state, state + n);
    g->env = env;
    g->boxed = boxed;
    g->self = self;
    g->selfClass = selfClass;
    g->methodClass = methodClass;
    if (generators.size() >= genSweepAt) {   // drop the state of generators no value refers to any more
        for (auto it = generators.begin(); it != generators.end();) it = it->second.owner.expired() ? generators.erase(it) : std::next(it);
        CollectCycles();
        genSweepAt = generators.size() * 2 > 64 ? generators.size() * 2 : 64;
    }
    // Found by the value's own map, not by a number in it: a float id stopped being unique after 2^24 generators,
    // and two generators could share one. "__gen" only marks the map now; its number is not used to find it.
    V v = V::MakeMap();
    auto m = v.AsMap();
    (*m)["__gen"] = V{(float)nextGenerator++};
    generators[m.get()] = GenEntry{std::weak_ptr<const void>(std::shared_ptr<const void>(m)), std::move(g)};
    return v;
}
template <class V> Generator<V>* BasicRuntime<V>::GeneratorOf(const V& v) {
    if (generators.empty()) return nullptr;
    auto m = v.AsMap();
    if (!m) return nullptr;
    if (!m->count("__gen")) return nullptr;
    auto g = generators.find(m.get());
    // A gone generator's entry stays until the next sweep: a new map at the same address is not that generator.
    if (g == generators.end() || g->second.owner.expired()) return nullptr;
    return g->second.gen.get();
}
template <class V> bool BasicRuntime<V>::Resume(Generator<V>& g, V& out) {
    if (g.done) return false;
    if (g.running) throw RuntimeError("a generator can't ask itself for its next value", 0);
    detail::CallDepthGuard depthGuard(CallDepth(), limits.maxCallDepth, [] { return std::string("a generator"); });
    const int callerLine = curLine;
    scopes.push_back(Env{});
    Env& e = scopes.back();
    if (!g.slots.empty()) { e.chunk = g.code.get(); e.slots = g.slots.data(); e.slotState = g.state.data(); }
    e.env = g.env;
    e.boxed = g.boxed;
    BumpVars();
    detail::PopScope<V> pop{*this};
    std::unique_ptr<detail::SelfScope<V>> selfScope;
    if (!g.selfClass.empty()) selfScope.reset(new detail::SelfScope<V>(*this, g.self, g.selfClass, g.methodClass));
    struct Running { bool& r; ~Running() { r = false; } } running{g.running};
    g.running = true;
    Flow f;
    try {
        f = RunChunk(*g.code, g.slots.empty() ? nullptr : g.slots.data(), g.state.empty() ? nullptr : g.state.data(), &g.run);
    } catch (...) {
        g.done = true;
        curLine = callerLine;
        throw;
    }
    curLine = callerLine;
    if (f == Flow::Yield) { out = std::move(g.run.value); return true; }
    g.done = true;   // it returned: its frame is no longer needed
    g.slots.clear();
    g.state.clear();
    g.run = GenRun<V>{};
    return false;
}
// g.next() (the next value, or 0 once it has finished), g.done() (true once it has finished: asking may run it to its
// next yield), g.to_list() (every value left). Iterator objects (MoveNext / Current) are used by foreach.
template <class V> bool BasicRuntime<V>::GeneratorMethod(const V& v, const std::string& name, std::vector<V>&, V& out) {
    Generator<V>* g = GeneratorOf(v);
    if (!g) return false;
    auto peeked = [&](V& val) {
        auto m = v.AsMap();
        auto it = m->find("__peek");
        if (it == m->end()) return false;
        val = it->second;
        m->erase(it);
        return true;
    };
    if (name == "next" || name == "Next") {
        if (!peeked(out) && !Resume(*g, out)) out = V{};
        return true;
    }
    if (name == "done" || name == "Done") {
        auto m = v.AsMap();
        if (m->count("__peek")) { out = V{false}; return true; }
        V val;
        if (Resume(*g, val)) { (*m)["__peek"] = val; out = V{false}; }
        else out = V{true};
        return true;
    }
    if (name == "to_list" || name == "ToList") {
        out = V::MakeArray();
        V val;
        if (peeked(val)) out.AsArray()->push_back(val);
        while (Resume(*g, val)) {
            out.AsArray()->push_back(val);
            if (out.AsArray()->size() > (std::size_t)1 << 24) throw RuntimeError("to_list: the generator gave more than 16777216 values", 0);
        }
        return true;
    }
    return false;
}

// ---- Modules -------------------------------------------------------------------------------------------------------
template <class V> void BasicRuntime<V>::AddProgram(const Program& prog, const std::string& source) {
    for (auto& kv : prog.functions) functions[kv.first] = &kv.second;
    BumpCalls();
    for (auto& kv : prog.classes) classes[kv.first] = &kv.second;
    localNames.insert(prog.localNames.begin(), prog.localNames.end());
    statics = SharedStatics(source);   // `static` variables are shared by every run of this source
}
template <class V> BasicRuntime<V>* BasicRuntime<V>::ModuleOf(const V& fn) {
    if (!modules) return nullptr;
    auto m = fn.AsMap();
    if (!m) return nullptr;
    auto it = m->find("__mod");
    const int id = it == m->end() ? 0 : detail::SafeInt(it->second.AsFloat());
    if (id == moduleId || id < 0 || id >= (int)modules->byId.size()) return nullptr;
    return modules->byId[(std::size_t)id];
}
// import "file.okay": the file runs once, in a runtime of its own (its own globals and functions) with this one's
// natives, builtins, host and limits. Its exports are a map of its globals and its functions ({"__fn", "__mod"}).
template <class V> V BasicRuntime<V>::Import(const std::string& path, int line) {
    if (!modules) {
        modules = std::make_shared<ModuleSystem<V>>();
        modules->byId.push_back(this);
    }
    if (!modules->load) throw RuntimeError("import: this program can't load modules", Ln(line));
    std::string resolved, source, error;
    if (!modules->load(path, currentFile, resolved, source, error))
        throw RuntimeError("import \"" + path + "\": " + (error.empty() ? std::string("can't read it") : error), Ln(line));
    for (const std::string& l : modules->loading)
        if (l == resolved) {
            std::string chain;
            for (const std::string& c : modules->loadingNames) chain += c + " -> ";
            throw RuntimeError("import cycle: " + chain + path, Ln(line));
        }
    auto found = modules->byPath.find(resolved);
    if (found != modules->byPath.end()) return found->second->exports;   // loaded once, then shared
    auto mod = std::make_unique<typename ModuleSystem<V>::Module>();
    mod->path = resolved;
    mod->rt = std::make_unique<BasicRuntime<V>>();
    BasicRuntime<V>& r = *mod->rt;
    r.natives = natives;
    r.builtins = builtins;
    r.namedArgBuiltins = namedArgBuiltins;
    r.scriptFunctionsFirst = scriptFunctionsFirst;
    r.hooks = hooks;
    r.limits = limits;
    r.out = out;
    r.in = in;
    r.canonEvent = canonEvent;
    r.modules = modules;
    r.moduleId = (int)modules->byId.size();
    r.currentFile = resolved;
    modules->byId.push_back(&r);
    mod->prog = std::make_unique<Program>();
    try {
        ParseSource(source, *mod->prog);
    } catch (const ScriptError& e) {
        modules->byId.back() = nullptr;
        throw RuntimeError("in " + path + (e.line ? ":" + std::to_string(e.line) : std::string()) + ": " + e.what(), Ln(line));
    }
    r.AddProgram(*mod->prog, source);
    typename ModuleSystem<V>::Module& m = *mod;
    modules->byPath[resolved] = std::move(mod);
    modules->loading.push_back(resolved);
    modules->loadingNames.push_back(path);
    struct Done {
        ModuleSystem<V>& m;
        ~Done() { m.loading.pop_back(); m.loadingNames.pop_back(); }
    } done{*modules};
    try {
        r.RunBody(m.prog->top, m.prog->topCode);
    } catch (ExitRequest&) {
        throw;
    } catch (const ScriptError& e) {
        modules->byId[(std::size_t)r.moduleId] = nullptr;
        modules->byPath.erase(resolved);   // a failed module is not kept: importing it again tries again
        throw RuntimeError("in " + path + (e.line ? ":" + std::to_string(e.line) : std::string()) + ": " + e.what(), Ln(line));
    }
    V ex = V::MakeMap();
    for (auto& kv : r.Global().vars)
        if (kv.first.compare(0, 2, "__") != 0) (*ex.AsMap())[kv.first] = kv.second;
    for (auto& kv : m.prog->functions) {
        V fn = V::MakeMap();
        (*fn.AsMap())["__fn"] = V{kv.first};
        (*fn.AsMap())["__mod"] = V{(float)r.moduleId};
        (*ex.AsMap())[kv.first] = fn;
    }
    m.exports = ex;
    m.done = true;
    return ex;
}
// `a[i] op= rhs` on a class object, a list or a map (the caller has checked it is one of those).
template <class V>
V BasicRuntime<V>::IndexCompound(const V& a, const V& rhs, const V& index, const IndexCompoundAssignExpr& e) {
    using detail::SafeInt;
    auto apply = [&](const V& cur) -> V {
        if (detail::IntOf(cur) || detail::IntOf(rhs)) return detail::Arith<V>(e.op, cur, rhs);   // whole numbers stay exact
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
