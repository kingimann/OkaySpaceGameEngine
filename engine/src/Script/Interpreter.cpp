// Runs a checked program. The checker has already resolved every name and type,
// so this walks the tree doing exactly what each node says; the only failures
// left are things that depend on the running game (a destroyed object, dividing
// by zero, a loop that never ends), and those are reported with their line.
#include "ScriptInternal.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace okay::script {

std::string ValueToString(const Value& v) {
    struct V {
        std::string operator()(std::monostate) const { return "null"; }
        std::string operator()(bool b) const { return b ? "True" : "False"; }
        std::string operator()(std::int32_t i) const { return std::to_string(i); }
        std::string operator()(float f) const {
            if (std::isnan(f)) return "NaN";   // the same on every platform, not printf's nan / -nan
            if (std::isinf(f)) return f > 0 ? "Infinity" : "-Infinity";
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%.7g", (double)f);
            return buf;
        }
        std::string operator()(const std::string& s) const { return s; }
        std::string operator()(const Vec3& p) const { return "(" + (*this)(p.x) + ", " + (*this)(p.y) + ", " + (*this)(p.z) + ")"; }
        std::string operator()(const ObjRef& r) const {
            if (r.go == 0) return "null";
            GameObject* g = GameObject::FromInstanceId(r.go);
            return g ? g->name : "(destroyed)";
        }
        std::string operator()(const ArrayRef& a) const { return a ? a->typeName : "null"; }
        std::string operator()(const ObjPtr& o) const { return o ? o->className : "null"; }
        std::string operator()(const DictRef& d) const { return d ? d->typeName : "null"; }
    };
    return std::visit(V{}, v);
}

namespace {
// Dropping the last reference to a long chain (a linked list of 100k nodes)
// would free it one nested destructor per link and overflow the stack, so the
// links are moved onto a list and freed one at a time instead.
bool HoldsRef(const Value& v) {
    return std::holds_alternative<ObjPtr>(v) || std::holds_alternative<ArrayRef>(v) || std::holds_alternative<DictRef>(v);
}
void Take(std::vector<Value>& from, std::vector<Value>& to) { for (Value& x : from) if (HoldsRef(x)) to.push_back(std::move(x)); }
void ReleaseChain(std::vector<Value> pending) {
    while (!pending.empty()) {
        Value v = std::move(pending.back());
        pending.pop_back();
        if (auto* o = std::get_if<ObjPtr>(&v); o && *o && o->use_count() == 1) Take((*o)->fields, pending);
        if (auto* a = std::get_if<ArrayRef>(&v); a && *a && a->use_count() == 1) Take((*a)->items, pending);
        if (auto* d = std::get_if<DictRef>(&v); d && *d && d->use_count() == 1)
            for (auto& kv : (*d)->entries) {
                if (HoldsRef(kv.first)) pending.push_back(std::move(kv.first));
                if (HoldsRef(kv.second)) pending.push_back(std::move(kv.second));
            }
    }   // each v is freed here with nothing left inside it
}
std::vector<Value> Refs(std::vector<Value>& values) { std::vector<Value> r; Take(values, r); return r; }
} // namespace

ScriptObj::~ScriptObj() { ReleaseChain(Refs(fields)); }
ArrayObj::~ArrayObj() { ReleaseChain(Refs(items)); }
DictObj::~DictObj() {
    index.clear();   // its keys are copies; the entries hold the values that matter
    std::vector<Value> r;
    for (auto& kv : entries) {
        if (HoldsRef(kv.first)) r.push_back(std::move(kv.first));
        if (HoldsRef(kv.second)) r.push_back(std::move(kv.second));
    }
    ReleaseChain(std::move(r));
}

bool KeyLess::operator()(const Value& a, const Value& b) const {
    if (a.index() != b.index()) return a.index() < b.index();
    if (auto* r = std::get_if<ObjRef>(&a)) { const ObjRef& s = std::get<ObjRef>(b); return r->go != s.go ? r->go < s.go : r->type < s.type; }
    if (auto* o = std::get_if<ObjPtr>(&a)) return std::less<ScriptObj*>()(o->get(), std::get<ObjPtr>(b).get());
    if (auto* l = std::get_if<ArrayRef>(&a)) return std::less<ArrayObj*>()(l->get(), std::get<ArrayRef>(b).get());
    if (auto* d = std::get_if<DictRef>(&a)) return std::less<DictObj*>()(d->get(), std::get<DictRef>(b).get());
    if (auto* s = std::get_if<std::string>(&a)) return *s < std::get<std::string>(b);
    if (auto* i = std::get_if<std::int32_t>(&a)) return *i < std::get<std::int32_t>(b);
    if (auto* f = std::get_if<float>(&a)) return *f < std::get<float>(b);
    if (auto* t = std::get_if<bool>(&a)) return !*t && std::get<bool>(b);
    return false;   // null
}

