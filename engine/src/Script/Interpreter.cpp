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
        std::string operator()(const FuncRef& f) const { return f ? f->typeName : "null"; }
        std::string operator()(const CellRef& c) const { return c ? std::visit(*this, c->v) : "null"; }
        std::string operator()(const CoRef& c) const { return c && c->method ? c->method->name + "()" : "null"; }
        std::string operator()(const WaitRef& w) const { return w ? WaitTypeName((int)w->kind) : "null"; }
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

// How deeply script calls are nested right now, across every script (a lambda can
// call into the script that made it), so endless recursion stops with an error.
thread_local int g_depth = 0;
constexpr int kMaxDepth = 200;
struct DepthGuard {
    DepthGuard(const std::string& what, SrcLoc loc) {
        if (g_depth >= kMaxDepth) throw RuntimeError(what + " calls itself too deeply (endless recursion?)", loc, true);
        ++g_depth;
    }
    ~DepthGuard() { --g_depth; }
};

// A stable merge sort of 0..n-1 by `less`. Unlike std::sort it stays in bounds
// whatever `less` answers (a script's comparison can be inconsistent).
template <typename Less>
std::vector<std::size_t> SafeOrder(std::size_t n, Less less) {
    std::vector<std::size_t> a(n), b(n);
    for (std::size_t i = 0; i < n; ++i) a[i] = i;
    for (std::size_t w = 1; w < n; w *= 2) {
        for (std::size_t lo = 0; lo < n; lo += 2 * w) {
            const std::size_t mid = std::min(lo + w, n), hi = std::min(lo + 2 * w, n);
            std::size_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi) b[k++] = less(a[j], a[i]) ? a[j++] : a[i++];
            while (i < mid) b[k++] = a[i++];
            while (j < hi) b[k++] = a[j++];
        }
        std::swap(a, b);
    }
    return a;
}
std::vector<Value> Reorder(const std::vector<Value>& items, const std::vector<std::size_t>& order) {
    std::vector<Value> out;
    out.reserve(order.size());
    for (std::size_t i : order) out.push_back(items[i]);
    return out;
}

class Exec {
public:
    Exec(const Program& p, std::vector<Value>& fields, CallContext& ctx, Instance& inst)
        : prog_(p), self_(&fields), cls_(p.component), ctx_(ctx), inst_(inst), api_(EngineApi::Get()) {}

    Value CallMethod(int index, std::vector<Value>& args) {
        const MethodDecl& m = prog_.classes[(std::size_t)cls_].methods[(std::size_t)index];
        if (m.isCoroutine) return MakeCoroutine(m, args);
        DepthGuard depth(m.name + "()", m.loc);
        std::vector<Value> frame((std::size_t)std::max(m.localCount, (int)m.params.size()));
        for (std::size_t i = 0; i < m.params.size() && i < args.size(); ++i) frame[i] = args[i];
        std::vector<Value>* saved = locals_;
        const FuncObj* savedFn = fn_;
        locals_ = &frame;
        fn_ = nullptr;
        Value ret;
        try {
            if (m.baseCall) Eval(*m.baseCall);   // the base class's constructor first
            Flow f = Run(*m.body);
            if (f == Flow::Return) ret = std::move(retVal_);
        } catch (...) { locals_ = saved; fn_ = savedFn; throw; }
        locals_ = saved;
        fn_ = savedFn;
        return ret;
    }

    // ---- Coroutines ----
    // Calling an IEnumerator method makes a coroutine; nothing runs until StartCoroutine.
    Value MakeCoroutine(const MethodDecl& m, std::vector<Value>& args) {
        auto co = std::make_shared<CoState>();
        co->method = &m;
        co->prog = inst_.programPtr();
        co->inst = &inst_;
        co->alive = inst_.AliveToken();
        co->self = this_;
        co->cls = cls_;
        co->locals.resize((std::size_t)std::max(m.localCount, (int)m.params.size()));
        for (std::size_t i = 0; i < m.params.size() && i < args.size(); ++i) co->locals[i] = args[i];
        co->stack.push_back(CoFrame{m.body.get(), &m.body->body});
        return Value{std::move(co)};
    }
    // StartCoroutine: on the object that made it, running now until its first yield.
    void StartCo(const CoRef& co, SrcLoc loc) {
        if (co->alive.expired()) throw RuntimeError("This coroutine belongs to a script that was destroyed", loc);
        if (co->started) throw RuntimeError(co->method->name + "() was already started: call it again for a new coroutine", loc);
        co->started = true;
        co->inst->coroutines.push_back(co);
        if (co->inst != &inst_ || co->prog.get() != &prog_) {
            Exec other(*co->prog, co->inst->Fields(), co->inst->Context(), *co->inst);
            other.ResumeCo(*co);
        } else {
            ResumeCo(*co);
        }
    }
    // Run `co` until its next yield (true) or its end (false).
    bool ResumeCo(CoState& co) {
        if (co.done) return false;
        DepthGuard depth(co.method->name + "()", co.method->loc);
        std::vector<Value>* self = self_;
        std::vector<Value>* locals = locals_;
        const FuncObj* fn = fn_;
        const int cls = cls_;
        ObjPtr savedThis = std::move(this_);
        self_ = co.self ? &co.self->fields : &inst_.Fields();
        cls_ = co.cls;
        this_ = co.self;
        locals_ = &co.locals;
        fn_ = nullptr;
        auto restore = [&] { self_ = self; locals_ = locals; fn_ = fn; cls_ = cls; this_ = std::move(savedThis); };
        bool yielded = false;
        try { yielded = CoRun(co); }
        catch (...) { co.done = true; co.stack.clear(); restore(); throw; }
        restore();
        if (!yielded) { co.done = true; co.stack.clear(); }
        return yielded;
    }

