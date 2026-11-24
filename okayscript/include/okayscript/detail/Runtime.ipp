#pragma once
// Scopes, typed variables and calls (natives, script functions, lambdas, methods by dotted name) of the OkayScript core.
// Written for this project; the rules are the engine OkayScriptVM's (engine/src/Scripting/OkayScriptVM.cpp).
#include "okayscript/Runtime.hpp"
#include "okayscript/detail/Support.hpp"
#include "okayscript/detail/ValueMethods.ipp"

namespace okayscript {

namespace detail {
// The error names the function only when it is raised: building that text on every call cost a heap allocation.
struct CallDepthGuard {
    int& depth;
    template <class MakeWhat> CallDepthGuard(int& d, int max, MakeWhat what) : depth(d) {
        if (depth >= max) throw RuntimeError(std::string(what()) + " calls itself too deeply (endless recursion?)", 0);
        ++depth;
    }
    ~CallDepthGuard() { --depth; }
    CallDepthGuard(const CallDepthGuard&) = delete;
    CallDepthGuard& operator=(const CallDepthGuard&) = delete;
};
inline bool AnyNamed(const std::vector<std::string>* names) {
    if (!names) return false;
    for (const std::string& n : *names) if (!n.empty()) return true;
    return false;
}
// Lay call arguments onto parameter slots. Extra positional arguments are ignored (engine rule).
template <class V>
void PlaceArgs(const std::vector<std::string>& params, const std::vector<V>& args, const std::vector<std::string>* names,
               std::vector<char>& present, std::vector<V>& slot, const std::string& what) {
    present.assign(params.size(), 0);
    slot.assign(params.size(), V{});
    bool sawNamed = false;
    std::size_t next = 0;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string nm = (names && i < names->size()) ? (*names)[i] : std::string();
        if (nm.empty()) {
            if (sawNamed) throw RuntimeError(what + ": a value without a name can't follow a named one", 0);
            if (next >= params.size()) continue;
            slot[next] = args[i]; present[next] = 1; ++next;
        } else {
            sawNamed = true;
            std::size_t idx = params.size();
            for (std::size_t p = 0; p < params.size(); ++p) if (params[p] == nm) { idx = p; break; }
            if (idx == params.size()) throw RuntimeError(what + " has no parameter '" + nm + "'", 0);
            if (present[idx]) throw RuntimeError(what + ": '" + nm + "' was given twice", 0);
            slot[idx] = args[i]; present[idx] = 1;
        }
    }
}
template <class V> struct PopScope {
    BasicRuntime<V>& rt;
    ~PopScope() { rt.scopes.pop_back(); rt.BumpVars(); }
};

/// The operand stacks and local slots of the running chunks on one thread. A chunk takes its slots on entry and gives them back on
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

/// A function's or lambda's locals while it runs: one value and one state byte per slot (Chunk::slotNames), all
/// "not declared yet" at the start. Taken from the thread's ValueStack, so they never move while the body calls out.
template <class V> struct Frame {
    ValueStack<V>& st;
    std::size_t n;
    V* slots = nullptr;
    unsigned char* state = nullptr;
    unsigned char fixed[16];
    std::unique_ptr<unsigned char[]> more;
    explicit Frame(const Chunk& c) : st(ValueStack<V>::ForThisThread()), n(c.slotNames.size()) {
        if (!n) return;
        slots = st.Take(n);
        if (n > sizeof fixed) { more.reset(new unsigned char[n]); state = more.get(); } else state = fixed;
        for (std::size_t i = 0; i < n; ++i) state[i] = 0;
    }
    ~Frame() { if (n) st.Give(slots, n); }
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
    /// Hang the slots on the scope the call just opened, so the names are found from the functions it calls.
    void Attach(BasicEnvironment<V>& e, const Chunk& c) const {
        if (!n) return;
        e.chunk = &c;
        e.slots = slots;
        e.slotState = state;
    }
};
/// Write v to a declared slot with its type rule (state = kDeclared | kind).
template <class V> void StoreTypedSlot(V& slot, unsigned char state, const V& v, const std::string& name) {
    const unsigned char kind = (unsigned char)(state & 0x7F);
    if (kind & KConst) throw RuntimeError("'" + name + "' is const, so it can't change after it's set", 0);
    slot = (kind & 0x0F) ? CoerceToKind<V>(kind, v, name) : v;
}
} // namespace detail

