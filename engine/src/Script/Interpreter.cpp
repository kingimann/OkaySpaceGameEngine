// Runs a checked program. The checker has already resolved every name and type,
// so this walks the tree doing exactly what each node says; the only failures
// left are things that depend on the running game (a destroyed object, dividing
// by zero, a loop that never ends), and those are reported with their line.
#include "ScriptInternal.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <cmath>
#include <cstdio>

namespace okay::script {

std::string ValueToString(const Value& v) {
    struct V {
        std::string operator()(std::monostate) const { return "null"; }
        std::string operator()(bool b) const { return b ? "True" : "False"; }
        std::string operator()(std::int32_t i) const { return std::to_string(i); }
        std::string operator()(float f) const {
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
    };
    return std::visit(V{}, v);
}

namespace {

enum class Flow { Normal, Break, Continue, Return };

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
        : prog_(p), cls_(*p.Component()), fields_(fields), ctx_(ctx), inst_(inst), api_(EngineApi::Get()) {}

    Value CallMethod(int index, std::vector<Value>& args) {
        const MethodDecl& m = cls_.methods[(std::size_t)index];
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
    const ClassDecl& cls_;
    std::vector<Value>& fields_;
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
                const ArrayRef arr = std::get<ArrayRef>(Receiver(*s.expr));   // holds the array while looping
                const LocalDecl& d = s.locals[0];
                for (std::size_t i = 0; i < arr->items.size(); ++i) {
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

    // An array element as a place to read or write: the array and index are
    // worked out once, however the place is then used.
    struct Slot { ArrayRef arr; std::size_t i = 0; };
    Slot IndexSlot(const Expr& e) {
        Value a = Receiver(*e.object);
        const std::int32_t i = AsI(Eval(*e.lhs));
        ArrayRef arr = std::get<ArrayRef>(a);
        if (i < 0 || (std::size_t)i >= arr->items.size())
            throw RuntimeError("Index " + std::to_string(i) + " is outside the array (length " + std::to_string(arr->items.size()) + ")", e.loc);
        return {std::move(arr), (std::size_t)i};
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

    Value Receiver(const Expr& obj) {
        Value v = Eval(obj);
        if (std::holds_alternative<std::monostate>(v)) throw RuntimeError("'" + Describe(obj) + "' is null here", obj.loc);
        return v;
    }
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
            case EK::This: return Value{};
            case EK::Name: case EK::Member: return Load(e);
            case EK::Call: return Call(e);
            case EK::New: {
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
                if (e.lhs->kind == EK::Index) {
                    Slot sl = IndexSlot(*e.lhs);
                    Value cur = sl.arr->items[sl.i];
                    Value next = IncDecValue(cur, e.op == T::PlusPlus);
                    sl.arr->items[sl.i] = next;
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
                        return Value{(std::int32_t)std::trunc(*f)};
                    }
                    return a;
                }
                return Value{AsF(a)};
            }
            case EK::Conditional: return AsB(Eval(*e.third)) ? Eval(*e.lhs) : Eval(*e.rhs);
            case EK::Index: { Slot sl = IndexSlot(e); return sl.arr->items[sl.i]; }
            case EK::NewArray: case EK::ArrayLit: return MakeArray(e);
            case EK::Interp: {
                std::string s;
                for (auto& a : e.args) s += ValueToString(Eval(*a));
                return Value{std::move(s)};
            }
            case EK::Format: {
                const Value v = Eval(*e.lhs);
                const auto* i = std::get_if<std::int32_t>(&v);
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
        if (e.kind == EK::Index) { Slot sl = IndexSlot(e); return sl.arr->items[sl.i]; }
        switch (e.bind) {
            case Bind::EnumValue: return Value{e.ival};
            case Bind::ArrayLength: {
                Value a = Receiver(*e.object);
                return Value{(std::int32_t)std::get<ArrayRef>(a)->items.size()};
            }
            case Bind::Local: return (*locals_)[(std::size_t)e.slot];
            case Bind::Field: return fields_[(std::size_t)e.slot];
            case Bind::StaticField: return inst_.Statics()[(std::size_t)e.slot];
            case Bind::SelfMember:
                if (e.slot == 2) return Value{ctx_.component ? ctx_.component->enabled : true};
                return SelfRef(e.slot);
            case Bind::VecField: {
                Vec3 v = AsV(Eval(*e.object));
                return Value{e.member == 0 ? v.x : e.member == 1 ? v.y : v.z};
            }
            case Bind::ApiProp: {
                Value self = Receiver(*e.object);
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
        if (e.kind == EK::Index) { Slot sl = IndexSlot(e); sl.arr->items[sl.i] = v; return; }
        switch (e.bind) {
            case Bind::Local: (*locals_)[(std::size_t)e.slot] = v; return;
            case Bind::Field: fields_[(std::size_t)e.slot] = v; return;
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
        if (e.lhs->kind == EK::Index) {   // a[i] = v, a[i] += v: the array and index first, once
            Slot sl = IndexSlot(*e.lhs);
            Value rhs = Eval(*e.rhs);
            Value v = e.op == T::Assign ? rhs : Arith(CompoundOp(e.op), sl.arr->items[sl.i], rhs, e.lhs->type, false, false, e.loc);
            sl.arr->items[sl.i] = v;
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
            case Bind::ApiStaticMethod: case Bind::ApiMethod: {
                const ApiType& t = api_.types[(std::size_t)e.slot];
                const ApiMethod& m = e.bind == Bind::ApiMethod ? t.methods[(std::size_t)e.member] : t.staticMethods[(std::size_t)e.member];
                Value self = e.bind == Bind::ApiMethod ? Receiver(*e.object) : Value{};
                std::vector<Value> args;
                args.reserve(e.args.size());
                for (auto& a : e.args) args.push_back(Eval(*a));
                ctx_.loc = e.loc;
                return m.fn(self, args, ctx_);
            }
            case Bind::GetComponent: {
                GameObject* g = nullptr;
                if (e.object) g = Resolve(AsRef(Receiver(*e.object)), e.object->loc);
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
            if (auto* x = std::get_if<float>(&v)) { fields_[i] = Value{(std::int32_t)std::trunc(*x)}; return true; }
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