namespace {

enum class Flow { Normal, Break, Continue, Return };

// Thrown when a ?. finds null; the chain's NullCond node turns it into null.
struct NullSkip {};

float AsF(const Value& v) {
    if (auto* f = std::get_if<float>(&v)) return *f;
    if (auto* i = std::get_if<std::int32_t>(&v)) return (float)*i;
    return 0.0f;
}
std::int32_t AsI(const Value& v) { if (auto* i = std::get_if<std::int32_t>(&v)) return *i; return (std::int32_t)AsF(v); }
bool AsB(const Value& v) { if (auto* b = std::get_if<bool>(&v)) return *b; return false; }
Vec3 AsV(const Value& v) { if (auto* p = std::get_if<Vec3>(&v)) return *p; return Vec3::Zero; }
ObjRef AsRef(const Value& v) { if (auto* r = std::get_if<ObjRef>(&v)) return *r; return ObjRef{}; }

Value DefaultFor(const Type& t) {
    switch (t.kind) {
        case TK::Bool: return Value{false};
        case TK::Int: case TK::Enum: return Value{std::int32_t{0}};
        case TK::Float: return Value{0.0f};
        case TK::String: return Value{std::string()};
        case TK::Vector3: return Value{Vec3::Zero};
        default: return Value{};
    }
}

// The most items one array can hold, so a bad length is an error instead of
// running out of memory.
constexpr std::int64_t kMaxArrayLength = 1'000'000;

// Wrapping int arithmetic (like C#), done in unsigned to stay well-defined.
std::int32_t Wrap(std::int64_t v) { return (std::int32_t)(std::uint32_t)(std::uint64_t)v; }

class Exec {
public:
    Exec(const Program& p, std::vector<Value>& fields, CallContext& ctx, Instance& inst)
        : prog_(p), self_(&fields), cls_(p.component), ctx_(ctx), inst_(inst), api_(EngineApi::Get()) {}

    Value CallMethod(int index, std::vector<Value>& args) {
        const MethodDecl& m = prog_.classes[(std::size_t)cls_].methods[(std::size_t)index];
        if (++depth_ > 200) { --depth_; throw RuntimeError(m.name + "() calls itself too deeply (endless recursion?)", m.loc); }
        std::vector<Value> frame((std::size_t)std::max(m.localCount, (int)m.params.size()));
        for (std::size_t i = 0; i < m.params.size() && i < args.size(); ++i) frame[i] = args[i];
        std::vector<Value>* saved = locals_;
        locals_ = &frame;
        Value ret;
        try {
            Flow f = Run(*m.body);
            if (f == Flow::Return) ret = std::move(retVal_);
        } catch (...) { locals_ = saved; --depth_; throw; }
        locals_ = saved;
        --depth_;
        return ret;
    }

    Value EvalStandalone(const Expr& e) { return Eval(e); }

private:
    const Program& prog_;
    // The object the running code belongs to: the component's fields, or a plain
    // class object (`this`) while one of its methods runs.
    std::vector<Value>* self_;
    int cls_;
    ObjPtr this_;
    CallContext& ctx_;
    Instance& inst_;
    const EngineApi& api_;
    std::vector<Value>* locals_ = nullptr;
    Value retVal_;
    int depth_ = 0;

    void Step(SrcLoc loc) {
        if (++inst_.steps > inst_.stepLimit)
            throw RuntimeError("This ran too long and was stopped (an endless loop?)", loc);
    }

    Flow Run(const Stmt& s) {
        Step(s.loc);
        switch (s.kind) {
            case SK::Block:
                for (auto& c : s.body) { Flow f = Run(*c); if (f != Flow::Normal) return f; }
                return Flow::Normal;
            case SK::Expr: Eval(*s.expr); return Flow::Normal;
            case SK::Local:
                for (const LocalDecl& d : s.locals)
                    (*locals_)[(std::size_t)d.slot] = d.init ? Eval(*d.init) : DefaultFor(d.resolved);
                return Flow::Normal;
            case SK::If:
                if (AsB(Eval(*s.expr))) return Run(*s.then);
                return s.otherwise ? Run(*s.otherwise) : Flow::Normal;
            case SK::While:
                while (AsB(Eval(*s.expr))) {
                    Flow f = Run(*s.loopBody);
                    if (f == Flow::Break) break;
                    if (f == Flow::Return) return f;
                    Step(s.loc);
                }
                return Flow::Normal;
            case SK::DoWhile:
                do {
                    Flow f = Run(*s.loopBody);
                    if (f == Flow::Break) break;
                    if (f == Flow::Return) return f;
                    Step(s.loc);
                } while (AsB(Eval(*s.expr)));
                return Flow::Normal;
            case SK::For: {
                if (s.init) Run(*s.init);
                while (!s.expr || AsB(Eval(*s.expr))) {
                    Flow f = Run(*s.loopBody);
                    if (f == Flow::Break) break;
                    if (f == Flow::Return) return f;
                    for (auto& st : s.step) Eval(*st);
                    Step(s.loc);
                }
                return Flow::Normal;
            }
            case SK::Return:
                retVal_ = s.expr ? Eval(*s.expr) : Value{};
                return Flow::Return;
            case SK::Break: return Flow::Break;
            case SK::Continue: return Flow::Continue;
            case SK::Switch: {
                // The checker made sure no section runs into the next one.
                const Value v = Eval(*s.expr);
                const SwitchSection* hit = nullptr;
                const SwitchSection* def = nullptr;
                for (const SwitchSection& sec : s.sections) {
                    if (sec.isDefault) def = &sec;
                    for (const Value& c : sec.values) if (Equal(v, c)) { hit = &sec; break; }
                    if (hit) break;
                }
                if (!hit) hit = def;
                if (!hit) return Flow::Normal;
                for (auto& st : hit->body) {
                    Flow f = Run(*st);
                    if (f == Flow::Break) return Flow::Normal;
                    if (f != Flow::Normal) return f;
                }
                return Flow::Normal;
            }
            case SK::Foreach: {
                const Value c = Receiver(*s.expr);   // holds the collection while looping
                const LocalDecl& d = s.locals[0];
                if (auto* dict = std::get_if<DictRef>(&c)) return ForeachDict(s, **dict);
                const ArrayRef& arr = std::get<ArrayRef>(c);
                const std::uint32_t version = arr->version;
                for (std::size_t i = 0; i < arr->items.size(); ++i) {
                    if (arr->version != version) throw RuntimeError("The " + arr->typeName + " was changed during foreach", s.loc);
                    Value item = arr->items[i];
                    if (d.promote) if (auto* n = std::get_if<std::int32_t>(&item)) item = Value{(float)*n};
                    (*locals_)[(std::size_t)d.slot] = std::move(item);
                    Flow f = Run(*s.loopBody);
                    if (f == Flow::Break) break;
                    if (f == Flow::Return) return f;
                    Step(s.loc);
                }
                return Flow::Normal;
            }
        }
        return Flow::Normal;
    }