// ---- Scopes ------------------------------------------------------------------------------------------------------
template <class V> void BasicRuntime<V>::Define(const std::string& n, const V& v) {
    if (scopes.size() > 1 && !localNames.count(n)) { localNames.insert(n); BumpGlobals(); }   // keeps Lookup's shortcut true
    Env& e = scopes.back();
    const int slot = e.SlotOf(n);
    if (slot >= 0) {   // a local of the running function: its slot
        e.slots[slot] = v;
        if (e.slotState[slot] != kDeclared) { e.slotState[slot] = kDeclared; BumpVars(); }
        return;
    }
    auto ins = e.vars.try_emplace(n);
    ins.first->second = v;
    // A new name may hide one further out, and a dropped type rule changes how it is written: places cached by
    // bytecode are stale. Setting a name this scope already has changes nothing they rely on.
    if (ins.second) BumpVars();
    else if (!e.kinds.empty() && e.kinds.erase(n)) { BumpVars(); BumpGlobals(); }
}
template <class V> void BasicRuntime<V>::DefineTyped(const std::string& n, const V& v, unsigned char kind) {
    if (scopes.size() > 1 && !localNames.count(n)) localNames.insert(n);
    BumpGlobals();   // a type rule may change on a global
    Env& e = scopes.back();
    const int slot = e.SlotOf(n);
    if (slot >= 0) {
        e.slots[slot] = (kind & 0x0F) ? CoerceToKind<V>(kind, v, n) : v;
        e.slotState[slot] = (unsigned char)(kDeclared | (kind & 0x1F));
        BumpVars();
        return;
    }
    e.vars[n] = (kind & 0x0F) ? CoerceToKind<V>(kind, v, n) : v;
    if (kind) e.kinds[n] = kind; else if (!e.kinds.empty()) e.kinds.erase(n);
    BumpVars();
}
namespace detail {
template <class V> bool AssignIn(BasicEnvironment<V>& e, const std::string& n, const V& v) {
    if (e.chunk) {
        const int i = e.SlotOf(n);
        if (i >= 0 && (e.slotState[i] & kDeclared)) {
            if (e.slotState[i] == kDeclared) e.slots[i] = v;
            else StoreTypedSlot<V>(e.slots[i], e.slotState[i], v, n);
            return true;
        }
    }
    auto f = e.vars.find(n);
    if (f == e.vars.end()) return false;
    if (!e.kinds.empty()) {
        auto k = e.kinds.find(n);
        if (k != e.kinds.end()) {
            if (k->second & KConst) throw RuntimeError("'" + n + "' is const, so it can't change after it's set", 0);
            f->second = (k->second & 0x0F) ? CoerceToKind<V>(k->second, v, n) : v;
            return true;
        }
    }
    f->second = v;
    return true;
}
} // namespace detail
// Every scope on the stack is searched, innermost first, then the globals (dynamic scoping, as in the engine): a
// function also sees the locals of the functions that called it, and a lambda the locals of whoever calls it.
// Then the shared statics, then (inside a method) the fields of the object it runs on.
template <class V> void BasicRuntime<V>::Assign(const std::string& n, const V& v) {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) if (detail::AssignIn(*it, n, v)) return;
    if (statics && detail::AssignIn(*statics, n, v)) return;
    if (auto* f = SelfField(n)) { WriteField(self, *f, v); return; }
    Global().vars[n] = v;   // a new name becomes a global
}
namespace detail {
template <class V> void FillCache(NameCache* c, const BasicEnvironment<V>& e, const std::string& n, const V* p,
                                  unsigned long long epoch) {
    if (!c) return;
    c->epoch = epoch;
    c->value = const_cast<V*>(p);
    c->writable = e.kinds.empty() || !e.kinds.count(n);
}
inline void FillCache(NameCache* c, void* p, bool writable, unsigned long long epoch) {
    if (!c) return;
    c->epoch = epoch;
    c->value = p;
    c->writable = writable;
}
/// A declared slot named n in e, or -1.
template <class V> int DeclaredSlot(const BasicEnvironment<V>& e, const std::string& n) {
    const int i = e.SlotOf(n);
    return (i >= 0 && (e.slotState[i] & kDeclared)) ? i : -1;
}
} // namespace detail
template <class V> void BasicRuntime<V>::AssignCached(const std::string& n, const V& v, NameCache& cache) {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        if (!detail::AssignIn(*it, n, v)) continue;
        const int i = it->chunk ? detail::DeclaredSlot(*it, n) : -1;
        if (i >= 0) detail::FillCache(&cache, &it->slots[i], it->slotState[i] == kDeclared, varEpoch);
        else detail::FillCache(&cache, *it, n, &it->vars.find(n)->second, &*it == &scopes.front() ? GlobalEpochFor(n) : varEpoch);
        return;
    }
    if (statics && detail::AssignIn(*statics, n, v)) return;
    if (auto* f = SelfField(n)) { WriteField(self, *f, v); return; }
    auto& slot = Global().vars[n];   // a new name becomes a global
    slot = v;
    detail::FillCache(&cache, Global(), n, &slot, GlobalEpochFor(n));
}
template <class V> const V* BasicRuntime<V>::Lookup(const std::string& n, NameCache* cache) const {
    if (scopes.size() > 1 && !localNames.count(n)) {
        // Not a local anywhere: only the globals (scopes[0]) and the statics can hold it.
        auto f = scopes.front().vars.find(n);
        if (f != scopes.front().vars.end()) { detail::FillCache(cache, scopes.front(), n, &f->second, globalEpoch); return &f->second; }
        if (statics && !statics->vars.empty()) { auto g = statics->vars.find(n); if (g != statics->vars.end()) return &g->second; }
        return nullptr;
    }
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        if (it->chunk) {
            const int i = detail::DeclaredSlot(*it, n);
            if (i >= 0) { detail::FillCache(cache, &it->slots[i], it->slotState[i] == kDeclared, varEpoch); return &it->slots[i]; }
        }
        if (it->vars.empty()) continue;
        auto f = it->vars.find(n);
        if (f != it->vars.end()) {
            detail::FillCache(cache, *it, n, &f->second, &*it == &scopes.front() ? GlobalEpochFor(n) : varEpoch);
            return &f->second;
        }
    }
    if (statics && !statics->vars.empty()) { auto f = statics->vars.find(n); if (f != statics->vars.end()) return &f->second; }
    return nullptr;
}
template <class V> V BasicRuntime<V>::Get(const std::string& n) const {
    if (const V* p = Lookup(n)) return *p;
    if (!selfClass.empty() && const_cast<BasicRuntime*>(this)->SelfField(n)) {
        auto mp = self.AsMap();
        if (!mp) return V{};
        auto it = mp->find(n);
        return it == mp->end() ? V{} : it->second;
    }
    return V{};   // never set: 0
}
template <class V> bool BasicRuntime<V>::Has(const std::string& n) const {
    if (Lookup(n)) return true;
    return !selfClass.empty() && const_cast<BasicRuntime*>(this)->SelfField(n) != nullptr;
}