    // Call a lambda value. It runs as code of the object that made it, which may
    // be another script object running the same program.
    Value Invoke(const FuncRef& f, std::vector<Value>& args, SrcLoc loc) {
        if (!f->list.empty()) {   // a += b: each in turn; a Func gives the last one's value
            const std::vector<FuncRef> list = f->list;   // a handler may change the event while it runs
            Value ret;
            for (const FuncRef& g : list) { std::vector<Value> a = args; ret = Invoke(g, a, loc); }
            return ret;
        }
        if (f->alive.expired()) throw RuntimeError("This function belongs to a script that was destroyed", loc);
        if (f->inst != &inst_ || f->prog.get() != &prog_) {
            Exec other(*f->prog, f->inst->Fields(), f->inst->Context(), *f->inst);
            return other.RunFunc(*f, args, loc);
        }
        return RunFunc(*f, args, loc);
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
    const FuncObj* fn_ = nullptr;   ///< the lambda running now (its captures)
    Value retVal_;

    // The coroutine's statements, one at a time: the statements with a yield inside
    // keep where they are on co.stack; the rest run as usual.
    enum class CoFlow { Normal, Break, Continue, End, Yielded };
    static CoFlow FromFlow(Flow f) {
        return f == Flow::Break ? CoFlow::Break : f == Flow::Continue ? CoFlow::Continue : f == Flow::Return ? CoFlow::End : CoFlow::Normal;
    }
    static bool IsLoop(SK k) { return k == SK::While || k == SK::DoWhile || k == SK::For || k == SK::Foreach; }
    bool CoRun(CoState& co) {
        while (!co.stack.empty()) {
            CoFlow r = CoFlow::Normal;
            {
                CoFrame& f = co.stack.back();   // not used after CoStmt, which may push
                const Stmt& s = *f.s;
                switch (s.kind) {
                    case SK::Block: case SK::Switch:
                        if (f.pc >= f.list->size()) { co.stack.pop_back(); continue; }
                        r = CoStmt(co, *(*f.list)[f.pc++]);
                        break;
                    case SK::While:
                        if (f.phase == 1) Step(s.loc);
                        if (!AsB(Eval(*s.expr))) { co.stack.pop_back(); continue; }
                        f.phase = 1;
                        r = CoStmt(co, *s.loopBody);
                        break;
                    case SK::DoWhile:
                        if (f.phase == 1) { Step(s.loc); if (!AsB(Eval(*s.expr))) { co.stack.pop_back(); continue; } }
                        f.phase = 1;
                        r = CoStmt(co, *s.loopBody);
                        break;
                    case SK::For:
                        if (f.phase == 0 && s.init) Run(*s.init);
                        if (f.phase == 1) { for (auto& st : s.step) Eval(*st); Step(s.loc); }
                        if (s.expr && !AsB(Eval(*s.expr))) { co.stack.pop_back(); continue; }
                        f.phase = 1;
                        r = CoStmt(co, *s.loopBody);
                        break;
                    case SK::Foreach: {
                        if (f.phase == 0) {
                            f.coll = Receiver(*s.expr);
                            f.version = CollVersion(f.coll);
                            f.pc = 0;
                        } else {
                            ++f.pc;
                            Step(s.loc);
                        }
                        if (CollVersion(f.coll) != f.version) throw RuntimeError("The " + ValueToString(f.coll) + " was changed during foreach", s.loc);
                        Value item;
                        if (!ForeachItem(s, f.coll, f.pc, item)) { co.stack.pop_back(); continue; }
                        (*locals_)[(std::size_t)s.locals[0].slot] = std::move(item);
                        f.phase = 1;
                        r = CoStmt(co, *s.loopBody);
                        break;
                    }
                    default: co.stack.pop_back(); continue;
                }
            }
            if (r == CoFlow::Yielded) return true;
            if (r == CoFlow::End) { co.stack.clear(); return false; }
            if (r == CoFlow::Normal) continue;
            // break / continue: out to the nearest loop (or, for break, switch)
            while (!co.stack.empty()) {
                const SK k = co.stack.back().s->kind;
                if (IsLoop(k) && r == CoFlow::Continue) { co.stack.back().phase = 1; break; }
                const bool target = IsLoop(k) || (k == SK::Switch && r == CoFlow::Break);
                co.stack.pop_back();
                if (target) break;
            }
        }
        return false;
    }
    CoFlow CoStmt(CoState& co, const Stmt& s) {
        if (!s.hasYield) return FromFlow(Run(s));
        Step(s.loc);
        switch (s.kind) {
            case SK::Yield: return CoYield(co, s);
            case SK::If: {
                const Stmt* b = AsB(Eval(*s.expr)) ? s.then.get() : s.otherwise.get();
                return b ? CoStmt(co, *b) : CoFlow::Normal;
            }
            case SK::Block: co.stack.push_back(CoFrame{&s, &s.body}); return CoFlow::Normal;
            case SK::Switch: {
                if (const SwitchSection* sec = PickSection(s)) co.stack.push_back(CoFrame{&s, &sec->body});
                return CoFlow::Normal;
            }
            case SK::While: case SK::DoWhile: case SK::For: case SK::Foreach:
                co.stack.push_back(CoFrame{&s});
                return CoFlow::Normal;
            default: return FromFlow(Run(s));   // the checker keeps yields out of everything else
        }
    }
    // yield return x: what to wait for before going on.
    CoFlow CoYield(CoState& co, const Stmt& s) {
        if (s.yieldBreak) return CoFlow::End;
        Value v = Eval(*s.expr);
        co.wait = 0.0f;
        co.until.reset();
        co.waitFor.reset();
        if (auto* w = std::get_if<WaitRef>(&v); w && *w) {
            if ((*w)->kind == WaitKind::Seconds) co.wait = (*w)->seconds;
            else if ((*w)->kind == WaitKind::Until || (*w)->kind == WaitKind::While) co.until = *w;
        } else if (auto* c = std::get_if<CoRef>(&v); c && *c) {   // another coroutine: start it if needed, wait for its end
            if (!(*c)->started) StartCo(*c, s.loc);
            if (!(*c)->done) co.waitFor = *c;
        }
        co.stamp = inst_.ticks;
        return CoFlow::Yielded;
    }
    static std::uint32_t CollVersion(const Value& c) {
        if (auto* d = std::get_if<DictRef>(&c)) return (*d)->version;
        if (auto* a = std::get_if<ArrayRef>(&c)) return (*a)->version;
        return 0;
    }
    // Item i of a foreach's collection (a dictionary gives KeyValuePairs), or false past the end.
    bool ForeachItem(const Stmt& s, const Value& c, std::size_t i, Value& out) {
        const LocalDecl& d = s.locals[0];
        if (auto* dict = std::get_if<DictRef>(&c)) {
            if (i >= (*dict)->entries.size()) return false;
            auto kv = std::make_shared<ArrayObj>();
            kv->typeName = prog_.NameOf(d.resolved);
            kv->items = {(*dict)->entries[i].first, (*dict)->entries[i].second};
            out = Value{std::move(kv)};
            return true;
        }
        const ArrayRef& arr = std::get<ArrayRef>(c);
        if (i >= arr->items.size()) return false;
        out = arr->items[i];
        if (d.promote) if (auto* n = std::get_if<std::int32_t>(&out)) out = Value{(float)*n};
        return true;
    }

    Value RunFunc(const FuncObj& f, std::vector<Value>& args, SrcLoc loc) {
        const LambdaDecl& L = *f.decl;
        DepthGuard depth("This lambda", loc);
        std::vector<Value> frame((std::size_t)std::max(L.localCount, (int)L.params.size()));
        for (std::size_t i = 0; i < L.params.size() && i < args.size(); ++i) frame[i] = args[i];
        std::vector<Value>* self = self_;
        std::vector<Value>* locals = locals_;
        const FuncObj* fn = fn_;
        const int cls = cls_;
        ObjPtr savedThis = std::move(this_);
        self_ = f.self ? &f.self->fields : &inst_.Fields();
        cls_ = f.cls;
        this_ = f.self;
        locals_ = &frame;
        fn_ = &f;
        auto restore = [&] { self_ = self; locals_ = locals; fn_ = fn; cls_ = cls; this_ = std::move(savedThis); };
        Value ret;
        try {
            if (L.value) {
                ret = Eval(*L.value);
                if (L.ret.kind == TK::Void) ret = Value{};
            } else if (Run(*L.body) == Flow::Return) {
                ret = std::move(retVal_);
            }
        } catch (...) { restore(); throw; }
        restore();
        return ret;
    }
    Value Call1(const FuncRef& f, const Value& x, SrcLoc loc) { std::vector<Value> a{x}; return Invoke(f, a, loc); }

    // A lambda value: what it captures is shared with the code that made it.
    Value MakeLambda(const Expr& e) {
        const LambdaDecl& L = *e.lambda;
        auto f = std::make_shared<FuncObj>();
        f->decl = &L;
        f->prog = inst_.programPtr();
        f->inst = &inst_;
        f->alive = inst_.AliveToken();
        if (L.usesSelf) f->self = this_;
        f->cls = cls_;
        f->typeName = prog_.NameOf(e.type);
        f->captures.reserve(L.captureFrom.size());
        for (int from : L.captureFrom) {
            if (from < 0) { f->captures.push_back(fn_->captures[(std::size_t)(-1 - from)]); continue; }
            Value& slot = (*locals_)[(std::size_t)from];   // the local moves into a cell both share from now on
            if (!std::holds_alternative<CellRef>(slot)) slot = Value{std::make_shared<Cell>(Cell{std::move(slot)})};
            f->captures.push_back(std::get<CellRef>(slot));
        }
        return Value{std::move(f)};
    }

    void Step(SrcLoc loc) {
        if (++inst_.steps > inst_.stepLimit)
            throw RuntimeError("This ran too long and was stopped (an endless loop?)", loc, true);
    }

    // ---- try / catch / throw ----
    std::vector<ObjPtr> caught_;   ///< the exceptions the running catch blocks caught (for throw;)

    // Exception's Message (a script's own Exception class may not have one).
    static std::string MessageOf(const ScriptObj& ex, const Program& p) {
        for (const FieldDecl& f : p.classes[(std::size_t)p.exceptionClass].fields)
            if (f.name == "Message")
                if (auto* s = std::get_if<std::string>(&ex.fields[(std::size_t)f.index])) return *s;
        return std::string();
    }
    [[noreturn]] void Throw(ObjPtr ex, SrcLoc loc) {
        const std::string text = ex->className + ": " + MessageOf(*ex, prog_);
        throw ScriptThrow(text, loc, std::move(ex));
    }
    // An engine error (null, division by zero, ...) as an Exception a catch can look at.
    ObjPtr AsException(const RuntimeError& err) {
        if (auto* st = dynamic_cast<const ScriptThrow*>(&err)) return st->ex;
        const ClassDecl& k = prog_.classes[(std::size_t)prog_.exceptionClass];
        auto ex = std::make_shared<ScriptObj>();
        ex->cls = prog_.exceptionClass;
        ex->className = k.name;
        ex->fields.resize((std::size_t)k.fieldCount);
        for (const FieldDecl& f : k.fields) ex->fields[(std::size_t)f.index] = f.name == "Message" ? Value{std::string(err.what())} : DefaultFor(f.resolved);
        return ex;
    }
    Flow RunTry(const Stmt& s) {
        Flow flow = Flow::Normal;
        std::exception_ptr pending;   // thrown again once the finally has run
        try {
            flow = Run(*s.then);
        } catch (const RuntimeError& err) {
            if (err.fatal || prog_.exceptionClass < 0) throw;   // an endless loop or recursion: nothing may catch it
            ObjPtr ex = AsException(err);
            const CatchClause* hit = nullptr;
            for (const CatchClause& c : s.catches)
                if (c.resolved.kind == TK::Class && prog_.classes[(std::size_t)ex->cls].isA[(std::size_t)c.resolved.id]) { hit = &c; break; }
            if (!hit) pending = std::current_exception();
            else {
                if (hit->slot >= 0) (*locals_)[(std::size_t)hit->slot] = Value{ex};
                caught_.push_back(ex);
                try { flow = Run(*hit->body); }
                catch (const RuntimeError& again) {
                    caught_.pop_back();
                    if (again.fatal) throw;
                    pending = std::current_exception();
                }
                if (!pending) caught_.pop_back();
            }
        }
        if (s.finallyBody) {
            Value ret = retVal_;   // a return in the try block keeps its value
            Run(*s.finallyBody);
            retVal_ = std::move(ret);
        }
        if (pending) std::rethrow_exception(pending);
        return flow;
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
            case SK::Try: return RunTry(s);
            case SK::Yield: return Flow::Normal;   // only reached inside a coroutine, which runs it itself
            case SK::Throw: {
                if (!s.expr) Throw(caught_.back(), s.loc);   // throw; in a catch (the checker made sure)
                const Value v = Eval(*s.expr);
                const ObjPtr* ex = std::get_if<ObjPtr>(&v);
                if (!ex || !*ex) throw RuntimeError("throw got null instead of an Exception", s.loc);
                Throw(*ex, s.loc);
            }
            case SK::Continue: return Flow::Continue;
            case SK::Switch: {
                // The checker made sure no section runs into the next one.
                const SwitchSection* hit = PickSection(s);
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

    // The section a switch's value picks (or default), or null.
    const SwitchSection* PickSection(const Stmt& s) {
        const Value v = Eval(*s.expr);
        const SwitchSection* def = nullptr;
        for (const SwitchSection& sec : s.sections) {
            if (sec.isDefault) def = &sec;
            for (const Value& c : sec.values) if (Equal(v, c)) return &sec;
        }
        return def;
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
        if (op >= CollOp::Where) return Linq(e, l, a);
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
                l.items = Reorder(l.items, SafeOrder(l.items.size(), [&](std::size_t i, std::size_t j) { return KeyCompare(l.items[i], l.items[j]) < 0; }));
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

    // Sorting order of numbers, text and enums; NaN comes first, as in C#.
    static int KeyCompare(const Value& x, const Value& y) {
        if (auto* s = std::get_if<std::string>(&x)) { const std::string& t = std::get<std::string>(y); return *s < t ? -1 : t < *s ? 1 : 0; }
        if (std::holds_alternative<std::int32_t>(x) && std::holds_alternative<std::int32_t>(y)) {
            const std::int32_t a = std::get<std::int32_t>(x), b = std::get<std::int32_t>(y);
            return a < b ? -1 : b < a ? 1 : 0;
        }
        const float a = AsF(x), b = AsF(y);
        if (std::isnan(a) || std::isnan(b)) return std::isnan(a) ? (std::isnan(b) ? 0 : -1) : 1;
        return a < b ? -1 : b < a ? 1 : 0;
    }

    // The LINQ-style methods and the List methods that take a lambda. They work on
    // a copy of the items, so a lambda that changes the list can't upset them.
    Value Linq(const Expr& e, ArrayObj& l, std::vector<Value>& a) {
        const CollOp op = (CollOp)e.member;
        const std::vector<Value> items = l.items;
        FuncRef fn;
        if (!a.empty()) {
            if (auto* f = std::get_if<FuncRef>(&a[0])) fn = *f;
            else throw RuntimeError("The function given to " + e.name + "() is null", e.args[0]->loc);
        }
        auto test = [&](const Value& x) { return AsB(Call1(fn, x, e.loc)); };
        auto newList = [&](std::vector<Value> v) {
            auto out = std::make_shared<ArrayObj>();
            out->typeName = prog_.NameOf(e.type);
            out->items = std::move(v);
            return Value{std::move(out)};
        };
        const Type& ct = e.object->type;
        const Type elem = ct.kind == TK::List ? prog_.generics[(std::size_t)ct.id].a : prog_.arrays[(std::size_t)ct.id];
        switch (op) {
            case CollOp::Where: case CollOp::FindAll: {
                std::vector<Value> out;
                for (const Value& x : items) if (test(x)) out.push_back(x);
                return newList(std::move(out));
            }
            case CollOp::Select: {
                std::vector<Value> out;
                out.reserve(items.size());
                for (const Value& x : items) out.push_back(Call1(fn, x, e.loc));
                return newList(std::move(out));
            }
            case CollOp::OrderBy: case CollOp::OrderByDescending: {
                std::vector<Value> keys;
                keys.reserve(items.size());
                for (const Value& x : items) keys.push_back(Call1(fn, x, e.loc));
                const bool desc = op == CollOp::OrderByDescending;
                return newList(Reorder(items, SafeOrder(items.size(), [&](std::size_t i, std::size_t j) {
                    const int c = KeyCompare(keys[i], keys[j]);
                    return desc ? c > 0 : c < 0;
                })));
            }
            case CollOp::First: case CollOp::FirstOrDefault: case CollOp::Last: case CollOp::LastOrDefault: {
                const bool last = op == CollOp::Last || op == CollOp::LastOrDefault;
                for (std::size_t k = 0; k < items.size(); ++k) {
                    const Value& x = items[last ? items.size() - 1 - k : k];
                    if (!fn || test(x)) return x;
                }
                if (op == CollOp::FirstOrDefault || op == CollOp::LastOrDefault) return DefaultFor(elem);
                throw RuntimeError(std::string(last ? "Last()" : "First()") + " found nothing" + (fn ? " that matches" : "") + " in this " + l.typeName, e.loc);
            }
            case CollOp::Any:
                for (const Value& x : items) if (!fn || test(x)) return Value{true};
                return Value{false};
            case CollOp::All:
                for (const Value& x : items) if (!test(x)) return Value{false};
                return Value{true};
            case CollOp::Count: {
                if (!fn) return Value{(std::int32_t)items.size()};
                std::int32_t n = 0;
                for (const Value& x : items) if (test(x)) ++n;
                return Value{n};
            }
            case CollOp::Sum: case CollOp::Min: case CollOp::Max: case CollOp::Average: {
                std::vector<Value> v;
                if (fn) for (const Value& x : items) v.push_back(Call1(fn, x, e.loc));
                else v = items;
                const char* name = op == CollOp::Sum ? "Sum()" : op == CollOp::Min ? "Min()" : op == CollOp::Max ? "Max()" : "Average()";
                if (v.empty() && op != CollOp::Sum) throw RuntimeError(std::string(name) + " of an empty " + l.typeName, e.loc);
                const bool ints = e.type.kind == TK::Int;
                if (op == CollOp::Min || op == CollOp::Max) {
                    std::size_t best = 0;
                    for (std::size_t i = 1; i < v.size(); ++i) {
                        const int c = KeyCompare(v[i], v[best]);
                        if (op == CollOp::Min ? c < 0 : c > 0) best = i;
                    }
                    return ints ? Value{AsI(v[best])} : Value{AsF(v[best])};
                }
                if (ints && op == CollOp::Sum) {   // like C#: a sum too big for an int is an error, not a wrong number
                    std::int64_t sum = 0;
                    for (const Value& x : v) {
                        sum += AsI(x);
                        if (sum > INT32_MAX || sum < INT32_MIN) throw RuntimeError("Sum() is too big for an int", e.loc);
                    }
                    return Value{(std::int32_t)sum};
                }
                double sum = 0;
                for (const Value& x : v) sum += std::holds_alternative<std::int32_t>(x) ? (double)std::get<std::int32_t>(x) : (double)AsF(x);
                if (op == CollOp::Average) sum /= (double)v.size();
                return Value{(float)sum};
            }
            case CollOp::ToList: return newList(items);
            case CollOp::Find:
                for (const Value& x : items) if (test(x)) return x;
                return DefaultFor(elem);
            case CollOp::FindIndex:
                for (std::size_t i = 0; i < items.size(); ++i) if (test(items[i])) return Value{(std::int32_t)i};
                return Value{std::int32_t{-1}};
            case CollOp::RemoveAll: {
                std::vector<Value> keep;
                for (const Value& x : items) if (!test(x)) keep.push_back(x);
                const std::int32_t removed = (std::int32_t)(items.size() - keep.size());
                if (removed > 0) { l.items = std::move(keep); ++l.version; }
                return Value{removed};
            }
            case CollOp::ForEach:
                for (const Value& x : items) Call1(fn, x, e.loc);
                return Value{};
            case CollOp::SortBy: {
                l.items = Reorder(items, SafeOrder(items.size(), [&](std::size_t i, std::size_t j) {
                    std::vector<Value> xy{items[i], items[j]};
                    return AsI(Invoke(fn, xy, e.loc)) < 0;
                }));
                ++l.version;
                return Value{};
            }
            default: return Value{};
        }
    }

    bool IsA(const ScriptObj& o, int k) const { return prog_.classes[(std::size_t)o.cls].isA[(std::size_t)k] != 0; }
    static std::string Article(const std::string& n) { return (std::string("aeiouAEIOU").find(n[0]) != std::string::npos ? "an " : "a ") + n; }

    // Run `f` as code of class `k` (the class o is, or one it extends) for object `o`.
    template <typename F>
    Value As(const ObjPtr& o, int k, F&& f) {
        std::vector<Value>* self = self_;
        const int cls = cls_;
        ObjPtr saved = std::move(this_);
        self_ = &o->fields; cls_ = k; this_ = o;
        auto restore = [&] { self_ = self; cls_ = cls; this_ = std::move(saved); };
        Value v;
        try { v = f(); } catch (...) { restore(); throw; }
        restore();
        return v;
    }

    // Class k's part of making `o`: its fields' starting values, then its constructor
    // (which starts with the base class's), or the base class's part when it has none.
    void Construct(const ObjPtr& o, int k, int ctor, std::vector<Value>& args, SrcLoc loc) {
        const ClassDecl& c = prog_.classes[(std::size_t)k];
        // A field that starts as `new` of its own class would come back here forever.
        if (g_depth >= kMaxDepth) throw RuntimeError("Making a " + c.name + " makes more objects without end (a field that starts as new " + c.name + "?)", loc, true);
        DepthGuard depth(c.name, loc);
        As(o, k, [&] {
            for (const FieldDecl& f : c.fields)
                if (f.init) o->fields[(std::size_t)f.index] = Eval(*f.init);
            if (ctor >= 0) CallMethod(ctor, args);
            else if (c.baseClass >= 0) {
                std::vector<Value> none;
                int baseCtor = -1;   // the base's constructor that takes nothing (the checker made sure there is one, if it has any)
                const auto& bm = prog_.classes[(std::size_t)c.baseClass].methods;
                for (std::size_t i = 0; i < bm.size(); ++i) if (bm[i].isCtor && bm[i].params.empty()) baseCtor = (int)i;
                Construct(o, c.baseClass, baseCtor, none, loc);
            }
            return Value{};
        });
    }

    // new Node(...): fields at their defaults, then their starting values, then the constructor.
    Value NewObject(const Expr& e) {
        std::vector<Value> args;
        args.reserve(e.args.size());
        for (auto& a : e.args) args.push_back(Eval(*a));
        const ClassDecl& k = prog_.classes[(std::size_t)e.slot];
        // A field that starts as `new` of its own class would recurse in here forever.
        if (g_depth >= kMaxDepth) throw RuntimeError("Making a " + k.name + " makes more objects without end (a field that starts as new " + k.name + "?)", e.loc, true);
        DepthGuard depth(k.name, e.loc);
        auto o = std::make_shared<ScriptObj>();
        o->cls = e.slot;
        o->className = k.name;
        o->fields.resize((std::size_t)k.fieldCount);
        for (int c = e.slot; c >= 0; c = prog_.classes[(std::size_t)c].baseClass)
            for (const FieldDecl& f : prog_.classes[(std::size_t)c].fields) o->fields[(std::size_t)f.index] = DefaultFor(f.resolved);
        Construct(o, e.slot, e.member, args, e.loc);
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

    // The script a reference (GetComponent<Health>()) points at, found again on each use.
    Instance& ScriptOf(const Expr& recv, bool nullCond = false) {
        const ObjRef r = AsRef(Receiver(recv, nullCond));
        GameObject* g = Resolve(r, recv.loc);
        const std::string& name = ScriptClassName(-1000 - r.type);
        Instance* other = FindScriptInstance(g, name);
        if (!other) throw RuntimeError(g->name + " doesn't have its " + name + " script any more", recv.loc);
        return *other;
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
                if (e.bind == Bind::NewWait) {
                    auto w = std::make_shared<WaitObj>();
                    w->kind = (WaitKind)e.member;
                    if (w->kind == WaitKind::Seconds) w->seconds = AsF(Eval(*e.args[0]));
                    if (w->kind == WaitKind::Until || w->kind == WaitKind::While) w->cond = std::get<FuncRef>(Receiver(*e.args[0]));
                    return Value{std::move(w)};
                }
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
                if (e.type.kind == TK::Class) {   // (Boss)enemy: null stays null, anything else must really be one
                    const ObjPtr* o = std::get_if<ObjPtr>(&a);
                    if (e.slot == 1 && o && *o && !IsA(**o, e.type.id))
                        throw RuntimeError("This " + (*o)->className + " isn't " + Article(prog_.NameOf(e.type)), e.loc);
                    return a;
                }
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
            case EK::Lambda: return MakeLambda(e);
            case EK::Is: case EK::As: {
                const Value a = Eval(*e.lhs);
                const ObjPtr* o = std::get_if<ObjPtr>(&a);
                const bool yes = o && *o && IsA(**o, e.slot);
                if (e.kind == EK::Is) return Value{yes};
                return yes ? a : Value{};
            }
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
            case Bind::ScriptField: return ScriptOf(*e.object, e.nullCond).GetField(e.name);
            case Bind::ScriptSelf: {
                GameObject* g = Resolve(AsRef(Receiver(*e.object, e.nullCond)), e.object->loc);
                return MakeRef(g, e.slot == 0 ? api_.gameObject : api_.transform);
            }
            case Bind::PairKey: case Bind::PairValue:
                return std::get<ArrayRef>(Receiver(*e.object, e.nullCond))->items[e.bind == Bind::PairKey ? 0 : 1];
            case Bind::Local: {
                const Value& v = (*locals_)[(std::size_t)e.slot];
                if (auto* c = std::get_if<CellRef>(&v)) return (*c)->v;
                return v;
            }
            case Bind::Captured: return fn_->captures[(std::size_t)e.slot]->v;
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
            case Bind::Local: {
                Value& slot = (*locals_)[(std::size_t)e.slot];
                if (auto* c = std::get_if<CellRef>(&slot)) (*c)->v = v;
                else slot = v;
                return;
            }
            case Bind::Captured: fn_->captures[(std::size_t)e.slot]->v = v; return;
            case Bind::Field: (*self_)[(std::size_t)e.slot] = v; return;
            case Bind::StaticField: inst_.Statics()[(std::size_t)e.slot] = v; return;
            case Bind::ScriptField:
                if (!ScriptOf(*e.object).SetField(e.name, v)) throw RuntimeError("Can't set " + e.name + " to " + ValueToString(v), e.loc);
                return;
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
        if (type.kind == TK::Func) return CombineFuncs(op, a, b);
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

    // The same function: the same lambda value, or the same method of the same
    // object (so -= OnDied removes what += OnDied added), or the same list of them.
    static bool SameFunc(const FuncRef& a, const FuncRef& b) {
        if (a == b) return true;
        if (!a || !b) return false;
        if (!a->list.empty() || !b->list.empty()) {
            if (a->list.size() != b->list.size()) return false;
            for (std::size_t i = 0; i < a->list.size(); ++i) if (!SameFunc(a->list[i], b->list[i])) return false;
            return true;
        }
        return a->decl->method >= 0 && a->decl->method == b->decl->method && a->cls == b->cls && a->self == b->self &&
               a->inst == b->inst && a->prog == b->prog && !a->alive.expired();
    }
    static std::vector<FuncRef> Parts(const Value& v) {
        const auto* f = std::get_if<FuncRef>(&v);
        if (!f) return {};
        return (*f)->list.empty() ? std::vector<FuncRef>{*f} : (*f)->list;
    }
    static Value FromParts(std::vector<FuncRef> parts, const std::string& typeName) {
        if (parts.empty()) return Value{};
        if (parts.size() == 1) return Value{std::move(parts[0])};
        auto m = std::make_shared<FuncObj>();
        m->typeName = typeName;
        m->list = std::move(parts);
        return Value{std::move(m)};
    }
    // a += b adds b's functions after a's; a -= b takes out the last run of b's
    // functions in a (as in C#). Either gives a new value: copies made before don't change.
    static Value CombineFuncs(T op, const Value& a, const Value& b) {
        std::vector<FuncRef> pa = Parts(a);
        const std::vector<FuncRef> pb = Parts(b);
        const std::string name = !pa.empty() ? pa[0]->typeName : !pb.empty() ? pb[0]->typeName : std::string();
        if (op == T::Plus) { pa.insert(pa.end(), pb.begin(), pb.end()); return FromParts(std::move(pa), name); }
        if (pb.empty() || pb.size() > pa.size()) return a;
        for (std::size_t at = pa.size() - pb.size() + 1; at-- > 0;) {
            bool match = true;
            for (std::size_t j = 0; j < pb.size() && match; ++j) match = SameFunc(pa[at + j], pb[j]);
            if (!match) continue;
            pa.erase(pa.begin() + (std::ptrdiff_t)at, pa.begin() + (std::ptrdiff_t)(at + pb.size()));
            return FromParts(std::move(pa), name);
        }
        return a;
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
        if (auto* f = std::get_if<FuncRef>(&a)) return SameFunc(*f, std::get<FuncRef>(b));
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
            case Bind::Invoke: {
                const Value f = Receiver(*e.object, e.nullCond);   // held while it runs
                std::vector<Value> args;
                args.reserve(e.args.size());
                for (auto& a : e.args) args.push_back(Eval(*a));
                return Invoke(std::get<FuncRef>(f), args, e.loc);
            }
            case Bind::ObjMethod: {   // the method the object's own class has under that name
                ObjPtr o = std::get<ObjPtr>(Receiver(*e.object, e.nullCond));
                std::vector<Value> args;
                args.reserve(e.args.size());
                for (auto& a : e.args) args.push_back(Eval(*a));
                const auto [k, m] = prog_.classes[(std::size_t)o->cls].dispatch[(std::size_t)e.slot];
                if (m < 0) throw RuntimeError("Internal error: " + o->className + " has no " + e.name + "()", e.loc);
                return As(o, k, [&] { return CallMethod(m, args); });
            }
            case Bind::BaseMethod: {
                std::vector<Value> args;
                args.reserve(e.args.size());
                for (auto& a : e.args) args.push_back(Eval(*a));
                const ObjPtr self = this_;   // a copy: As() swaps this_ out
                return As(self, e.member, [&] { return CallMethod(e.slot, args); });
            }
            case Bind::BaseCtor: {
                std::vector<Value> args;
                args.reserve(e.args.size());
                for (auto& a : e.args) args.push_back(Eval(*a));
                const ObjPtr self = this_;   // a copy: As() swaps this_ out
                Construct(self, e.slot, e.member, args, e.loc);
                return Value{};
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
            case Bind::GetScript: {   // the script on the object, or null
                GameObject* g = e.object ? Resolve(AsRef(Receiver(*e.object, e.nullCond)), e.object->loc) : ctx_.self;
                if (!g) throw RuntimeError("This script isn't attached to an object", e.loc);
                return FindScriptInstance(g, ScriptClassName(e.slot)) ? MakeRef(g, ScriptRefType(e.slot)) : Value{};
            }
            case Bind::ScriptMethod: {
                Instance& other = ScriptOf(*e.object, e.nullCond);
                std::vector<Value> args;
                args.reserve(e.args.size());
                for (auto& a : e.args) args.push_back(Eval(*a));
                const int m = other.program().MethodIndex(e.name);
                if (m < 0) throw RuntimeError(other.program().Component()->name + " has no " + e.name + "() any more", e.loc);
                const std::uint64_t steps = other.steps;
                Value r = other.Call(m, std::move(args));
                other.steps = steps;   // a call from another script isn't a callback of its own
                return r;
            }
            case Bind::StartCoroutine: {
                const Value v = Receiver(*e.args[0]);
                const CoRef& co = std::get<CoRef>(v);
                StartCo(co, e.loc);
                return v;
            }
            case Bind::StopCoroutine: {   // takes effect at its next yield
                const Value v = Eval(*e.args[0]);
                if (auto* co = std::get_if<CoRef>(&v); co && *co) (*co)->done = true;
                return Value{};
            }
            case Bind::StopAllCoroutines:
                for (const CoRef& co : inst_.coroutines) co->done = true;
                return Value{};
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

// Once a frame (after Update): resume each coroutine whose wait is over. One that
// paused during this frame waits for the next. An error stops that coroutine only;
// the first one is thrown once the others have had their turn.
void Instance::TickCoroutines(float dt) {
    if (coroutines.empty()) { ++ticks; return; }
    const std::uint64_t now = ticks;
    Exec ex(*prog_, fields_, ctx_, *this);
    std::exception_ptr first;
    for (std::size_t i = 0; i < coroutines.size(); ++i) {   // by index: a coroutine can start more
        const CoRef co = coroutines[i];
        if (co->done || co->stamp >= now) continue;
        try {
            steps = 0;
            if (co->waitFor) {
                if (!co->waitFor->done) continue;
                co->waitFor.reset();
            } else if (co->until) {
                std::vector<Value> none;
                const Value got = ex.Invoke(co->until->cond, none, co->method->loc);
                const bool c = std::holds_alternative<bool>(got) && std::get<bool>(got);
                if (co->until->kind == WaitKind::Until ? !c : c) continue;
                co->until.reset();
            } else if (co->wait > 0.0f) {
                co->wait -= dt;
                if (co->wait > 1e-5f) continue;
            }
            ex.ResumeCo(*co);
        } catch (const RuntimeError& e) {
            co->done = true;
            if (e.fatal || !first) first = std::current_exception();
            if (e.fatal) break;
        }
    }
    coroutines.erase(std::remove_if(coroutines.begin(), coroutines.end(), [](const CoRef& c) { return c->done; }), coroutines.end());
    ++ticks;
    if (first) std::rethrow_exception(first);
}

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