    // foreach over a dictionary: each entry as a KeyValuePair (key, value).
    Flow ForeachDict(const Stmt& s, DictObj& dict) {
        const LocalDecl& d = s.locals[0];
        const std::string pairName = prog_.NameOf(d.resolved);
        const std::uint32_t version = dict.version;
        for (std::size_t i = 0; i < dict.entries.size(); ++i) {
            if (dict.version != version) throw RuntimeError("The " + dict.typeName + " was changed during foreach", s.loc);
            auto kv = std::make_shared<ArrayObj>();
            kv->typeName = pairName;
            kv->items = {dict.entries[i].first, dict.entries[i].second};
            (*locals_)[(std::size_t)d.slot] = Value{std::move(kv)};
            Flow f = Run(*s.loopBody);
            if (f == Flow::Break) break;
            if (f == Flow::Return) return f;
            Step(s.loc);
        }
        return Flow::Normal;
    }

    // How a dictionary key reads in a message.
    static std::string KeyText(const Value& k) {
        if (auto* str = std::get_if<std::string>(&k)) return "\"" + *str + "\"";
        return ValueToString(k);
    }
    // The value stored under `key`, or a runtime error naming the key.
    static Value* DictFind(DictObj& d, const Value& key) {
        auto it = d.index.find(key);
        return it == d.index.end() ? nullptr : &d.entries[it->second].second;
    }
    static void DictSet(DictObj& d, const Value& key, Value v) {
        if (Value* slot = DictFind(d, key)) { *slot = std::move(v); return; }
        d.index[key] = d.entries.size();
        d.entries.emplace_back(key, std::move(v));
        ++d.version;
    }
    static bool DictErase(DictObj& d, const Value& key) {
        auto it = d.index.find(key);
        if (it == d.index.end()) return false;
        const std::size_t at = it->second;
        d.index.erase(it);
        d.entries.erase(d.entries.begin() + (std::ptrdiff_t)at);
        for (auto& ix : d.index) if (ix.second > at) --ix.second;
        ++d.version;
        return true;
    }
    static RuntimeError OutsideList(std::int64_t i, const ArrayObj& a, bool isArray, SrcLoc loc) {
        return RuntimeError("Index " + std::to_string(i) + " is outside the " + (isArray ? std::string("array") : a.typeName) +
                            " (length " + std::to_string(a.items.size()) + ")", loc);
    }

    // An array element or an object's field as a place to read or write: the
    // array and index (or the object) are worked out once, however the place is
    // then used. `keep` holds what `v` points into.
    struct Place { std::shared_ptr<void> keep; Value* v = nullptr; };
    static bool IsPlace(const Expr& e) { return e.kind == EK::Index || e.bind == Bind::ObjField; }
    Place PlaceOf(const Expr& e) {
        if (e.kind == EK::Index) {
            Value a = Receiver(*e.object);
            if (auto* dict = std::get_if<DictRef>(&a)) {   // d[key] must already be there to read it
                const Value key = Eval(*e.lhs);
                Value* v = DictFind(**dict, key);
                if (!v) throw RuntimeError("The key " + KeyText(key) + " isn't in this " + (*dict)->typeName, e.loc);
                return {std::move(*dict), v};
            }
            const std::int32_t i = AsI(Eval(*e.lhs));
            ArrayRef arr = std::get<ArrayRef>(a);
            if (i < 0 || (std::size_t)i >= arr->items.size()) throw OutsideList(i, *arr, e.object->type.kind == TK::Array, e.loc);
            Value* v = &arr->items[(std::size_t)i];
            return {std::move(arr), v};
        }
        ObjPtr o = std::get<ObjPtr>(Receiver(*e.object, e.nullCond));
        Value* v = &o->fields[(std::size_t)e.slot];
        return {std::move(o), v};
    }