// ---- Statics -----------------------------------------------------------------------------------------------------
template <class V> void BasicRuntime<V>::DefineStatic(const std::string& n, const V& v, unsigned char kind) {
    BumpVars();
    BumpGlobals();
    if (!statics) statics = std::make_shared<Env>();
    if (statics->vars.count(n)) return;   // only the first run sets it
    statics->vars[n] = (kind & 0x0F) ? CoerceToKind<V>(kind, v, n) : v;
    statics->kinds[n] = kind;
}
template <class V> std::shared_ptr<BasicEnvironment<V>> BasicRuntime<V>::SharedStatics(const std::string& source) {
    static std::unordered_map<std::size_t, std::shared_ptr<Env>> all;
    auto& e = all[std::hash<std::string>{}(source)];
    if (!e) e = std::make_shared<Env>();
    return e;
}

// ---- Plain classes -----------------------------------------------------------------------------------------------
template <class V> UserClass* BasicRuntime<V>::ClassOf(const V& inst) {
    auto mp = inst.AsMap();
    if (!mp) return nullptr;
    auto tag = mp->find("__class");
    if (tag == mp->end() || !tag->second.IsString()) return nullptr;
    auto cit = classes.find(tag->second.AsString());
    return cit == classes.end() ? nullptr : const_cast<UserClass*>(cit->second);
}
template <class V> bool BasicRuntime<V>::SameInstance(const V& inst) const {
    auto a = self.AsMap(), b = inst.AsMap();
    return a && b && a.get() == b.get();
}
template <class V> bool BasicRuntime<V>::HasMethod(const std::string& name) {
    if (selfClass.empty() || name.find('.') != std::string::npos) return false;
    auto cit = classes.find(selfClass);
    return cit != classes.end() && cit->second->methods.count(name) != 0;
}
namespace detail {
inline const FieldDef* FindField(const UserClass& c, const std::string& n) {
    for (auto& f : c.fields) if (f.name == n) return &f;
    return nullptr;
}
} // namespace detail
template <class V> const FieldDef* BasicRuntime<V>::SelfField(const std::string& n) {
    if (selfClass.empty()) return nullptr;
    auto cit = classes.find(selfClass);
    if (cit == classes.end()) return nullptr;
    return detail::FindField(*cit->second, n);
}
template <class V> void BasicRuntime<V>::WriteField(const V& inst, const FieldDef& f, const V& v) {
    auto mp = inst.AsMap();
    if (!mp) throw RuntimeError("can't set '" + f.name + "': the object is null", 0);
    (*mp)[f.name] = (f.kind & 0x0F) ? CoerceToKind<V>(f.kind, v, f.name) : v;
}
template <class V> V BasicRuntime<V>::GetMember(const V& inst, const std::string& name, bool external) {
    UserClass* c = ClassOf(inst);
    if (!c) throw RuntimeError("no field '" + name + "' on this value", 0);
    const FieldDef* f = detail::FindField(*c, name);
    if (!f) throw RuntimeError(c->name + " has no field '" + name + "'", 0);
    if (external && !f->isPublic && !SameInstance(inst)) throw RuntimeError("'" + name + "' is private on " + c->name, 0);
    auto mp = inst.AsMap();
    auto it = mp->find(name);
    return it == mp->end() ? V{} : it->second;
}
template <class V> void BasicRuntime<V>::SetMember(const V& inst, const std::string& name, const V& v, bool external) {
    UserClass* c = ClassOf(inst);
    if (!c) throw RuntimeError("can't set '" + name + "' on this value", 0);
    const FieldDef* f = detail::FindField(*c, name);
    if (!f) throw RuntimeError(c->name + " has no field '" + name + "'", 0);
    if (external && !f->isPublic && !SameInstance(inst)) throw RuntimeError("'" + name + "' is private on " + c->name, 0);
    WriteField(inst, *f, v);
}
namespace detail {
// Runs `fn` with `self` set to an object, and puts the old one back afterwards (also when it throws).
template <class V> struct SelfScope {
    BasicRuntime<V>& r; V prev; std::string prevClass;
    SelfScope(BasicRuntime<V>& rt, const V& inst, const std::string& cls) : r(rt), prev(rt.self), prevClass(rt.selfClass) {
        r.self = inst; r.selfClass = cls;
    }
    ~SelfScope() { r.self = prev; r.selfClass = prevClass; }
    SelfScope(const SelfScope&) = delete;
    SelfScope& operator=(const SelfScope&) = delete;
};
} // namespace detail
template <class V> V BasicRuntime<V>::CallMethod(const V& inst, const std::string& method, std::vector<V>& args, bool external) {
    UserClass* c = ClassOf(inst);
    if (!c) throw RuntimeError("no method '" + method + "' on this value", 0);
    auto mit = c->methods.find(method);
    if (mit == c->methods.end()) {
        if (const FieldDef* f = detail::FindField(*c, method)) {
            if (external && !f->isPublic && !SameInstance(inst))
                throw RuntimeError("'" + method + "' is private on " + c->name, 0);
            auto mp = inst.AsMap();
            auto it = mp->find(method);
            V fv = it == mp->end() ? V{} : it->second;
            if (IsLambda(fv)) return CallLambda(fv, args);
        }
        throw RuntimeError(c->name + " has no method '" + method + "'", 0);
    }
    if (external && !mit->second.isPublic && !SameInstance(inst))
        throw RuntimeError("'" + method + "' is private on " + c->name, 0);
    detail::SelfScope<V> scope(*this, inst, c->name);
    FunctionDecl& decl = mit->second.decl;
    if (decl.name.empty()) decl.name = c->name + "." + method;
    return Invoke(decl, args, nullptr);
}
template <class V> V BasicRuntime<V>::MakeInstance(const std::string& type, std::vector<V>& args) {
    detail::CallDepthGuard guard(CallDepth(), limits.maxCallDepth, [&] { return "new " + type; });
    auto cit = classes.find(type);
    if (cit == classes.end()) throw RuntimeError("no class named '" + type + "'", 0);
    UserClass& c = *const_cast<UserClass*>(cit->second);
    V inst = V::MakeMap();
    (*inst.AsMap())["__class"] = V{type};
    detail::SelfScope<V> scope(*this, inst, type);
    for (auto& f : c.fields) {
        V v = f.init ? EvalCode(*f.init, f.initCode) : DefaultForKind<V>(f.kind);
        if (f.kind & 0x0F) v = CoerceToKind<V>(f.kind, v, f.name);
        (*inst.AsMap())[f.name] = std::move(v);
    }
    auto ct = c.ctors.find((int)args.size());
    if (ct != c.ctors.end()) {
        if (ct->second.decl.name.empty()) ct->second.decl.name = type;
        Invoke(ct->second.decl, args, nullptr);
    } else if (!args.empty() || !c.ctors.empty()) {
        throw RuntimeError(type + " has no constructor that takes " + std::to_string(args.size()) +
                           (args.size() == 1 ? " value" : " values"), 0);
    }
    return inst;
}

// ---- Events ------------------------------------------------------------------------------------------------------
namespace detail {
template <class V> bool SameListener(const V& a, const V& b) {
    auto ma = a.AsMap(), mb = b.AsMap();
    if (!ma || !mb) return false;
    if (ma->count("__lambda") && mb->count("__lambda")) return (*ma)["__lambda"].AsFloat() == (*mb)["__lambda"].AsFloat();
    if (ma->count("__fn") && mb->count("__fn")) return (*ma)["__fn"].AsString() == (*mb)["__fn"].AsString();
    return false;
}
} // namespace detail
template <class V> void BasicRuntime<V>::AddSub(const std::string& event, const V& fn) { subs.push_back(Sub{event, fn}); }
template <class V> void BasicRuntime<V>::RemoveSub(const std::string& event, const V& fn) {
    auto canon = [&](const std::string& n) { return canonEvent ? canonEvent(n) : n; };
    const std::string c = canon(event);
    for (auto it = subs.begin(); it != subs.end(); ++it)
        if (canon(it->name) == c && detail::SameListener(it->fn, fn)) { subs.erase(it); return; }
}
template <class V> bool BasicRuntime<V>::HasSub(const std::string& event) const {
    auto canon = [&](const std::string& n) { return canonEvent ? canonEvent(n) : n; };
    const std::string c = canon(event);
    for (const auto& s : subs) if (canon(s.name) == c) return true;
    return false;
}
template <class V> void BasicRuntime<V>::FireSubs(const std::string& event, std::vector<V>& args) {
    auto canon = [&](const std::string& n) { return canonEvent ? canonEvent(n) : n; };
    const std::string c = canon(event);
    const std::vector<Sub> snap = subs;
    for (const Sub& s : snap) {
        if (canon(s.name) != c) continue;
        auto m = s.fn.AsMap();
        if (!m) continue;
        if (m->count("__fn")) {
            const std::string fn = (*m)["__fn"].AsString();
            if (functions.count(fn)) CallFunction(fn, args);   // the script's own function, even when a builtin has its name
            continue;
        }
        if (IsLambda(s.fn)) CallLambda(s.fn, args);
    }
}