    Value CollCall(const Expr& e) {
        Value c = Receiver(*e.object, e.nullCond);
        std::vector<Value> a;
        for (auto& x : e.args) a.push_back(Eval(*x));
        const CollOp op = (CollOp)e.member;
        if (auto* dp = std::get_if<DictRef>(&c)) {
            DictObj& d = **dp;
            switch (op) {
                case CollOp::DictAdd:
                    if (DictFind(d, a[0])) throw RuntimeError("The key " + KeyText(a[0]) + " is already in this " + d.typeName, e.loc);
                    DictSet(d, a[0], a[1]);
                    return Value{};
                case CollOp::DictTryAdd:
                    if (DictFind(d, a[0])) return Value{false};
                    DictSet(d, a[0], a[1]);
                    return Value{true};
                case CollOp::DictRemove: return Value{DictErase(d, a[0])};
                case CollOp::DictContainsKey: return Value{DictFind(d, a[0]) != nullptr};
                case CollOp::DictContainsValue:
                    for (auto& kv : d.entries) if (Equal(kv.second, a[0])) return Value{true};
                    return Value{false};
                case CollOp::DictClear:
                    if (!d.entries.empty()) ++d.version;
                    d.entries.clear(); d.index.clear();
                    return Value{};
                case CollOp::DictGetValueOrDefault: {
                    if (Value* v = DictFind(d, a[0])) return *v;
                    return a.size() > 1 ? a[1] : DefaultFor(prog_.generics[(std::size_t)e.object->type.id].b);
                }
                default: return Value{};
            }
        }
        ArrayObj& l = *std::get<ArrayRef>(c);
        auto find = [&](const Value& v) -> std::int64_t {
            for (std::size_t i = 0; i < l.items.size(); ++i) if (Equal(l.items[i], v)) return (std::int64_t)i;
            return -1;
        };
        switch (op) {
            case CollOp::ListAdd: l.items.push_back(a[0]); ++l.version; return Value{};
            case CollOp::ListAddRange: {
                const std::vector<Value> more = std::get<ArrayRef>(a[0])->items;   // a copy: xs.AddRange(xs) doubles it
                l.items.insert(l.items.end(), more.begin(), more.end());
                ++l.version;
                return Value{};
            }
            case CollOp::ListInsert: {
                const std::int32_t i = AsI(a[0]);
                if (i < 0 || (std::size_t)i > l.items.size()) throw OutsideList(i, l, false, e.loc);
                l.items.insert(l.items.begin() + i, a[1]);
                ++l.version;
                return Value{};
            }
            case CollOp::ListRemove: {
                const std::int64_t i = find(a[0]);
                if (i < 0) return Value{false};
                l.items.erase(l.items.begin() + (std::ptrdiff_t)i);
                ++l.version;
                return Value{true};
            }
            case CollOp::ListRemoveAt: {
                const std::int32_t i = AsI(a[0]);
                if (i < 0 || (std::size_t)i >= l.items.size()) throw OutsideList(i, l, false, e.loc);
                l.items.erase(l.items.begin() + i);
                ++l.version;
                return Value{};
            }
            case CollOp::ListClear:
                if (!l.items.empty()) ++l.version;
                l.items.clear();
                return Value{};
            case CollOp::ListContains: return Value{find(a[0]) >= 0};
            case CollOp::ListIndexOf: return Value{(std::int32_t)find(a[0])};
            case CollOp::ListSort:   // numbers, text or enums (the checker allows nothing else)
                std::stable_sort(l.items.begin(), l.items.end(), [](const Value& x, const Value& y) {
                    if (auto* s = std::get_if<std::string>(&x)) return *s < std::get<std::string>(y);
                    if (std::holds_alternative<std::int32_t>(x) && std::holds_alternative<std::int32_t>(y))
                        return std::get<std::int32_t>(x) < std::get<std::int32_t>(y);
                    return AsF(x) < AsF(y);
                });
                ++l.version;
                return Value{};
            case CollOp::ListReverse: std::reverse(l.items.begin(), l.items.end()); ++l.version; return Value{};
            case CollOp::ListToArray: {
                auto out = std::make_shared<ArrayObj>();
                out->typeName = prog_.NameOf(e.type);
                out->items = l.items;
                return Value{std::move(out)};
            }
            default: return Value{};
        }
    }

    // Run `f` as code of object `o` (its fields, its class, `this`).
    template <typename F>
    Value As(const ObjPtr& o, F&& f) {
        std::vector<Value>* self = self_;
        const int cls = cls_;
        ObjPtr saved = std::move(this_);
        self_ = &o->fields; cls_ = o->cls; this_ = o;
        auto restore = [&] { self_ = self; cls_ = cls; this_ = std::move(saved); };
        Value v;
        try { v = f(); } catch (...) { restore(); throw; }
        restore();
        return v;
    }

    // new Node(...): fields at their defaults, then their starting values, then the constructor.
    Value NewObject(const Expr& e) {
        std::vector<Value> args;
        args.reserve(e.args.size());
        for (auto& a : e.args) args.push_back(Eval(*a));
        const ClassDecl& k = prog_.classes[(std::size_t)e.slot];
        // A field that starts as `new` of its own class would recurse in here forever.
        if (depth_ >= 200) throw RuntimeError("Making a " + k.name + " makes more objects without end (a field that starts as new " + k.name + "?)", e.loc);
        ++depth_;
        struct Leave { int& d; ~Leave() { --d; } } leave{depth_};
        auto o = std::make_shared<ScriptObj>();
        o->cls = e.slot;
        o->className = k.name;
        o->fields.reserve(k.fields.size());
        for (const FieldDecl& f : k.fields) o->fields.push_back(DefaultFor(f.resolved));
        As(o, [&] {
            for (std::size_t i = 0; i < k.fields.size(); ++i)
                if (k.fields[i].init) o->fields[i] = Eval(*k.fields[i].init);
            if (e.member >= 0) CallMethod(e.member, args);
            return Value{};
        });
        return Value{std::move(o)};
    }

    Value MakeArray(const Expr& e) {
        auto arr = std::make_shared<ArrayObj>();
        arr->typeName = prog_.NameOf(e.type);
        const Type& elem = prog_.arrays[(std::size_t)e.type.id];
        std::int64_t n = (std::int64_t)e.args.size();
        if (e.kind == EK::NewArray && e.lhs) {
            n = AsI(Eval(*e.lhs));
            if (n < 0) throw RuntimeError("An array can't have a negative length (" + std::to_string(n) + ")", e.loc);
            if (n > kMaxArrayLength)
                throw RuntimeError("An array of " + std::to_string(n) + " items is too big (the most is " + std::to_string(kMaxArrayLength) + ")", e.loc);
        }
        arr->items.reserve((std::size_t)n);
        for (auto& a : e.args) arr->items.push_back(Eval(*a));
        while ((std::int64_t)arr->items.size() < n) arr->items.push_back(DefaultFor(elem));
        return Value{std::move(arr)};
    }

    Value Eval(const Expr& e) {
        Value v = EvalRaw(e);
        if (e.toFloat) if (auto* i = std::get_if<std::int32_t>(&v)) v = Value{(float)*i};
        if (e.enumStr >= 0)
            if (auto* i = std::get_if<std::int32_t>(&v)) {
                const std::string* n = prog_.enums[(std::size_t)e.enumStr].NameOf(*i);
                v = Value{n ? *n : std::to_string(*i)};
            }
        return v;
    }

    Value Receiver(const Expr& obj, bool nullCond = false) {
        Value v = Eval(obj);
        if (nullCond && IsNull(v)) throw NullSkip{};
        if (std::holds_alternative<std::monostate>(v)) throw RuntimeError("'" + Describe(obj) + "' is null here", obj.loc);
        return v;
    }
    /// null, or a reference to a destroyed object.
    static bool IsNull(const Value& v) { return Equal(v, Value{}); }
    static std::string Describe(const Expr& e) {
        if (e.kind == EK::Name || e.kind == EK::Member || e.kind == EK::Call) return e.name;
        return "this value";
    }

    Value SelfRef(int which) {
        GameObject* g = ctx_.self;
        if (!g) throw RuntimeError("This script isn't attached to an object", ctx_.loc);
        return MakeRef(g, which == 0 ? api_.gameObject : api_.transform);
    }

    Value EvalRaw(const Expr& e) {
        ctx_.loc = e.loc;
        switch (e.kind) {
            case EK::IntLit: return Value{e.ival};
            case EK::FloatLit: return Value{e.fval};
            case EK::StringLit: return Value{e.sval};
            case EK::BoolLit: return Value{e.bval};
            case EK::NullLit: return Value{};
            case EK::This: return Value{this_};
            case EK::Name: case EK::Member: return Load(e);
            case EK::Call: return Call(e);
            case EK::New: {
                if (e.bind == Bind::NewObject) return NewObject(e);
                if (e.bind == Bind::NewCollection) {
                    if (e.type.kind == TK::Dict) {
                        auto d = std::make_shared<DictObj>();
                        d->typeName = prog_.NameOf(e.type);
                        return Value{std::move(d)};
                    }
                    auto l = std::make_shared<ArrayObj>();
                    l->typeName = prog_.NameOf(e.type);
                    for (auto& a : e.args) l->items.push_back(Eval(*a));
                    return Value{std::move(l)};
                }
                float c[3] = {0, 0, 0};
                for (std::size_t i = 0; i < e.args.size(); ++i) c[i] = AsF(Eval(*e.args[i]));
                return Value{Vec3{c[0], c[1], c[2]}};
            }
            case EK::Unary: {
                Value a = Eval(*e.lhs);
                if (e.op == T::Not) return Value{!AsB(a)};
                if (auto* i = std::get_if<std::int32_t>(&a)) return Value{Wrap(-(std::int64_t)*i)};
                if (auto* f = std::get_if<float>(&a)) return Value{-*f};
                return Value{AsV(a) * -1.0f};
            }
            case EK::Binary: return Binary(e);
            case EK::Assign: return Assign(e);
            case EK::IncDec: {
                if (IsPlace(*e.lhs)) {
                    Place pl = PlaceOf(*e.lhs);
                    Value cur = *pl.v;
                    Value next = IncDecValue(cur, e.op == T::PlusPlus);
                    *pl.v = next;
                    return e.prefix ? next : cur;
                }
                Value cur = Load(*e.lhs);
                Value next = IncDecValue(cur, e.op == T::PlusPlus);
                Store(*e.lhs, next);
                return e.prefix ? next : cur;
            }
            case EK::Cast: {
                Value a = Eval(*e.lhs);
                if (e.type.kind == TK::Int || e.type.kind == TK::Enum) {
                    if (auto* f = std::get_if<float>(&a)) {
                        if (!std::isfinite(*f)) throw RuntimeError("Can't turn this number into an int (it isn't finite)", e.loc);
                        const double t = std::trunc((double)*f);
                        if (t < -2147483648.0 || t > 2147483647.0) throw RuntimeError(ValueToString(a) + " doesn't fit in an int", e.loc);
                        return Value{(std::int32_t)t};
                    }
                    return a;
                }
                return Value{AsF(a)};
            }
            case EK::Conditional: return AsB(Eval(*e.third)) ? Eval(*e.lhs) : Eval(*e.rhs);
            case EK::Index: return *PlaceOf(e).v;
            case EK::NullCond:
                try { return Eval(*e.lhs); } catch (const NullSkip&) { return Value{}; }
            case EK::NewArray: case EK::ArrayLit: return MakeArray(e);
            case EK::Interp: {
                std::string s;
                for (auto& a : e.args) s += ValueToString(Eval(*a));
                return Value{std::move(s)};
            }
            case EK::Format: {
                const Value v = Eval(*e.lhs);
                const auto* i = std::get_if<std::int32_t>(&v);
                if (!i && !std::isfinite(AsF(v))) return Value{ValueToString(v)};
                char buf[96];
                std::snprintf(buf, sizeof(buf), "%.*f", (int)e.ival, i ? (double)*i : (double)AsF(v));
                return Value{std::string(buf)};
            }
        }
        return Value{};
    }

    static Value IncDecValue(const Value& cur, bool inc) {
        if (auto* i = std::get_if<std::int32_t>(&cur)) return Value{Wrap((std::int64_t)*i + (inc ? 1 : -1))};
        return Value{AsF(cur) + (inc ? 1.0f : -1.0f)};
    }