template <class V> void BasicRuntime<V>::CountIteration(long long& n, const char* what) const {
    if (limits.maxLoopIterations > 0 && ++n > limits.maxLoopIterations)
        throw RuntimeError(std::string(what) + " exceeded iteration limit", 0);
}

// ---- Dotted names ------------------------------------------------------------------------------------------------
template <class V> V BasicRuntime<V>::GetDotted(const std::string& name, bool& ok) {
    ok = true;
    V out;
    if (hooks && hooks->ReadName(*this, name, out)) return out;
    const auto dot = name.rfind('.');
    if (dot != std::string::npos) {
        const std::string base = name.substr(0, dot), prop = name.substr(dot + 1);
        if (base == "this" && detail::IsClassInstance(self)) return GetMember(self, prop, false);
        if (base.find('.') == std::string::npos) {
            if (Has(base)) {
                const V bv = Get(base);
                if (detail::IsClassInstance(bv)) return GetMember(bv, prop, true);
                if (hooks && hooks->GetMember(*this, bv, prop, out)) return out;
                if (detail::ValueProperty<V>(bv, prop, out)) return out;
                if (hooks && hooks->GetMemberLast(*this, bv, prop, out)) return out;
            }
        } else {
            bool inner = false;
            const V bv = GetDotted(base, inner);
            if (inner && detail::IsClassInstance(bv)) return GetMember(bv, prop, true);
            if (inner && hooks && hooks->GetMember(*this, bv, prop, out)) return out;
            if (inner && detail::ValueProperty<V>(bv, prop, out)) return out;
            if (inner && hooks && hooks->GetMemberLast(*this, bv, prop, out)) return out;
        }
    }
    ok = false;
    return V{};
}
template <class V> bool BasicRuntime<V>::SetDotted(const std::string& name, const V& v) {
    const auto dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    const std::string base = name.substr(0, dot), field = name.substr(dot + 1);
    if (base.find('.') == std::string::npos) {
        if (base == "this" && detail::IsClassInstance(self)) { SetMember(self, field, v, false); return true; }
        if (Has(base) && detail::IsClassInstance(Get(base))) { SetMember(Get(base), field, v, true); return true; }
    }
    if (hooks && hooks->WriteName(*this, name, v)) return true;
    V bv;
    if (base.find('.') == std::string::npos) {
        if (!Has(base)) return false;
        bv = Get(base);
    } else {
        bool ok = false;
        bv = GetDotted(base, ok);
        if (!ok) return false;
    }
    if (auto m = bv.AsMap()) { (*m)[field] = v; return true; }   // m.key = v
    // v.x = 3 on a variable holding a vector
    if (base.find('.') == std::string::npos && field.size() == 1 && (field[0] == 'x' || field[0] == 'y' || field[0] == 'z')) {
        auto vec = bv.AsVec3();
        if (field[0] == 'x') vec.x = v.AsFloat(); else if (field[0] == 'y') vec.y = v.AsFloat(); else vec.z = v.AsFloat();
        Assign(base, V{vec});
        return true;
    }
    return false;
}

// ---- Calls -------------------------------------------------------------------------------------------------------

template <class V> bool BasicRuntime<V>::IsLambda(const V& v) { return detail::IsLambdaValue(v); }

template <class V>
V BasicRuntime<V>::Invoke(const FunctionDecl& fn, std::vector<V>& args, const std::vector<std::string>* names) {
    return Invoke(fn, args.data(), args.size(), names);
}
namespace detail {
/// Parameter i of a call: its slot, value and (for `int dmg`) type rule.
template <class V> void BindParam(const Chunk& code, Frame<V>& frame, std::size_t i, V&& v, unsigned char kind,
                                  const std::string& name) {
    const int at = code.paramSlot[i];
    if (!kind) { frame.slots[at] = std::move(v); frame.state[at] = kDeclared; return; }
    frame.slots[at] = (kind & 0x0F) ? CoerceToKind<V>(kind, v, name) : std::move(v);
    frame.state[at] = (unsigned char)(kDeclared | (kind & 0x1F));
}
/// Default argument i (worked out in the called function's frame, so it sees the parameters before it).
template <class V> V DefaultArg(BasicRuntime<V>& rt, const std::vector<ExprPtr>& defaults, std::vector<ChunkPtr>& code,
                                std::size_t i) {
    if (code.size() < defaults.size()) code.resize(defaults.size());
    return rt.EvalCode(*defaults[i], code[i]);
}
} // namespace detail

template <class V>
V BasicRuntime<V>::Invoke(const FunctionDecl& fn, const V* args, std::size_t argc, const std::vector<std::string>* names) {
    auto what = [&fn] { return fn.name + "()"; };
    detail::CallDepthGuard depthGuard(CallDepth(), limits.maxCallDepth, what);
    const Chunk& code = CodeFor(fn.params, fn.body, fn.code);
    detail::Frame<V> frame(code);   // the locals: slots, not a scope map
    const int callerLine = curLine;
    scopes.push_back(Env{});
    frame.Attach(scopes.back(), code);
    BumpVars();
    detail::PopScope<V> pop{*this};
    const std::size_t np = fn.params.size();
    if (!names) {
        // Positional arguments (nearly every call): bind them straight, no slot lists.
        for (std::size_t i = 0; i < np; ++i) {
            const unsigned char k = i < fn.paramKinds.size() ? fn.paramKinds[i] : 0;
            V v;
            if (i < argc) v = args[i];
            else if (i < fn.defaults.size() && fn.defaults[i]) v = detail::DefaultArg(*this, fn.defaults, fn.defaultCode, i);
            else if (k) v = DefaultForKind<V>(k);
            detail::BindParam(code, frame, i, std::move(v), k, fn.params[i]);
        }
    } else {
        std::vector<char> present;
        std::vector<V> slot, given(args, args + argc);
        detail::PlaceArgs<V>(fn.params, given, names, present, slot, what());
        for (std::size_t i = 0; i < np; ++i) {
            const unsigned char k = i < fn.paramKinds.size() ? fn.paramKinds[i] : 0;
            V v;
            if (present[i]) v = slot[i];
            else if (i < fn.defaults.size() && fn.defaults[i]) v = detail::DefaultArg(*this, fn.defaults, fn.defaultCode, i);
            else v = k ? DefaultForKind<V>(k) : V{};
            detail::BindParam(code, frame, i, std::move(v), k, fn.params[i]);
        }
    }
    BumpVars();   // the parameters are new names in this scope
    V ret;
    // A stray break/continue (no loop around it) ends the function, like return.
    if (RunChunk(code, frame.slots, frame.state) == Flow::Return) ret = std::move(retVal);
    curLine = callerLine;
    if (fn.returnKind & 0x0F) return CoerceToKind<V>(fn.returnKind, ret, what());
    return ret;
}