    Value Load(const Expr& e) {
        if (IsPlace(e)) return *PlaceOf(e).v;
        switch (e.bind) {
            case Bind::EnumValue: return Value{e.ival};
            case Bind::ArrayLength: {
                Value a = Receiver(*e.object, e.nullCond);
                return Value{(std::int32_t)std::get<ArrayRef>(a)->items.size()};
            }
            case Bind::CollCount: {
                Value c = Receiver(*e.object, e.nullCond);
                if (auto* d = std::get_if<DictRef>(&c)) return Value{(std::int32_t)(*d)->entries.size()};
                return Value{(std::int32_t)std::get<ArrayRef>(c)->items.size()};
            }
            case Bind::DictKeys: case Bind::DictValues: {   // a new List of the keys / values, in order
                DictRef d = std::get<DictRef>(Receiver(*e.object, e.nullCond));
                auto out = std::make_shared<ArrayObj>();
                out->typeName = prog_.NameOf(e.type);
                for (auto& kv : d->entries) out->items.push_back(e.bind == Bind::DictKeys ? kv.first : kv.second);
                return Value{std::move(out)};
            }
            case Bind::PairKey: case Bind::PairValue:
                return std::get<ArrayRef>(Receiver(*e.object, e.nullCond))->items[e.bind == Bind::PairKey ? 0 : 1];
            case Bind::Local: return (*locals_)[(std::size_t)e.slot];
            case Bind::Field: return (*self_)[(std::size_t)e.slot];
            case Bind::StaticField: return inst_.Statics()[(std::size_t)e.slot];
            case Bind::SelfMember:
                if (e.slot == 2) return Value{ctx_.component ? ctx_.component->enabled : true};
                return SelfRef(e.slot);
            case Bind::VecField: {
                Vec3 v = AsV(Eval(*e.object));
                return Value{e.member == 0 ? v.x : e.member == 1 ? v.y : v.z};
            }
            case Bind::ApiProp: {
                Value self = Receiver(*e.object, e.nullCond);
                ctx_.loc = e.loc;
                return api_.types[(std::size_t)e.slot].props[(std::size_t)e.member].get(self, ctx_);
            }
            case Bind::ApiStaticProp:
                ctx_.loc = e.loc;
                return api_.types[(std::size_t)e.slot].staticProps[(std::size_t)e.member].get(Value{}, ctx_);
            default:
                throw RuntimeError("Internal error: unresolved name '" + e.name + "'", e.loc);
        }
    }

    void Store(const Expr& e, const Value& v) {
        if (IsPlace(e)) { *PlaceOf(e).v = v; return; }
        switch (e.bind) {
            case Bind::Local: (*locals_)[(std::size_t)e.slot] = v; return;
            case Bind::Field: (*self_)[(std::size_t)e.slot] = v; return;
            case Bind::StaticField: inst_.Statics()[(std::size_t)e.slot] = v; return;
            case Bind::SelfMember:
                if (e.slot == 2 && ctx_.component) ctx_.component->enabled = AsB(v);
                return;
            case Bind::VecField: {
                Vec3 p = AsV(Load(*e.object));
                float f = AsF(v);
                (e.member == 0 ? p.x : e.member == 1 ? p.y : p.z) = f;
                Store(*e.object, Value{p});
                return;
            }
            case Bind::ApiProp: {
                Value self = Receiver(*e.object);
                ctx_.loc = e.loc;
                api_.types[(std::size_t)e.slot].props[(std::size_t)e.member].set(self, v, ctx_);
                return;
            }
            case Bind::ApiStaticProp:
                ctx_.loc = e.loc;
                api_.types[(std::size_t)e.slot].staticProps[(std::size_t)e.member].set(Value{}, v, ctx_);
                return;
            default:
                throw RuntimeError("Internal error: can't assign here", e.loc);
        }
    }

    Value Arith(T op, const Value& a, const Value& b, const Type& type, bool pl, bool pr, SrcLoc loc) {
        if (type.kind == TK::String) return Value{ValueToString(a) + ValueToString(b)};
        if (type.kind == TK::Vector3) {
            const bool av = std::holds_alternative<Vec3>(a), bv = std::holds_alternative<Vec3>(b);
            switch (op) {
                case T::Plus: return Value{AsV(a) + AsV(b)};
                case T::Minus: return Value{AsV(a) - AsV(b)};
                case T::Star: return Value{av ? AsV(a) * AsF(b) : AsV(b) * AsF(a)};
                case T::Slash: {
                    float d = AsF(b);
                    if (d == 0.0f) throw RuntimeError("Division by zero", loc);
                    return Value{AsV(a) * (1.0f / d)};
                }
                default: break;
            }
            (void)bv;
        }
        if (type.kind == TK::Int) {
            std::int64_t x = AsI(a), y = AsI(b);
            switch (op) {
                case T::Plus: return Value{Wrap(x + y)};
                case T::Minus: return Value{Wrap(x - y)};
                case T::Star: return Value{Wrap(x * y)};
                case T::Slash:
                    if (y == 0) throw RuntimeError("Division by zero", loc);
                    return Value{Wrap(x / y)};
                case T::Percent:
                    if (y == 0) throw RuntimeError("Division by zero", loc);
                    return Value{Wrap(x % y)};
                default: break;
            }
        }
        float x = pl ? (float)AsI(a) : AsF(a), y = pr ? (float)AsI(b) : AsF(b);
        switch (op) {
            case T::Plus: return Value{x + y};
            case T::Minus: return Value{x - y};
            case T::Star: return Value{x * y};
            case T::Slash: return Value{x / y};     // float division by zero gives infinity, like C#
            case T::Percent: return Value{std::fmod(x, y)};
            default: return Value{};
        }
    }

    static bool Equal(const Value& a, const Value& b) {
        if (std::holds_alternative<std::monostate>(a) || std::holds_alternative<std::monostate>(b)) {
            // null == ref: a reference to a destroyed object also counts as null.
            const Value& other = std::holds_alternative<std::monostate>(a) ? b : a;
            if (std::holds_alternative<std::monostate>(other)) return true;
            if (auto* r = std::get_if<ObjRef>(&other)) return r->go == 0 || !GameObject::FromInstanceId(r->go);
            return false;
        }
        if (a.index() != b.index()) return AsF(a) == AsF(b);
        if (auto* r = std::get_if<ObjRef>(&a)) return *r == std::get<ObjRef>(b);
        if (auto* v = std::get_if<Vec3>(&a)) { Vec3 w = std::get<Vec3>(b); return v->x == w.x && v->y == w.y && v->z == w.z; }
        return a == b;
    }

    Value Binary(const Expr& e) {
        if (e.op == T::QuestionQuestion) {
            Value a = Eval(*e.lhs);
            return IsNull(a) ? Eval(*e.rhs) : a;
        }
        if (e.op == T::AndAnd) return Value{AsB(Eval(*e.lhs)) && AsB(Eval(*e.rhs))};
        if (e.op == T::OrOr) return Value{AsB(Eval(*e.lhs)) || AsB(Eval(*e.rhs))};
        Value a = Eval(*e.lhs), b = Eval(*e.rhs);
        switch (e.op) {
            case T::EqEq: return Value{Equal(a, b)};
            case T::NotEq: return Value{!Equal(a, b)};
            case T::Lt: case T::Gt: case T::Le: case T::Ge: {
                const bool ints = std::holds_alternative<std::int32_t>(a) && std::holds_alternative<std::int32_t>(b);
                if (ints) {
                    std::int32_t x = AsI(a), y = AsI(b);
                    return Value{e.op == T::Lt ? x < y : e.op == T::Gt ? x > y : e.op == T::Le ? x <= y : x >= y};
                }
                float x = AsF(a), y = AsF(b);
                return Value{e.op == T::Lt ? x < y : e.op == T::Gt ? x > y : e.op == T::Le ? x <= y : x >= y};
            }
            default: return Arith(e.op, a, b, e.type, e.promoteL, e.promoteR, e.loc);
        }
    }

    Value Assign(const Expr& e) {
        if (e.op == T::Assign && e.lhs->kind == EK::Index && e.lhs->object->type.kind == TK::Dict) {   // d[key] = v adds or replaces
            DictRef d = std::get<DictRef>(Receiver(*e.lhs->object));
            const Value key = Eval(*e.lhs->lhs);
            Value v = Eval(*e.rhs);
            DictSet(*d, key, v);
            return v;
        }
        if (e.op == T::QuestionQuestionEq) {   // the value is only worked out when the target is null
            if (IsPlace(*e.lhs)) {
                Place pl = PlaceOf(*e.lhs);
                if (!IsNull(*pl.v)) return *pl.v;
                Value v = Eval(*e.rhs);
                return *pl.v = std::move(v);
            }
            Value cur = Load(*e.lhs);
            if (!IsNull(cur)) return cur;
            Value v = Eval(*e.rhs);
            Store(*e.lhs, v);
            return v;
        }
        if (IsPlace(*e.lhs)) {   // a[i] = v, o.f += v: the array and index (or object) first, once
            Place pl = PlaceOf(*e.lhs);
            Value rhs = Eval(*e.rhs);
            Value v = e.op == T::Assign ? rhs : Arith(CompoundOp(e.op), *pl.v, rhs, e.lhs->type, false, false, e.loc);
            *pl.v = v;
            return v;
        }
        if (e.op == T::Assign) {
            Value v = Eval(*e.rhs);
            Store(*e.lhs, v);
            return v;
        }
        Value cur = Load(*e.lhs);
        Value rhs = Eval(*e.rhs);
        T op = CompoundOp(e.op);
        const Type& lt = e.lhs->type;
        Type resultType = lt;
        Value v = Arith(op, cur, rhs, resultType, false, false, e.loc);
        Store(*e.lhs, v);
        return v;
    }

    static T CompoundOp(T op) {
        return op == T::PlusEq ? T::Plus : op == T::MinusEq ? T::Minus : op == T::StarEq ? T::Star : op == T::SlashEq ? T::Slash : T::Percent;
    }