template <class V> V BasicRuntime<V>::CallFunction(const std::string& name, std::vector<V>& args,
                                                   const std::vector<std::string>* names) {
    auto f = functions.find(name);
    if (f != functions.end() && !HasMethod(name)) return Invoke(*f->second, args, names);
    return Call(name, args, names);
}

template <class V>
V BasicRuntime<V>::Call(const std::string& name, std::vector<V>& args, const std::vector<std::string>* names, int line) {
    line = Ln(line);
    {
        auto bi = builtins.find(name);
        if (bi != builtins.end() && (natives.empty() || !natives.count(name))) {
            if (detail::AnyNamed(names) && !namedArgBuiltins.count(name))
                throw RuntimeError("'" + name + "' doesn't take named arguments", line);
            const std::vector<std::string>* prev = activeArgNames;
            activeArgNames = names;
            struct Restore { const std::vector<std::string>*& p; const std::vector<std::string>* v; ~Restore() { p = v; } } restore{activeArgNames, prev};
            return bi->second(args);
        }
        auto nat = natives.empty() ? natives.end() : natives.find(name);
        if (nat != natives.end()) {
            if (detail::AnyNamed(names)) throw RuntimeError("'" + name + "' doesn't take named arguments", line);
            const BasicNative<V>& n = nat->second;
            if (!n.arity.Accepts(args.size()))
                throw RuntimeError(name + "() takes " + n.arity.Describe() + ", got " + std::to_string(args.size()), line);
            BasicArgs<V> a(*this, name, args, line);
            try {
                return n.fn(a);
            } catch (const ScriptError&) {
                throw;
            } catch (const std::exception& e) {   // a C++ error inside a native is a script error on this line
                throw RuntimeError(name + "(): " + e.what(), line);
            }
        }
        if (HasMethod(name)) return CallMethod(self, name, args, false);   // inside a method, its class's own first
        auto f = functions.find(name);
        if (f != functions.end()) return Invoke(*f->second, args, names);

        // `name.Method(...)` on a variable: s.ToUpper(), xs.Add(3), m.ContainsKey("a").
        const auto dot = name.rfind('.');
        if (dot != std::string::npos && dot > 0) {
            const std::string base = name.substr(0, dot), method = name.substr(dot + 1);
            bool ok = false;
            V obj;
            if (base.find('.') == std::string::npos) {
                if (base == "this" && detail::IsClassInstance(self)) { obj = self; ok = true; }
                else if (Has(base)) { obj = Get(base); ok = true; }
            }
            if (!ok) obj = GetDotted(base, ok);
            if (ok && detail::IsClassInstance(obj)) return CallMethod(obj, method, args, true);
            V out;
            if (ok && hooks && hooks->CallMember(*this, obj, method, args, out)) return out;
            detail::LambdaInvoker<V> inv = [this](const V& fv, std::vector<V>& xs) { return CallLambda(fv, xs); };
            if (ok && detail::CallValueMethod<V>(obj, method, args, out, &inv)) return out;
            if (ok && obj.AsMap()) {   // a lambda stored in a map: m.handler(1)
                auto it = obj.AsMap()->find(method);
                if (it != obj.AsMap()->end() && IsLambda(it->second)) return CallLambda(it->second, args, names);
            }
        }
        if (Has(name) && IsLambda(Get(name))) return CallLambda(Get(name), args, names);   // Action f = () => ...; f();
        V out;
        if (hooks && hooks->CallUnknown(*this, name, args, out)) return out;
        throw RuntimeError("call to undefined function '" + name + "'", line);
    }
}