    Value Call(const Expr& e) {
        switch (e.bind) {
            case Bind::Method: {
                std::vector<Value> args;
                args.reserve(e.args.size());
                for (auto& a : e.args) args.push_back(Eval(*a));
                return CallMethod(e.slot, args);
            }
            case Bind::CollMethod: return CollCall(e);
            case Bind::ObjMethod: {
                ObjPtr o = std::get<ObjPtr>(Receiver(*e.object, e.nullCond));
                std::vector<Value> args;
                args.reserve(e.args.size());
                for (auto& a : e.args) args.push_back(Eval(*a));
                return As(o, [&] { return CallMethod(e.slot, args); });
            }
            case Bind::ApiStaticMethod: case Bind::ApiMethod: {
                const ApiType& t = api_.types[(std::size_t)e.slot];
                const ApiMethod& m = e.bind == Bind::ApiMethod ? t.methods[(std::size_t)e.member] : t.staticMethods[(std::size_t)e.member];
                Value self = e.bind == Bind::ApiMethod ? Receiver(*e.object, e.nullCond) : Value{};
                std::vector<Value> args;
                args.reserve(e.args.size());
                for (auto& a : e.args) args.push_back(Eval(*a));
                ctx_.loc = e.loc;
                return m.fn(self, args, ctx_);
            }
            case Bind::GetComponent: {
                GameObject* g = nullptr;
                if (e.object) g = Resolve(AsRef(Receiver(*e.object, e.nullCond)), e.object->loc);
                else g = ctx_.self;
                if (!g) throw RuntimeError("This script isn't attached to an object", e.loc);
                const ApiType& t = api_.types[(std::size_t)e.slot];
                return t.find && t.find(g) ? MakeRef(g, e.slot) : Value{};
            }
            case Bind::Destroy: {
                GameObject* g = Resolve(AsRef(Eval(*e.args[0])), e.args[0]->loc);
                if (Scene* sc = g->scene()) sc->Destroy(g);
                return Value{};
            }
            default:
                throw RuntimeError("Internal error: unresolved call '" + e.name + "'", e.loc);
        }
    }
};

} // namespace

namespace {
std::unordered_map<const Scene*, std::unordered_map<std::string, std::shared_ptr<StaticStore>>>& AllStatics() {
    static std::unordered_map<const Scene*, std::unordered_map<std::string, std::shared_ptr<StaticStore>>> all;
    return all;
}
} // namespace

std::shared_ptr<StaticStore> SceneStatics(const Scene* scene, const std::string& source) {
    auto& s = AllStatics()[scene][source];
    if (!s) s = std::make_shared<StaticStore>();
    return s;
}

Instance::Instance(std::shared_ptr<Program> prog, GameObject* self)
    : prog_(std::move(prog)), statics_(std::make_shared<StaticStore>()) {
    ctx_.self = self;
    const ClassDecl* c = prog_->Component();
    if (c) {
        fields_.resize(c->fields.size());
        for (std::size_t i = 0; i < c->fields.size(); ++i) fields_[i] = DefaultFor(c->fields[i].resolved);
    }
}

void Instance::Init() {
    const ClassDecl* c = prog_->Component();
    if (!c) return;
    steps = 0;
    Exec ex(*prog_, fields_, ctx_, *this);
    for (std::size_t i = 0; i < c->fields.size(); ++i)
        if (c->fields[i].init && !c->fields[i].isStatic) fields_[i] = ex.EvalStandalone(*c->fields[i].init);
    InitStatics();
}

// Static starting values are set once per store, by the first object to use it.
void Instance::InitStatics() {
    if (statics_->ready) return;
    statics_->ready = true;
    const ClassDecl* c = prog_->Component();
    statics_->values.clear();
    for (const FieldDecl& f : c->fields) statics_->values.push_back(DefaultFor(f.resolved));
    Exec ex(*prog_, fields_, ctx_, *this);
    for (std::size_t i = 0; i < c->fields.size(); ++i)
        if (c->fields[i].init && c->fields[i].isStatic) statics_->values[i] = ex.EvalStandalone(*c->fields[i].init);
}

void Instance::AttachStatics(std::shared_ptr<StaticStore> store) {
    if (!prog_->Component()) return;
    statics_ = std::move(store);
    steps = 0;
    InitStatics();
}

void ForgetSceneStatics(const Scene* scene) { AllStatics().erase(scene); }

Value Instance::Call(int method, std::vector<Value> args) {
    steps = 0;
    Exec ex(*prog_, fields_, ctx_, *this);
    return ex.CallMethod(method, args);
}

Value Instance::GetField(const std::string& name) const {
    const ClassDecl* c = prog_->Component();
    if (!c) return Value{};
    for (std::size_t i = 0; i < c->fields.size(); ++i)
        if (c->fields[i].name == name) return c->fields[i].isStatic ? statics_->values[i] : fields_[i];
    return Value{};
}

bool Instance::SetField(const std::string& name, const Value& v) {
    const ClassDecl* c = prog_->Component();
    if (!c) return false;
    for (std::size_t i = 0; i < c->fields.size(); ++i) {
        const FieldDecl& f = c->fields[i];
        if (f.name != name || f.isConst || f.isStatic) continue;   // a static isn't any one object's setting
        // Convert to the field's type; values that don't fit are refused.
        const TK k = f.resolved.kind;
        if (k == TK::Int || k == TK::Enum) {
            if (auto* x = std::get_if<float>(&v)) {   // a value that doesn't fit an int is refused
                const double t = std::isfinite(*x) ? std::trunc((double)*x) : 1e300;
                if (t < -2147483648.0 || t > 2147483647.0) return false;
                fields_[i] = Value{(std::int32_t)t};
                return true;
            }
            if (std::holds_alternative<std::int32_t>(v)) { fields_[i] = v; return true; }
            return false;
        }
        if (k == TK::Float) {
            if (auto* x = std::get_if<std::int32_t>(&v)) { fields_[i] = Value{(float)*x}; return true; }
            if (std::holds_alternative<float>(v)) { fields_[i] = v; return true; }
            return false;
        }
        if ((k == TK::Bool && std::holds_alternative<bool>(v)) || (k == TK::String && std::holds_alternative<std::string>(v)) ||
            (k == TK::Vector3 && std::holds_alternative<Vec3>(v)) || (k == TK::Object && (std::holds_alternative<ObjRef>(v) || std::holds_alternative<std::monostate>(v)))) {
            fields_[i] = v;
            return true;
        }
        return false;
    }
    return false;
}

} // namespace okay::script