template <class V> V BasicRuntime<V>::CallLambda(const V& f, std::vector<V>& args, const std::vector<std::string>* names) {
    auto m = f.AsMap();
    if (!m) throw RuntimeError("not a function", 0);
    auto it = m->find("__lambda");
    if (it == m->end()) throw RuntimeError("not a function", 0);
    auto lb = lambdas.find(detail::SafeInt(it->second.AsFloat()));
    if (lb == lambdas.end()) throw RuntimeError("lambda is no longer available", 0);
    detail::CallDepthGuard depthGuard(CallDepth(), limits.maxCallDepth, [] { return std::string("a lambda"); });
    const int callerLine = curLine;
    const auto& ps = *lb->second.params;
    const std::vector<ExprPtr>* defs = lb->second.defaults;
    std::vector<char> present;
    std::vector<V> slot;
    detail::PlaceArgs<V>(ps, args, names, present, slot, "lambda");
    const Chunk& code = CodeFor(ps, *lb->second.body, *lb->second.code);
    detail::Frame<V> frame(code);
    scopes.push_back(Env{});
    frame.Attach(scopes.back(), code);
    BumpVars();
    detail::PopScope<V> pop{*this};
    for (std::size_t i = 0; i < ps.size(); ++i) {
        V v;
        if (present[i]) v = slot[i];
        else if (defs && i < defs->size() && (*defs)[i]) v = detail::DefaultArg(*this, *defs, *lb->second.defaultCode, i);
        Define(ps[i], v);
    }
    V ret;
    if (RunChunk(code, frame.slots, frame.state) == Flow::Return) ret = std::move(retVal);
    curLine = callerLine;
    return ret;
}

template <class V> V BasicRuntime<V>::CallValue(const V& fn, std::vector<V>& args, const std::vector<std::string>* names) {
    if (IsLambda(fn)) return CallLambda(fn, args, names);
    if (auto m = fn.AsMap()) {
        auto it = m->find("__fn");
        if (it != m->end()) return Call(it->second.AsString(), args, names);
    }
    if (fn.IsString()) return Call(fn.AsString(), args, names);
    throw RuntimeError(std::string("not a function (") + detail::ValueKindName(fn) + ")", 0);
}

// ---- Natives -----------------------------------------------------------------------------------------------------
template <class V> void BasicArgs<V>::Fail(const std::string& message) const { throw RuntimeError(m_name + "(): " + message, m_line); }
template <class V> void BasicArgs<V>::Need(std::size_t i, const char* type) const {
    if (i >= m_values.size()) Fail("argument " + std::to_string(i + 1) + " is missing (" + type + " expected)");
    Fail("argument " + std::to_string(i + 1) + " must be " + type + ", got " + detail::ValueKindName(m_values[i]));
}
template <class V> double BasicArgs<V>::Number(std::size_t i) const {
    if (i >= m_values.size() || !m_values[i].IsNumber()) Need(i, "a number");
    return m_values[i].AsFloat();
}
template <class V> std::string BasicArgs<V>::String(std::size_t i) const {
    if (i >= m_values.size() || !m_values[i].IsString()) Need(i, "text");
    return m_values[i].AsString();
}
template <class V> bool BasicArgs<V>::Bool(std::size_t i) const {
    if (i >= m_values.size() || !m_values[i].IsBool()) Need(i, "true/false");
    return m_values[i].AsBool();
}
template <class V>
typename std::decay_t<decltype(std::declval<V>().AsArray())> BasicArgs<V>::List(std::size_t i) const {
    if (i >= m_values.size() || !m_values[i].AsArray()) Need(i, "a list");
    return m_values[i].AsArray();
}
template <class V>
typename std::decay_t<decltype(std::declval<V>().AsMap())> BasicArgs<V>::Map(std::size_t i) const {
    if (i >= m_values.size() || !m_values[i].AsMap()) Need(i, "a map");
    return m_values[i].AsMap();
}
template <class V> V BasicArgs<V>::Call(const V& fn, std::vector<V> args) const {
    try {
        return m_rt.CallValue(fn, args);
    } catch (ScriptError& e) {
        if (e.line == 0) e.line = m_rt.Ln(m_line);
        throw;
    }
}

} // namespace okayscript
