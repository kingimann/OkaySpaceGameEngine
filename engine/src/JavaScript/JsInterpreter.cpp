// okay::js bytecode machine. Written from ECMA-262: 10.2 ECMAScript function
// objects ([[Call]] and [[Construct]], 10.2.1.1 PrepareForOrdinaryCall, 10.2.2 the
// derived-constructor rules: `this` is not bound until super() returns), 13.15.3
// ApplyStringOrNumericBinaryOperator, 7.2.13 IsLessThan, 13.3.7 the super keyword,
// 14.15 try statements, 27.5.3 generator objects (GeneratorStart, GeneratorResume,
// GeneratorResumeAbrupt, GeneratorYield), 27.7.5 async functions (AsyncFunctionStart,
// Await) and 15.7.14 ClassDefinitionEvaluation.
//
// Design (our own): one value stack of fixed size (zeroed address space, touched
// as used, so pointers into it never move) and a fixed frame array. A JavaScript
// call made by JavaScript code runs in the same loop; library code calling back
// into JavaScript starts a nested loop (Execute) that ends when its entry frame
// returns. A generator or async function keeps its frame (slots, operands and
// try handlers) in its GenObj while it is suspended and copies it back to resume.
// The collector runs only at safe points of this loop: backward jumps and calls.
#include "JsInternal.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace okay::js {

namespace {

using M_t = Runtime::Impl;

std::string Describe(const Value& v) {
    switch (v.t) {
        case Tag::Undefined: return "undefined";
        case Tag::Null: return "null";
        case Tag::String: return "a string";
        case Tag::Number: return "a number";
        case Tag::Bool: return "a boolean";
        case Tag::Symbol: return "a symbol";
        case Tag::BigInt: return "a BigInt";
        case Tag::Object: return v.O()->IsCallable() ? "a function" : "an object";
        default: return "a value";
    }
}

std::string ConstText(const Proto* p, int idx) {
    if (idx < 0 || idx >= (int)p->consts.size() || !p->consts[(std::size_t)idx].IsStr()) return "";
    return Utf16To8(p->consts[(std::size_t)idx].S()->s);
}

// The closure for proto p, its captured cells taken from the running frame (base) or its own closure (parent).
Closure* NewClosure(Runtime& rt, Proto* p, Closure* parent, Value* base) {
    Closure* c = rt.Alloc<Closure>();
    c->code = p;
    c->proto = rt.functionProto;
    c->lazy = true;
    c->ups.reserve(p->ups.size());
    for (const UpDesc& u : p->ups) {
        if (u.fromLocal) {
            const Value& slot = base[u.index];
            if (slot.t != Tag::Internal || slot.p->kind != Kind::Cell) rt.ThrowType("internal error: a captured variable has no cell");
            c->ups.push_back(static_cast<Cell*>(slot.p));
        } else c->ups.push_back(parent->ups[u.index]);
    }
    return c;
}

Cell* NewCellV(Runtime& rt, const Value& v) {
    Cell* c = rt.Alloc<Cell>();
    c->v = v;
    return c;
}

// 7.2.13 IsLessThan for two values already on the stack (converted in place, left first).
// Returns 0 false, 1 true, 2 undefined (NaN).
int LessThan(Runtime& rt, Value* x, Value* y, bool leftFirst) {
    if (x->IsNum() && y->IsNum()) {
        if (std::isnan(x->n) || std::isnan(y->n)) return 2;
        return x->n < y->n ? 1 : 0;
    }
    if (leftFirst) {
        *x = rt.ToPrimitive(*x, 1);
        *y = rt.ToPrimitive(*y, 1);
    } else {
        *y = rt.ToPrimitive(*y, 1);
        *x = rt.ToPrimitive(*x, 1);
    }
    if (x->IsStr() && y->IsStr()) return x->S()->s < y->S()->s ? 1 : 0;
    // 7.2.13 steps 3-4: a BigInt against a string reads the string as a BigInt (not one: undefined)
    if (x->IsBig() && y->IsStr()) {
        Value n;
        if (!StringToBigInt(rt, y->S()->s, n)) return 2;
        return BigCompare(*x, n) < 0 ? 1 : 0;
    }
    if (x->IsStr() && y->IsBig()) {
        Value n;
        if (!StringToBigInt(rt, x->S()->s, n)) return 2;
        return BigCompare(n, *y) < 0 ? 1 : 0;
    }
    *x = rt.ToNumeric(*x);
    *y = rt.ToNumeric(*y);
    if (x->IsBig() && y->IsBig()) return BigCompare(*x, *y) < 0 ? 1 : 0;
    if (x->IsBig()) { int c = BigCompareNum(*x, y->n); return c == 2 ? 2 : c < 0 ? 1 : 0; }
    if (y->IsBig()) { int c = BigCompareNum(*y, x->n); return c == 2 ? 2 : c > 0 ? 1 : 0; }
    double a = x->n, b = y->n;
    if (std::isnan(a) || std::isnan(b)) return 2;
    return a < b ? 1 : 0;
}

// 13.15.3 ApplyStringOrNumericBinaryOperator for everything but string +: ToNumeric both, then Number or
// BigInt arithmetic (mixing the two is a TypeError). The operands stay on the stack while they convert.
double JsMod(double a, double b);
double JsPow(double b, double e);
Value NumericBinary(Runtime& rt, int op, Value* a, Value* b) {
    *a = rt.ToNumeric(*a);
    *b = rt.ToNumeric(*b);
    if (a->IsBig() || b->IsBig()) {
        if (!(a->IsBig() && b->IsBig())) rt.ThrowType("can not mix BigInt and other types; use BigInt(x) or Number(x)");
        return BigBinary(rt, op, *a, *b);
    }
    double x = a->n, y = b->n;
    auto i32 = [&rt](double d) { return rt.ToInt32(Value::Num(d)); };
    switch (op) {
        case OP_Add: return Value::Num(x + y);
        case OP_Sub: return Value::Num(x - y);
        case OP_Mul: return Value::Num(x * y);
        case OP_Div: return Value::Num(x / y);
        case OP_Mod: return Value::Num(JsMod(x, y));
        case OP_Pow: return Value::Num(JsPow(x, y));
        case OP_BitAnd: return Value::Num((double)(i32(x) & i32(y)));
        case OP_BitOr: return Value::Num((double)(i32(x) | i32(y)));
        case OP_BitXor: return Value::Num((double)(i32(x) ^ i32(y)));
        case OP_Shl: return Value::Num((double)(std::int32_t)((std::uint32_t)i32(x) << ((std::uint32_t)i32(y) & 31)));
        case OP_Sar: return Value::Num((double)(i32(x) >> ((std::uint32_t)i32(y) & 31)));
        default: return Value::Num((double)((std::uint32_t)i32(x) >> ((std::uint32_t)i32(y) & 31)));
    }
}

// 13.15.3 for +: the operands stay on the stack while they are converted.
Value AddValues(Runtime& rt, Value* a, Value* b) {
    if (a->IsStr() && b->IsStr()) {
        const std::u16string& x = a->S()->s;
        const std::u16string& y = b->S()->s;
        if (x.empty()) return *b;
        if (y.empty()) return *a;
        rt.Charge((x.size() + y.size()) / 32);
        std::u16string s;
        s.reserve(x.size() + y.size());
        s += x;
        s += y;
        return Value::Str(rt.NewString(std::move(s)));
    }
    *a = rt.ToPrimitive(*a, 0);
    *b = rt.ToPrimitive(*b, 0);
    if (a->IsStr() || b->IsStr()) {
        *a = Value::Str(rt.ToString(*a));
        *b = Value::Str(rt.ToString(*b));
        return AddValues(rt, a, b);
    }
    if (a->IsBig() || b->IsBig()) return NumericBinary(rt, OP_Add, a, b);
    return Value::Num(rt.ToNumber(*a) + rt.ToNumber(*b));
}

double JsMod(double a, double b) {
    if (std::isnan(a) || std::isnan(b) || std::isinf(a) || b == 0) return NAN;
    if (std::isinf(b)) return a;
    if (a == 0) return a;
    double r = std::fmod(a, b);
    return r;
}

double JsPow(double b, double e) {
    // 6.1.6.1.3 Number::exponentiate: NaN exponent gives NaN; |b| = 1 with an infinite exponent gives NaN
    if (std::isnan(e)) return NAN;
    if (e == 0) return 1;
    if ((b == 1 || b == -1) && std::isinf(e)) return NAN;
    return std::pow(b, e);
}

void SaveFrame(Runtime& rt, GenObj* g, Frame& f) {
    M_t& M = *rt.vm;
    Value* from = f.base - 2;
    g->saved.assign(from, M.sp);
    g->pc = (std::uint32_t)(f.pc - f.fn->code->code.data());
    g->handlers.clear();
    for (std::size_t i = f.handlerBase; i < M.handlers.size(); ++i) g->handlers.push_back(M.handlers[i]);
    M.handlers.resize(f.handlerBase);
    g->argc = f.argc;
    g->newTarget = f.newTarget;
    g->extra = f.extra;
    rt.CountBytes((std::ptrdiff_t)(g->saved.size() * sizeof(Value)));
}

ArrayObj* ArgsArray(Runtime& rt, Frame& f, std::uint32_t from) {
    std::uint32_t np = f.fn->code->nparams;
    std::uint32_t inParams = f.argc < np ? f.argc : np;
    ArrayObj* extra = f.extra.IsObj() ? static_cast<ArrayObj*>(f.extra.O()) : nullptr;
    std::size_t total = inParams + (extra ? extra->elems.size() : 0);
    ArrayObj* a = rt.NewArray(total);
    for (std::uint32_t i = from; i < inParams; ++i) a->elems.push_back(f.base[i]);
    if (extra) {
        std::size_t skip = from > np ? from - np : 0;
        for (std::size_t i = skip; i < extra->elems.size(); ++i) a->elems.push_back(extra->elems[i]);
    }
    a->length = (std::uint32_t)a->elems.size();
    return a;
}

void CopyDataProperties(Runtime& rt, Object* target, const Value& src, const std::vector<GcObj*>* excluded) {
    if (src.IsNullish()) return;
    Object* s = rt.ToObject(src);
    rt.Keep(s);
    std::vector<GcObj*> keys = rt.OwnKeys(s, true, true);
    for (GcObj* k : keys) {
        if (excluded && std::find(excluded->begin(), excluded->end(), k) != excluded->end()) continue;
        Prop p;
        if (!rt.GetOwnProperty(s, k, p) || !(p.flags & kEnumerable)) continue;
        Value v = rt.Get(Value::Obj(s), k);
        rt.DefineData(target, k, v);
    }
}

// Unpack an array (or any iterable) onto the stack for a spread call. Returns the count.
int SpreadOnto(Runtime& rt, const Value& arr) {
    M_t& M = *rt.vm;
    ArrayObj* a = static_cast<ArrayObj*>(arr.O());
    std::size_t n = a->elems.size();
    if (M.sp + n + 64 > M.stackEnd) rt.ThrowRange("too many arguments in a spread call");
    for (std::size_t i = 0; i < n; ++i) *M.sp++ = a->elems[i].IsEmpty() ? Value() : a->elems[i];
    return (int)n;
}

void SpreadInto(Runtime& rt, ArrayObj* target, const Value& iterable) {
    if (iterable.IsObj() && iterable.O()->cls == Cls::Array) {
        IterRec* probe = GetIterator(rt, iterable);
        if (probe->fast == 1) {
            ArrayObj* src = static_cast<ArrayObj*>(iterable.O());
            for (std::uint32_t i = 0; i < src->length; ++i) {
                Value v = rt.GetIndex(iterable, i);
                rt.Charge(1);
                rt.DefineIndex(target, target->length, v);
            }
            return;
        }
        Value e;
        while (IterStep(rt, probe, e)) rt.DefineIndex(target, target->length, e);
        return;
    }
    IterRec* r = GetIterator(rt, iterable);
    Value e;
    while (IterStep(rt, r, e)) rt.DefineIndex(target, target->length, e);
}

ForInObj* ForInPrepare(Runtime& rt, const Value& v) {
    ForInObj* fi = rt.Alloc<ForInObj>();
    fi->proto = nullptr;
    if (v.IsNullish()) return fi;
    Object* o = rt.ToObject(v);
    fi->obj = Value::Obj(o);
    std::vector<GcObj*> seen;
    std::unordered_set<GcObj*> seenSet;
    int guard = 0;
    for (Object* q = o; q; q = rt.GetPrototypeOf(q)) {
        rt.Keep(q);
        std::vector<GcObj*> keys = rt.OwnKeys(q, true, false);
        rt.Charge(keys.size());
        for (GcObj* k : keys) {
            if (seenSet.count(k)) continue;
            seenSet.insert(k);
            Prop p;
            if (rt.GetOwnProperty(q, k, p) && (p.flags & kEnumerable)) fi->keys.push_back(k);
        }
        if (++guard > 10000) break;
    }
    return fi;
}

} // namespace

Value MakeIterResult(Runtime& rt, const Value& v, bool done) {
    Object* o = rt.NewObject();
    o->props.reserve(2);
    o->AddRaw(rt.atoms->value, v, kDefault);
    o->AddRaw(rt.atoms->done, Value::Bool(done), kDefault);
    return Value::Obj(o);
}

void EnterClosure(Runtime& rt, Closure* fn, int argc, std::uint8_t flags, Object* newTarget) {
    M_t& M = *rt.vm;
    Proto* p = fn->code;
    Value* args = M.sp - argc;
    if (M.nframes >= M_t::kMaxFrames - 1) rt.ThrowRange("too much recursion (the call stack is full)");
    if (args + p->nslots + p->maxStack + 32 > M.stackEnd) rt.ThrowRange("too much recursion (the value stack is full)");
    Value extra;
    std::uint32_t np = p->nparams;
    if ((std::uint32_t)argc > np) {
        if (p->flags & PF_NeedsExtra) {
            ArrayObj* a = rt.NewArray((std::size_t)argc - np);
            for (int i = (int)np; i < argc; ++i) a->elems.push_back(args[i]);
            a->length = (std::uint32_t)a->elems.size();
            extra = Value::Obj(a);
        }
    } else {
        for (std::uint32_t i = (std::uint32_t)argc; i < np; ++i) args[i] = Value();
    }
    for (std::uint32_t i = np; i < p->nslots; ++i) args[i] = Value();
    M.sp = args + p->nslots;
    Frame& f = M.frames[M.nframes++];
    f.fn = fn;
    f.pc = p->code.data();
    f.base = args;
    f.handlerBase = (std::uint32_t)M.handlers.size();
    f.argc = (std::uint32_t)argc;
    f.flags = flags;
    f.newTarget = newTarget;
    f.gen = nullptr;
    f.extra = extra;
}

Value CallNative(Runtime& rt, NativeFunc* f, Value* argv, int argc, const Value& thisv, Object* newTarget) {
    M_t& M = *rt.vm;
    if (M.nativeDepth >= M_t::kMaxNativeDepth) rt.ThrowRange("too much recursion (library calls nested too deeply)");
    CallArgs a;
    a.argv = argv;
    a.argc = argc;
    a.thisv = thisv;
    a.newTarget = newTarget;
    a.callee = f;
    std::size_t mark = rt.Scratch().size();
    ++M.nativeDepth;
    Value r;
    try {
        r = f->fn(rt, a);
    } catch (...) {
        --M.nativeDepth;
        if (rt.Scratch().size() > mark) rt.Scratch().resize(mark);
        throw;
    }
    --M.nativeDepth;
    if (rt.Scratch().size() > mark) rt.Scratch().resize(mark);
    return r;
}

// [[Call]] with callee, this and args at `at` (at[0], at[1], at[2..]); sp is just past the args.
static Value CallAt(Runtime& rt, Value* at, int argc) {
    M_t& M = *rt.vm;
    Object* o = at[0].O();
    switch (o->cls) {
        case Cls::Closure: {
            Closure* c = static_cast<Closure*>(o);
            if (c->code->flags & PF_ClassCtor) rt.ThrowType("a class constructor must be called with 'new'");
            EnterClosure(rt, c, argc, FF_Entry, nullptr);
            return Execute(rt, M.nframes - 1);
        }
        case Cls::Native: return CallNative(rt, static_cast<NativeFunc*>(o), at + 2, argc, at[1], nullptr);
        case Cls::Bound: {
            BoundFunc* b = static_cast<BoundFunc*>(o);
            std::vector<Value> args(b->args);
            for (int i = 0; i < argc; ++i) args.push_back(at[2 + i]);
            return rt.Call(Value::Obj(b->target), b->thisv, args.data(), (int)args.size());
        }
        case Cls::Proxy:
            if (o->proxyFn) {
                std::vector<Value> args(at + 2, at + 2 + argc);   // the value stack above `at` is reused by the trap call
                return ProxyCall(rt, static_cast<ProxyObj*>(o), at[1], args.data(), argc);
            }
            rt.ThrowType("this value is not a function");
        default: rt.ThrowType("this value is not a function");
    }
}

Value Runtime::Call(const Value& fn, const Value& thisv, const Value* args, int argc) {
    M_t& M = *vm;
    if (!IsCallable(fn)) ThrowType(Describe(fn) + " is not a function");
    if (M.nativeDepth >= M_t::kMaxNativeDepth) ThrowRange("too much recursion (library calls nested too deeply)");
    if (M.sp + argc + 64 > M.stackEnd) ThrowRange("too much recursion (the value stack is full)");
    Value* at = M.sp;
    at[0] = fn;
    at[1] = thisv;
    for (int i = 0; i < argc; ++i) at[2 + i] = args[i];
    M.sp = at + 2 + argc;
    ++M.nativeDepth;
    Value r;
    try {
        r = CallAt(*this, at, argc);
    } catch (...) {
        --M.nativeDepth;
        M.sp = at;
        throw;
    }
    --M.nativeDepth;
    M.sp = at;
    Keep(r);
    return r;
}

Value Runtime::Construct(const Value& fn, const Value* args, int argc, Object* newTarget) {
    M_t& M = *vm;
    if (!IsConstructor(fn)) ThrowType(Describe(fn) + " is not a constructor");
    if (!newTarget) newTarget = fn.O();
    Object* o = fn.O();
    if (o->cls == Cls::Bound) {
        BoundFunc* b = static_cast<BoundFunc*>(o);
        std::vector<Value> all(b->args);
        for (int i = 0; i < argc; ++i) all.push_back(args[i]);
        return Construct(Value::Obj(b->target), all.data(), (int)all.size(), newTarget == o ? b->target : newTarget);
    }
    if (o->cls == Cls::Proxy) return ProxyConstruct(*this, static_cast<ProxyObj*>(o), args, argc, newTarget);
    if (M.nativeDepth >= M_t::kMaxNativeDepth) ThrowRange("too much recursion (library calls nested too deeply)");
    if (M.sp + argc + 64 > M.stackEnd) ThrowRange("too much recursion (the value stack is full)");
    Value* at = M.sp;
    at[0] = fn;
    at[1] = Value::EmptyV();
    for (int i = 0; i < argc; ++i) at[2 + i] = args[i];
    M.sp = at + 2 + argc;
    ++M.nativeDepth;
    Value r;
    try {
        if (o->cls == Cls::Native) {
            r = CallNative(*this, static_cast<NativeFunc*>(o), at + 2, argc, Value(), newTarget);
        } else {
            Closure* c = static_cast<Closure*>(o);
            if (!c->derived) {
                Value pr = Get(Value::Obj(newTarget), atoms->prototype);
                at[1] = Value::Obj(NewObject(pr.IsObj() ? pr.O() : objectProto));
            }
            EnterClosure(*this, c, argc, FF_Entry | FF_Construct, newTarget);
            r = Execute(*this, M.nframes - 1);
        }
    } catch (...) {
        --M.nativeDepth;
        M.sp = at;
        throw;
    }
    --M.nativeDepth;
    M.sp = at;
    Keep(r);
    return r;
}

bool Runtime::CallHost(const Value& fn, const Value& thisv, const std::vector<Value>& args, Value* result, std::string* err) {
    M_t& M = *vm;
    Value* sp0 = M.sp;
    int nf = M.nframes;
    std::size_t nh = M.handlers.size();
    int nd = M.nativeDepth;
    // At the top level nothing else holds scratch values: what the host made since the last call (keys,
    // converted arguments) is dropped once the arguments are on the value stack, so the list does not grow.
    const bool top = M.nativeDepth == 0 && M.nframes == 0;
    try {
        Value r = Call(fn, thisv, args.data(), (int)args.size());
        Keep(r);
        RunJobs();
        if (top) {
            scratch_.clear();
            Keep(r);   // the host reads it before its next call
        }
        if (result) *result = r;
        return true;
    } catch (const JsThrow&) {
        if (err) *err = ExceptionText(exception);
        exception = Value();
    } catch (const JsTerminate& t) {
        if (err) *err = t.why;
        M.jobs.clear();
    }
    M.sp = sp0;
    M.nframes = nf;
    M.handlers.resize(nh);
    M.nativeDepth = nd;
    if (top) scratch_.clear();
    return false;
}

bool Runtime::ConstructHost(const Value& fn, const std::vector<Value>& args, Value* result, std::string* err) {
    M_t& M = *vm;
    Value* sp0 = M.sp;
    int nf = M.nframes;
    std::size_t nh = M.handlers.size();
    int nd = M.nativeDepth;
    // At the top level nothing else holds scratch values: what the host made since the last call (keys,
    // converted arguments) is dropped once the arguments are on the value stack, so the list does not grow.
    const bool top = M.nativeDepth == 0 && M.nframes == 0;
    try {
        Value r = Construct(fn, args.data(), (int)args.size());
        Keep(r);
        RunJobs();
        if (top) {
            scratch_.clear();
            Keep(r);   // the host reads it before its next call
        }
        if (result) *result = r;
        return true;
    } catch (const JsThrow&) {
        if (err) *err = ExceptionText(exception);
        exception = Value();
    } catch (const JsTerminate& t) {
        if (err) *err = t.why;
        M.jobs.clear();
    }
    M.sp = sp0;
    M.nframes = nf;
    M.handlers.resize(nh);
    M.nativeDepth = nd;
    if (top) scratch_.clear();
    return false;
}

void Runtime::RunJobs() {
    M_t& M = *vm;
    if (M.jobDepth > 0) return;
    ++M.jobDepth;
    try {
        while (!M.jobs.empty()) {
            Job j = M.jobs.front();
            M.jobs.pop_front();
            Keep(j.a);
            Keep(j.b);
            Keep(j.c);
            ScratchScope ss(*this);
            RunJob(*this, j);
        }
    } catch (...) {
        --M.jobDepth;
        throw;
    }
    --M.jobDepth;
}

Value ResumeGenerator(Runtime& rt, GenObj* g, int mode, const Value& v) {
    M_t& M = *rt.vm;
    if (g->state == GS_Running) rt.ThrowType("the generator is already running");
    if (g->state == GS_Done || (g->state == GS_Start && mode != 0)) {
        g->state = GS_Done;
        g->saved.clear();
        if (mode == 1) rt.Throw(v);
        return g->async ? Value() : MakeIterResult(rt, mode == 2 ? v : Value(), true);
    }
    if (M.nframes >= M_t::kMaxFrames - 1 || M.sp + g->saved.size() + 64 > M.stackEnd)
        rt.ThrowRange("too much recursion (the call stack is full)");
    Value* at = M.sp;
    for (std::size_t i = 0; i < g->saved.size(); ++i) at[i] = g->saved[i];
    M.sp = at + g->saved.size();
    rt.CountBytes(-(std::ptrdiff_t)(g->saved.size() * sizeof(Value)));
    Closure* fn = static_cast<Closure*>(at[0].O());
    Frame& f = M.frames[M.nframes++];
    f.fn = fn;
    f.pc = fn->code->code.data() + g->pc;
    f.base = at + 2;
    f.handlerBase = (std::uint32_t)M.handlers.size();
    f.argc = g->argc;
    f.flags = FF_Entry;
    f.newTarget = g->newTarget;
    f.gen = g;
    f.extra = g->extra;
    for (const Handler& h : g->handlers) M.handlers.push_back(h);
    bool wasStart = g->state == GS_Start;
    g->saved.clear();
    g->saved.shrink_to_fit();
    g->state = GS_Running;
    if (!wasStart) *M.sp++ = v;
    M.resumeMode = mode;
    Value r;
    M.yielded = false;
    try {
        r = Execute(rt, M.nframes - 1);
    } catch (...) {
        g->state = GS_Done;
        g->saved.clear();
        M.sp = at;
        throw;
    }
    M.sp = at;
    bool y = M.yielded;
    M.yielded = false;
    if (g->async) return r;
    if (y) return MakeIterResult(rt, r, false);
    g->state = GS_Done;
    return MakeIterResult(rt, r, true);
}

// ============================================================ the loop
Value Execute(Runtime& rt, int entry) {
    M_t& M = *rt.vm;
    std::vector<Value>& scratch = rt.Scratch();
    const std::size_t scratchBase = scratch.size();
    std::uint64_t& left = rt.BudgetLeft();
    Frame* f;
    Closure* cl;
    const Instr* pc;
    Value* base;
    const Value* K;
    GcObj* const* ups = nullptr;
    (void)ups;

#define LOAD()                              \
    do {                                    \
        f = &M.frames[M.nframes - 1];       \
        cl = f->fn;                         \
        pc = f->pc;                         \
        base = f->base;                     \
        K = cl->code->consts.data();        \
    } while (0)
#define SAFEPOINT()                                         \
    do {                                                    \
        if (rt.GcDue()) {                                   \
            if (scratch.size() > scratchBase) scratch.resize(scratchBase); \
            rt.Collect();                                   \
        }                                                   \
    } while (0)
#define SP (M.sp)
#define TOP (M.sp[-1])
#define KEY(i) (K[(i)].p)

    LOAD();
    for (;;) {
        try {
            for (;;) {
                if (--left == 0) rt.BudgetOut();
                const Instr I = *pc;
                f->pc = pc;
                ++pc;
                switch (I.op) {
                    case OP_Nop: case OP_Debugger: break;
                    case OP_Undef: *SP++ = Value(); break;
                    case OP_Null: *SP++ = Value::Null(); break;
                    case OP_True: *SP++ = Value::Bool(true); break;
                    case OP_False: *SP++ = Value::Bool(false); break;
                    case OP_PushEmpty: *SP++ = Value::EmptyV(); break;
                    case OP_Const: *SP++ = K[I.a]; break;
                    case OP_Int: *SP++ = Value::Num((double)I.a); break;
                    case OP_Pop: --SP; break;
                    case OP_Dup: SP[0] = SP[-1]; ++SP; break;
                    case OP_Dup2: SP[0] = SP[-2]; SP[1] = SP[-1]; SP += 2; break;
                    case OP_Swap: { Value t = SP[-1]; SP[-1] = SP[-2]; SP[-2] = t; break; }
                    case OP_DupX1: {   // a b -> b a b
                        Value b = SP[-1];
                        SP[0] = b;
                        SP[-1] = SP[-2];
                        SP[-2] = b;
                        ++SP;
                        break;
                    }
                    case OP_DupX2: {   // a b c -> c a b c
                        Value c = SP[-1];
                        SP[0] = c;
                        SP[-1] = SP[-2];
                        SP[-2] = SP[-3];
                        SP[-3] = c;
                        ++SP;
                        break;
                    }

                    // ---- variables
                    case OP_GetLoc: *SP++ = base[I.a]; break;
                    case OP_GetLocChk: {
                        const Value& v = base[I.a];
                        if (v.IsEmpty()) rt.ThrowError(ErrorType::ReferenceError, "'" + ConstText(cl->code, I.b) + "' is used before it is initialized");
                        *SP++ = v;
                        break;
                    }
                    case OP_SetLoc: base[I.a] = TOP; break;
                    case OP_PutLoc: base[I.a] = *--SP; break;
                    case OP_IncLoc: case OP_DecLoc: {
                        Value& v = base[I.a];
                        if (v.IsNum()) v = Value::Num(I.op == OP_IncLoc ? v.n + 1 : v.n - 1);
                        else {
                            Value n = rt.ToNumeric(v);
                            v = n.IsBig() ? BigUnary(rt, I.op == OP_IncLoc ? OP_Inc : OP_Dec, n) : Value::Num(I.op == OP_IncLoc ? n.n + 1 : n.n - 1);
                        }
                        break;
                    }
                    case OP_GetCell: *SP++ = static_cast<Cell*>(base[I.a].p)->v; break;
                    case OP_GetCellChk: {
                        const Value& v = static_cast<Cell*>(base[I.a].p)->v;
                        if (v.IsEmpty()) rt.ThrowError(ErrorType::ReferenceError, "'" + ConstText(cl->code, I.b) + "' is used before it is initialized");
                        *SP++ = v;
                        break;
                    }
                    case OP_SetCell: static_cast<Cell*>(base[I.a].p)->v = TOP; break;
                    case OP_PutCell: static_cast<Cell*>(base[I.a].p)->v = *--SP; break;
                    case OP_NewCell: {
                        Value init = I.b == 0 ? Value() : I.b == 1 ? Value::EmptyV() : base[I.a];
                        base[I.a] = Value::Internal(NewCellV(rt, init));
                        break;
                    }
                    case OP_CopyCell: {
                        Value cur = static_cast<Cell*>(base[I.a].p)->v;
                        base[I.a] = Value::Internal(NewCellV(rt, cur));
                        break;
                    }
                    case OP_GetUp: *SP++ = cl->ups[(std::size_t)I.a]->v; break;
                    case OP_GetUpChk: {
                        const Value& v = cl->ups[(std::size_t)I.a]->v;
                        if (v.IsEmpty()) {
                            std::string n = ConstText(cl->code, I.b);
                            if (n == "%this") rt.ThrowError(ErrorType::ReferenceError, "'this' is used before super() was called");
                            rt.ThrowError(ErrorType::ReferenceError, "'" + n + "' is used before it is initialized");
                        }
                        *SP++ = v;
                        break;
                    }
                    case OP_SetUp: cl->ups[(std::size_t)I.a]->v = TOP; break;
                    case OP_PutUp: cl->ups[(std::size_t)I.a]->v = *--SP; break;

                    // ---- globals (9.1.1.4 global environment records: the lexical part first)
                    case OP_GetGlobal: case OP_TypeofGlobal: {
                        GcObj* k = KEY(I.a);
                        if (Prop* p = rt.lexicals->FindRaw(k)) {
                            if (p->v.IsEmpty()) rt.ThrowError(ErrorType::ReferenceError, "'" + KeyText(rt, k) + "' is used before it is initialized");
                            *SP++ = I.op == OP_TypeofGlobal ? Value::Str(rt.Intern(rt.TypeOf(p->v))) : p->v;
                            break;
                        }
                        Prop* gp = rt.global->FindRaw(k);
                        Value v;
                        if (gp && !(gp->flags & kAccessor)) v = gp->v;
                        else if (gp || rt.HasProperty(rt.global, k)) v = rt.Get(Value::Obj(rt.global), k);
                        else if (I.op == OP_TypeofGlobal) {
                            *SP++ = Value::Str(rt.atoms->undefined);
                            break;
                        } else rt.ThrowError(ErrorType::ReferenceError, "'" + KeyText(rt, k) + "' is not defined");
                        *SP++ = I.op == OP_TypeofGlobal ? Value::Str(rt.Intern(rt.TypeOf(v))) : v;
                        break;
                    }
                    case OP_SetGlobal: {
                        GcObj* k = KEY(I.a);
                        if (Prop* p = rt.lexicals->FindRaw(k)) {
                            if (p->v.IsEmpty()) rt.ThrowError(ErrorType::ReferenceError, "'" + KeyText(rt, k) + "' is used before it is initialized");
                            if (!(p->flags & kWritable)) rt.ThrowType("assignment to the constant '" + KeyText(rt, k) + "'");
                            p->v = TOP;
                            break;
                        }
                        Prop* gp = rt.global->FindRaw(k);
                        if (gp && !(gp->flags & kAccessor) && (gp->flags & kWritable)) { gp->v = TOP; break; }
                        if (!gp && I.b && !rt.HasProperty(rt.global, k)) {   // sloppy code: PutValue step 5.b
                            rt.DefineData(rt.global, k, TOP);
                            break;
                        }
                        if (!gp && !rt.HasProperty(rt.global, k))
                            rt.ThrowError(ErrorType::ReferenceError, "'" + KeyText(rt, k) + "' is not defined (declare it with let, const or var)");
                        rt.Put(Value::Obj(rt.global), k, TOP);
                        break;
                    }
                    case OP_DeclVar: {
                        GcObj* k = KEY(I.a);
                        if (rt.lexicals->FindRaw(k)) rt.ThrowError(ErrorType::SyntaxError, "'" + KeyText(rt, k) + "' is already declared");
                        if (!rt.global->FindRaw(k)) rt.global->AddRaw(k, Value(), kWritable | kEnumerable);
                        break;
                    }
                    case OP_DeclFunc: {
                        GcObj* k = KEY(I.a);
                        if (rt.lexicals->FindRaw(k)) rt.ThrowError(ErrorType::SyntaxError, "'" + KeyText(rt, k) + "' is already declared");
                        Value fn = *--SP;
                        if (Prop* p = rt.global->FindRaw(k)) {
                            p->v = fn;
                            p->get = p->set = nullptr;
                            p->flags = kWritable | kEnumerable;
                        } else rt.global->AddRaw(k, fn, kWritable | kEnumerable);
                        break;
                    }
                    case OP_DeclLex: {
                        GcObj* k = KEY(I.a);
                        if (rt.lexicals->FindRaw(k) || rt.global->FindRaw(k))
                            rt.ThrowError(ErrorType::SyntaxError, "'" + KeyText(rt, k) + "' is already declared");
                        rt.lexicals->AddRaw(k, Value::EmptyV(), I.b ? 0 : kWritable);
                        break;
                    }
                    case OP_InitLex: {
                        GcObj* k = KEY(I.a);
                        Value v = *--SP;
                        if (Prop* p = rt.lexicals->FindRaw(k)) p->v = v;
                        else rt.lexicals->AddRaw(k, v, kWritable);
                        break;
                    }

                    // ---- properties
                    case OP_GetProp: {
                        Value& o = SP[-1];
                        GcObj* k = KEY(I.a);
                        if (o.IsObj() && o.O()->cls == Cls::Ordinary) {
                            Object* ob = o.O();
                            Prop* p = ob->FindRaw(k);
                            if (p && !(p->flags & kAccessor)) { o = p->v; break; }
                            if (!p && ob->proto && ob->proto->cls == Cls::Ordinary) {
                                Prop* q = ob->proto->FindRaw(k);
                                if (q && !(q->flags & kAccessor)) { o = q->v; break; }
                            }
                        }
                        Value r = rt.Get(o, k);
                        SP[-1] = r;
                        break;
                    }
                    case OP_GetPropKeep: {
                        Value o = SP[-1];
                        GcObj* k = KEY(I.a);
                        Value fnv;
                        if (o.IsNullish())
                            rt.ThrowType("can not call '" + KeyText(rt, k) + "' of " + (o.IsNull() ? "null" : "undefined"));
                        fnv = rt.Get(o, k);
                        SP[-1] = fnv;
                        *SP++ = o;
                        break;
                    }
                    case OP_SetProp: {
                        rt.Put(SP[-2], KEY(I.a), SP[-1]);
                        SP[-2] = SP[-1];
                        --SP;
                        break;
                    }
                    case OP_GetElem: {
                        Value& o = SP[-2];
                        Value& k = SP[-1];
                        if (o.IsObj() && k.IsNum() && o.O()->cls == Cls::Array) {
                            ArrayObj* a = static_cast<ArrayObj*>(o.O());
                            double d = k.n;
                            if (d >= 0 && d < (double)a->elems.size()) {
                                std::uint32_t i = (std::uint32_t)d;
                                if ((double)i == d && !a->elems[i].IsEmpty()) {
                                    o = a->elems[i];
                                    --SP;
                                    break;
                                }
                            }
                        } else if (o.IsObj() && k.IsNum() && o.O()->cls == Cls::TypedArray) {
                            o = TypedArrayGet(rt, static_cast<TypedArrayObj*>(o.O()), k.n == 0 ? 0.0 : k.n);
                            --SP;
                            break;
                        } else if (o.IsStr() && k.IsNum()) {
                            const std::u16string& s = o.S()->s;
                            double d = k.n;
                            if (d >= 0 && d < (double)s.size() && (double)(std::size_t)d == d) {
                                o = Value::Str(rt.NewString(std::u16string(1, s[(std::size_t)d])));
                                --SP;
                                break;
                            }
                        }
                        Value r = rt.GetV(o, k);
                        SP[-2] = r;
                        --SP;
                        break;
                    }
                    case OP_GetElemKeep: {
                        Value o = SP[-2];
                        Value r = rt.GetV(o, SP[-1]);
                        SP[-2] = r;
                        SP[-1] = o;
                        break;
                    }
                    case OP_SetElem: {
                        Value& o = SP[-3];
                        Value& k = SP[-2];
                        Value& v = SP[-1];
                        if (o.IsObj() && k.IsNum() && o.O()->cls == Cls::Array) {
                            ArrayObj* a = static_cast<ArrayObj*>(o.O());
                            double d = k.n;
                            if (d >= 0 && d < (double)a->elems.size() && !a->frozenElems) {
                                std::uint32_t i = (std::uint32_t)d;
                                if ((double)i == d && !a->elems[i].IsEmpty()) {
                                    a->elems[i] = v;
                                    SP[-3] = v;
                                    SP -= 2;
                                    break;
                                }
                            }
                        } else if (o.IsObj() && k.IsNum() && o.O()->cls == Cls::TypedArray) {
                            TypedArraySet(rt, static_cast<TypedArrayObj*>(o.O()), k.n == 0 ? 0.0 : k.n, v);
                            SP[-3] = SP[-1];
                            SP -= 2;
                            break;
                        }
                        rt.PutV(o, k, v);
                        SP[-3] = SP[-1];
                        SP -= 2;
                        break;
                    }
                    case OP_DelProp: {
                        Value o = SP[-1];
                        if (o.IsNullish()) rt.ThrowType("can not delete a property of " + Describe(o));
                        Object* ob = rt.ToObject(o);
                        if (!rt.Delete(ob, KEY(I.a))) rt.ThrowType("can not delete '" + KeyText(rt, KEY(I.a)) + "'");
                        SP[-1] = Value::Bool(true);
                        break;
                    }
                    case OP_DelElem: {
                        Value o = SP[-2];
                        if (o.IsNullish()) rt.ThrowType("can not delete a property of " + Describe(o));
                        GcObj* k = rt.ToPropertyKey(SP[-1]);
                        Object* ob = rt.ToObject(o);
                        if (!rt.Delete(ob, k)) rt.ThrowType("can not delete '" + KeyText(rt, k) + "'");
                        SP[-2] = Value::Bool(true);
                        --SP;
                        break;
                    }
                    case OP_In: {
                        Value o = SP[-1];
                        if (!o.IsObj()) rt.ThrowType("the right side of 'in' must be an object, not " + Describe(o));
                        GcObj* k = rt.ToPropertyKey(SP[-2]);
                        bool r = rt.HasProperty(o.O(), k);
                        SP[-2] = Value::Bool(r);
                        --SP;
                        break;
                    }
                    case OP_InstanceOf: {
                        bool r = rt.InstanceOf(SP[-2], SP[-1]);
                        SP[-2] = Value::Bool(r);
                        --SP;
                        break;
                    }
                    case OP_ToKey: {
                        Value& k = SP[-1];
                        if (k.IsNum() && k.n >= 0 && k.n < 4294967295.0 && k.n == std::floor(k.n)) break;
                        if (k.IsStr() || k.IsSym()) break;
                        GcObj* key = rt.ToPropertyKey(k);
                        k = key->kind == Kind::Symbol ? Value::Sym(static_cast<Symbol*>(key)) : Value::Str(static_cast<String*>(key));
                        break;
                    }

                    // ---- arithmetic
                    case OP_Add: {
                        Value& a = SP[-2];
                        Value& b = SP[-1];
                        if (a.IsNum() && b.IsNum()) { a.n += b.n; --SP; break; }
                        Value r = AddValues(rt, &a, &b);
                        SP[-2] = r;
                        --SP;
                        break;
                    }
#define NUMOP(OPC, EXPR)                                                           \
    case OPC: {                                                                    \
        Value& a = SP[-2];                                                         \
        Value& b = SP[-1];                                                         \
        double x, y;                                                               \
        if (a.IsNum() && b.IsNum()) { x = a.n; y = b.n; }                         \
        else { a = NumericBinary(rt, OPC, &a, &b); --SP; break; }                 \
        a = Value::Num(EXPR);                                                      \
        --SP;                                                                      \
        break;                                                                     \
    }
                    NUMOP(OP_Sub, x - y)
                    NUMOP(OP_Mul, x * y)
                    NUMOP(OP_Div, x / y)
                    NUMOP(OP_Mod, JsMod(x, y))
                    NUMOP(OP_Pow, JsPow(x, y))
#undef NUMOP
#define INTOP(OPC, EXPR)                                                           \
    case OPC: {                                                                    \
        Value& a = SP[-2];                                                         \
        Value& b = SP[-1];                                                         \
        if (!(a.IsNum() && b.IsNum()) && (a.IsObj() || b.IsObj() || a.IsBig() || b.IsBig())) { \
            a = NumericBinary(rt, OPC, &a, &b);                                    \
            --SP;                                                                  \
            break;                                                                 \
        }                                                                          \
        std::int32_t x = rt.ToInt32(a);                                            \
        a = Value::Num((double)x);                                                 \
        std::int32_t y = rt.ToInt32(b);                                            \
        a = Value::Num((double)(EXPR));                                            \
        --SP;                                                                      \
        break;                                                                     \
    }
                    INTOP(OP_BitAnd, x & y)
                    INTOP(OP_BitOr, x | y)
                    INTOP(OP_BitXor, x ^ y)
                    INTOP(OP_Shl, (std::int32_t)((std::uint32_t)x << ((std::uint32_t)y & 31)))
                    INTOP(OP_Sar, x >> ((std::uint32_t)y & 31))
                    INTOP(OP_Shr, (double)((std::uint32_t)x >> ((std::uint32_t)y & 31)))
#undef INTOP
                    case OP_Neg: {
                        Value& a = SP[-1];
                        if (a.IsNum()) a = Value::Num(-a.n);
                        else {
                            a = rt.ToNumeric(a);
                            a = a.IsBig() ? BigUnary(rt, OP_Neg, a) : Value::Num(-a.n);
                        }
                        break;
                    }
                    case OP_Pos: {   // unary + is ToNumber: a BigInt is a TypeError (13.5.4)
                        Value& a = SP[-1];
                        if (!a.IsNum()) a = Value::Num(rt.ToNumber(a));
                        break;
                    }
                    case OP_ToNum: {   // the old value of x++ / x--: ToNumeric (13.4.2.1)
                        Value& a = SP[-1];
                        if (!a.IsNum()) a = rt.ToNumeric(a);
                        break;
                    }
                    case OP_Inc: case OP_Dec: {
                        Value& a = SP[-1];
                        if (a.IsNum()) a = Value::Num(I.op == OP_Inc ? a.n + 1 : a.n - 1);
                        else {
                            a = rt.ToNumeric(a);
                            a = a.IsBig() ? BigUnary(rt, I.op, a) : Value::Num(I.op == OP_Inc ? a.n + 1 : a.n - 1);
                        }
                        break;
                    }
                    case OP_Not: SP[-1] = Value::Bool(!rt.ToBoolean(SP[-1])); break;
                    case OP_BitNot: {
                        Value& a = SP[-1];
                        if (a.IsNum()) a = Value::Num((double)~rt.ToInt32(a));
                        else {
                            a = rt.ToNumeric(a);
                            a = a.IsBig() ? BigUnary(rt, OP_BitNot, a) : Value::Num((double)~rt.ToInt32(a));
                        }
                        break;
                    }
                    case OP_Typeof: SP[-1] = Value::Str(rt.Intern(rt.TypeOf(SP[-1]))); break;
                    case OP_Void: SP[-1] = Value(); break;
                    case OP_ToStr: {
                        Value& a = SP[-1];
                        if (!a.IsStr()) {
                            if (a.IsSym()) rt.ThrowType("a Symbol can not be put in a template; use String(sym)");
                            Value s = Value::Str(rt.ToString(a));
                            SP[-1] = s;
                        }
                        break;
                    }
                    case OP_Eq: case OP_Ne: {
                        bool r = rt.LooseEquals(SP[-2], SP[-1]);
                        SP[-2] = Value::Bool(I.op == OP_Eq ? r : !r);
                        --SP;
                        break;
                    }
                    case OP_StrictEq: case OP_StrictNe: {
                        bool r = StrictEquals(SP[-2], SP[-1]);
                        SP[-2] = Value::Bool(I.op == OP_StrictEq ? r : !r);
                        --SP;
                        break;
                    }
                    case OP_Lt: { int r = LessThan(rt, &SP[-2], &SP[-1], true); SP[-2] = Value::Bool(r == 1); --SP; break; }
                    case OP_Gt: { int r = LessThan(rt, &SP[-1], &SP[-2], false); SP[-2] = Value::Bool(r == 1); --SP; break; }
                    case OP_Le: { int r = LessThan(rt, &SP[-1], &SP[-2], false); SP[-2] = Value::Bool(r == 0); --SP; break; }
                    case OP_Ge: { int r = LessThan(rt, &SP[-2], &SP[-1], true); SP[-2] = Value::Bool(r == 0); --SP; break; }

                    // ---- jumps
                    case OP_Jump:
                        if (I.a < (int)(pc - cl->code->code.data())) SAFEPOINT();
                        pc = cl->code->code.data() + I.a;
                        break;
                    case OP_JumpF:
                        if (!rt.ToBoolean(*--SP)) pc = cl->code->code.data() + I.a;
                        break;
                    case OP_JumpT:
                        if (rt.ToBoolean(*--SP)) {
                            if (I.a < (int)(pc - cl->code->code.data())) SAFEPOINT();
                            pc = cl->code->code.data() + I.a;
                        }
                        break;
                    case OP_LtJumpF: {
                        Value& a = SP[-2];
                        Value& b = SP[-1];
                        bool lt;
                        if (a.IsNum() && b.IsNum()) lt = a.n < b.n;
                        else lt = LessThan(rt, &a, &b, true) == 1;
                        SP -= 2;
                        if (!lt) pc = cl->code->code.data() + I.a;
                        break;
                    }
                    case OP_JumpFKeep:
                        if (!rt.ToBoolean(TOP)) pc = cl->code->code.data() + I.a;
                        else --SP;
                        break;
                    case OP_JumpTKeep:
                        if (rt.ToBoolean(TOP)) pc = cl->code->code.data() + I.a;
                        else --SP;
                        break;
                    case OP_JumpNotNullishKeep:
                        if (!TOP.IsNullish()) pc = cl->code->code.data() + I.a;
                        else --SP;
                        break;
                    case OP_JumpNotUndefKeep:
                        if (!TOP.IsUndef()) pc = cl->code->code.data() + I.a;
                        else --SP;
                        break;
                    case OP_JumpNullish:
                        if (TOP.IsNullish()) {
                            TOP = Value();
                            pc = cl->code->code.data() + I.a;
                        }
                        break;
                    case OP_JumpNullishCallee:
                        if (SP[-2].IsNullish()) {
                            SP -= 2;
                            *SP++ = Value();
                            pc = cl->code->code.data() + I.a;
                        }
                        break;

                    // ---- calls
                    case OP_CallSpread: {
                        Value arr = *--SP;
                        int argc = SpreadOnto(rt, arr);
                        goto do_call_n;
                    do_call_n:
                        {
                            Value* fs = SP - argc - 2;
                            Value fnv = fs[0];
                            if (!fnv.IsObj() || !fnv.O()->IsCallable()) rt.ThrowType("the spread call's target is " + Describe(fnv) + ", not a function");
                            Object* o = fnv.O();
                            if (o->cls == Cls::Closure && !(static_cast<Closure*>(o)->code->flags & PF_ClassCtor)) {
                                f->pc = pc;
                                EnterClosure(rt, static_cast<Closure*>(o), argc, 0, nullptr);
                                LOAD();
                                SAFEPOINT();
                                break;
                            }
                            f->pc = pc;
                            Value r = o->cls == Cls::Native ? CallNative(rt, static_cast<NativeFunc*>(o), fs + 2, argc, fs[1], nullptr)
                                                            : CallAt(rt, fs, argc);
                            SP = fs;
                            *SP++ = r;
                        }
                        break;
                    }
                    case OP_Call: {
                        int argc = I.a;
                        Value* fs = SP - argc - 2;
                        const Value& fnv = fs[0];
                        if (fnv.IsObj()) {
                            Object* o = fnv.O();
                            if (o->cls == Cls::Closure) {
                                Closure* c = static_cast<Closure*>(o);
                                if (c->code->flags & PF_ClassCtor) rt.ThrowType("a class constructor must be called with 'new'");
                                f->pc = pc;
                                EnterClosure(rt, c, argc, 0, nullptr);
                                LOAD();
                                SAFEPOINT();
                                break;
                            }
                            if (o->cls == Cls::Native) {
                                f->pc = pc;
                                Value r = CallNative(rt, static_cast<NativeFunc*>(o), fs + 2, argc, fs[1], nullptr);
                                SP = fs;
                                *SP++ = r;
                                break;
                            }
                            if (o->cls == Cls::Bound || o->proxyFn) {
                                f->pc = pc;
                                Value r = CallAt(rt, fs, argc);
                                SP = fs;
                                *SP++ = r;
                                break;
                            }
                        }
                        std::string what = I.b ? ConstText(cl->code, I.b - 1) : "";
                        rt.ThrowType((what.empty() ? std::string("this value") : "'" + what + "'") + " is " + Describe(fnv) + ", not a function");
                    }
                    case OP_NewSpread: case OP_New: {
                        int argc;
                        if (I.op == OP_NewSpread) {
                            Value arr = *--SP;
                            argc = SpreadOnto(rt, arr);
                        } else argc = I.a;
                        Value* fs = SP - argc - 2;
                        Value C = fs[0];
                        if (!rt.IsConstructor(C)) {
                            std::string what = I.b ? ConstText(cl->code, I.b - 1) : "";
                            rt.ThrowType((what.empty() ? std::string("this value") : "'" + what + "'") + " is " + Describe(C) + ", not a constructor");
                        }
                        Object* o = C.O();
                        f->pc = pc;
                        if (o->cls == Cls::Closure) {
                            Closure* c = static_cast<Closure*>(o);
                            if (!c->derived) {
                                Value pr = rt.Get(C, rt.atoms->prototype);
                                fs[1] = Value::Obj(rt.NewObject(pr.IsObj() ? pr.O() : rt.objectProto));
                            } else fs[1] = Value::EmptyV();
                            EnterClosure(rt, c, argc, FF_Construct, o);
                            LOAD();
                            SAFEPOINT();
                            break;
                        }
                        std::vector<Value> args(fs + 2, fs + 2 + argc);
                        Value r = rt.Construct(C, args.data(), argc, o);
                        SP = fs;
                        *SP++ = r;
                        break;
                    }
                    case OP_SuperCall: case OP_SuperCallSpread: {
                        int argc;
                        if (I.op == OP_SuperCallSpread) {
                            Value arr = *--SP;
                            argc = SpreadOnto(rt, arr);
                        } else argc = I.a;
                        Value* args = SP - argc;
                        Object* parent = cl->proto;
                        if (!parent || !rt.IsConstructor(Value::Obj(parent))) rt.ThrowType("the class's parent is not a constructor");
                        if (!base[-1].IsEmpty()) rt.ThrowError(ErrorType::ReferenceError, "super() was already called in this constructor");
                        std::vector<Value> av(args, args + argc);
                        Value r = rt.Construct(Value::Obj(parent), av.data(), argc, f->newTarget ? f->newTarget : cl);
                        SP = args;
                        *SP++ = r;
                        break;
                    }
                    case OP_Return: case OP_ReturnUndef: {
                        Value r = I.op == OP_Return ? *--SP : Value();
                        if (f->flags & FF_Construct) {
                            if (!r.IsObj()) {
                                if (!r.IsUndef() && cl->derived) rt.ThrowType("a derived constructor may only return an object or undefined");
                                r = base[-1];
                                if (r.IsEmpty()) rt.ThrowError(ErrorType::ReferenceError, "super() must be called before the constructor ends");
                            }
                        }
                        if (f->gen && f->gen->async) {
                            GenObj* g = f->gen;
                            g->state = GS_Done;
                            ResolvePromise(rt, g->promise, r);
                            r = Value::Obj(g->promise);
                        }
                        M.handlers.resize(f->handlerBase);
                        SP = f->base - 2;
                        bool wasEntry = M.nframes - 1 == entry;
                        --M.nframes;
                        if (wasEntry) {
                            if (scratch.size() > scratchBase) scratch.resize(scratchBase);
                            return r;
                        }
                        *SP++ = r;
                        LOAD();
                        break;
                    }

                    // ---- functions and objects
                    case OP_Closure: {
                        Closure* c = NewClosure(rt, cl->code->protos[(std::size_t)I.a], cl, base);
                        *SP++ = Value::Obj(c);
                        break;
                    }
                    case OP_NewObject: *SP++ = Value::Obj(rt.NewObject()); break;
                    case OP_NewArray: {
                        int n = I.a;
                        ArrayObj* a = rt.NewArray((std::size_t)n);
                        Value* from = SP - n;
                        a->elems.assign(from, SP);
                        a->length = (std::uint32_t)n;
                        rt.CountBytes((std::ptrdiff_t)(n * sizeof(Value)));
                        SP = from;
                        *SP++ = Value::Obj(a);
                        break;
                    }
                    case OP_ArrPush: {
                        ArrayObj* a = static_cast<ArrayObj*>(SP[-2].O());
                        rt.DefineIndex(a, a->length, SP[-1]);
                        --SP;
                        break;
                    }
                    case OP_ArrHole: {
                        ArrayObj* a = static_cast<ArrayObj*>(SP[-1].O());
                        a->elems.push_back(Value::EmptyV());
                        a->length = (std::uint32_t)a->elems.size();
                        break;
                    }
                    case OP_ArrSpread: {
                        ArrayObj* a = static_cast<ArrayObj*>(SP[-2].O());
                        SpreadInto(rt, a, SP[-1]);
                        --SP;
                        break;
                    }
                    case OP_DefField: {
                        Object* o = SP[-3].O();
                        GcObj* k = rt.ToPropertyKey(SP[-2]);
                        rt.DefineData(o, k, SP[-1]);
                        SP -= 2;
                        break;
                    }
                    case OP_DefFieldK: {
                        Object* o = SP[-2].O();
                        GcObj* k = KEY(I.a);
                        if (o->cls == Cls::Ordinary && o->extensible && !o->FindRaw(k)) {
                            o->AddRaw(k, SP[-1], kDefault);
                            rt.CountBytes(sizeof(Prop));
                        } else rt.DefineData(o, k, SP[-1]);
                        --SP;
                        break;
                    }
                    case OP_SetName: {
                        // [key fn]: an anonymous function or class stored under a computed key takes the key as its name
                        Value fnv = SP[-1];
                        if (fnv.IsObj() && fnv.O()->cls == Cls::Closure && !static_cast<Closure*>(fnv.O())->code->name) {
                            GcObj* k = rt.ToPropertyKey(SP[-2]);
                            rt.MakeLazyProps(fnv.O());
                            String* nm = k->kind == Kind::String ? static_cast<String*>(k)
                                                                  : rt.NewString(u"[" + (static_cast<Symbol*>(k)->desc ? static_cast<Symbol*>(k)->desc->s : u"") + u"]");
                            if (Prop* p = fnv.O()->FindRaw(rt.atoms->name)) p->v = Value::Str(nm);
                        }
                        break;
                    }
                    case OP_DefMethod: case OP_DefGetter: case OP_DefSetter: {
                        Object* o = SP[-3].O();
                        GcObj* k = rt.ToPropertyKey(SP[-2]);
                        Closure* fn = static_cast<Closure*>(SP[-1].O());
                        fn->home = o;
                        if (!fn->code->name || I.op != OP_DefMethod) {
                            rt.MakeLazyProps(fn);
                            std::u16string nm = k->kind == Kind::String ? static_cast<String*>(k)->s
                                                                         : u"[" + (static_cast<Symbol*>(k)->desc ? static_cast<Symbol*>(k)->desc->s : u"") + u"]";
                            if (I.op == OP_DefGetter) nm = u"get " + nm;
                            if (I.op == OP_DefSetter) nm = u"set " + nm;
                            if (Prop* p = fn->FindRaw(rt.atoms->name)) p->v = Value::Str(rt.NewString(nm));
                        }
                        std::uint8_t fl = I.b ? kDefault : kHidden;
                        if (I.op == OP_DefMethod) rt.DefineData(o, k, Value::Obj(fn), fl);
                        else {
                            Prop* ex = o->FindRaw(k);
                            Object* g = I.op == OP_DefGetter ? fn : (ex && (ex->flags & kAccessor) ? ex->get : nullptr);
                            Object* s = I.op == OP_DefSetter ? fn : (ex && (ex->flags & kAccessor) ? ex->set : nullptr);
                            if (ex && !(ex->flags & kAccessor)) o->RemoveRaw(k);
                            rt.DefineAccessor(o, k, g, s, (std::uint8_t)(fl & (kEnumerable | kConfigurable)));
                        }
                        SP -= 2;
                        break;
                    }
                    case OP_TemplateObj: {
                        // 13.2.8.4 GetTemplateObject: consts [a] cache, [a+1 ..] cooked strings, then the raw strings
                        std::vector<Value>& kc = cl->code->consts;
                        if (!kc[(std::size_t)I.a].IsObj()) {
                            const std::size_t n = I.b, at = (std::size_t)I.a + 1;
                            ArrayObj* raw = rt.NewArray(n);
                            ArrayObj* tpl = rt.NewArray(n);
                            for (std::size_t k = 0; k < n; ++k) {
                                tpl->elems.push_back(kc[at + k]);
                                raw->elems.push_back(kc[at + n + k]);
                            }
                            for (ArrayObj* arr : {raw, tpl}) {
                                arr->length = (std::uint32_t)n;
                                arr->frozenElems = arr->sealedElems = true;
                                arr->lengthWritable = false;
                            }
                            tpl->AddRaw(rt.atoms->raw, Value::Obj(raw), 0);
                            raw->extensible = tpl->extensible = false;
                            kc[(std::size_t)I.a] = Value::Obj(tpl);
                        }
                        *SP++ = kc[(std::size_t)I.a];
                        break;
                    }
                    case OP_SetProtoLit: {
                        // [obj value]: B.3.1 PropertyDefinitionEvaluation for `__proto__: value`
                        if (SP[-1].IsObj()) rt.SetPrototype(SP[-2].O(), SP[-1].O());
                        else if (SP[-1].IsNull()) rt.SetPrototype(SP[-2].O(), nullptr);
                        --SP;
                        break;
                    }
                    case OP_CopyProps: {
                        CopyDataProperties(rt, SP[-2].O(), SP[-1], nullptr);
                        --SP;
                        break;
                    }
                    case OP_RestObject: {
                        int n = I.a;
                        std::vector<GcObj*> ex;
                        for (int i = 0; i < n; ++i) ex.push_back(rt.ToPropertyKey(SP[-n + i]));
                        Value src = SP[-n - 1];
                        Object* o = rt.NewObject();
                        SP[-n - 1] = Value::Obj(o);
                        CopyDataProperties(rt, o, src, &ex);
                        SP -= n;
                        break;
                    }
                    case OP_Class: {
                        Value h = *--SP;
                        Object* protoParent = rt.objectProto;
                        Object* ctorParent = rt.functionProto;
                        if (I.b) {
                            if (h.IsNull()) protoParent = nullptr;
                            else {
                                if (!rt.IsConstructor(h)) rt.ThrowType("a class can only extend a constructor (or null), not " + Describe(h));
                                *SP++ = h;   // keep it while its prototype is read
                                Value pp = rt.Get(h, rt.atoms->prototype);
                                --SP;
                                if (!pp.IsObj() && !pp.IsNull()) rt.ThrowType("the parent class's prototype must be an object or null");
                                protoParent = pp.IsNull() ? nullptr : pp.O();
                                ctorParent = h.O();
                            }
                        }
                        Object* protoObj = rt.NewObject(protoParent);
                        *SP++ = Value::Obj(protoObj);
                        Closure* c = NewClosure(rt, cl->code->protos[(std::size_t)I.a], cl, base);
                        c->proto = ctorParent;
                        c->classCtor = true;
                        c->derived = I.b != 0;
                        rt.MakeLazyProps(c);
                        c->AddRaw(rt.atoms->prototype, Value::Obj(protoObj), 0);
                        protoObj->AddRaw(rt.atoms->constructor, Value::Obj(c), kHidden);
                        SP[-1] = Value::Obj(c);
                        *SP++ = Value::Obj(protoObj);
                        break;
                    }
                    case OP_SetFieldInit: {
                        Closure* c = static_cast<Closure*>(SP[-3].O());
                        Closure* fn = static_cast<Closure*>(SP[-1].O());
                        fn->home = SP[-2].O();
                        c->fields = fn;
                        --SP;
                        break;
                    }
                    case OP_RunFields: {
                        Value thisv = SP[-1];
                        if (cl->fields) {
                            f->pc = pc;
                            rt.Call(Value::Obj(cl->fields), thisv, nullptr, 0);
                        }
                        --SP;
                        break;
                    }
                    case OP_LoadThis: *SP++ = base[-1].IsEmpty() ? Value() : base[-1]; break;
                    case OP_LoadThisChk:
                        if (base[-1].IsEmpty()) rt.ThrowError(ErrorType::ReferenceError, "'this' is used before super() was called");
                        *SP++ = base[-1];
                        break;
                    case OP_StoreThis: base[-1] = *--SP; break;
                    case OP_LoadNewTarget: *SP++ = f->newTarget ? Value::Obj(f->newTarget) : Value(); break;
                    case OP_LoadFunc: *SP++ = base[-2]; break;
                    case OP_LoadHome: *SP++ = cl->home ? Value::Obj(cl->home) : Value(); break;
                    case OP_SuperGet: {
                        Value thisv = SP[-3], home = SP[-2];
                        GcObj* k = rt.ToPropertyKey(SP[-1]);
                        if (!home.IsObj()) rt.ThrowType("'super' is not available here");
                        Object* p = home.O()->proto;
                        if (!p) rt.ThrowType("can not read '" + KeyText(rt, k) + "' of super: there is no parent prototype");
                        Value r = rt.Get(Value::Obj(p), k, thisv);
                        SP[-3] = r;
                        SP -= 2;
                        break;
                    }
                    case OP_SuperSet: {
                        Value thisv = SP[-4], home = SP[-3], v = SP[-1];
                        GcObj* k = rt.ToPropertyKey(SP[-2]);
                        if (!home.IsObj() || !home.O()->proto) rt.ThrowType("'super' is not available here");
                        bool done = false;
                        for (Object* q = home.O()->proto; q && !done; q = q->proto) {
                            Prop pr;
                            if (!rt.GetOwnProperty(q, k, pr)) continue;
                            if (pr.flags & kAccessor) {
                                if (!pr.set) rt.ThrowType("'" + KeyText(rt, k) + "' has a getter but no setter");
                                rt.Call(Value::Obj(pr.set), thisv, &v, 1);
                                done = true;
                            } else if (!(pr.flags & kWritable)) rt.ThrowType("'" + KeyText(rt, k) + "' is read-only");
                            break;
                        }
                        if (!done) {
                            if (!thisv.IsObj()) rt.ThrowType("can not set a property on a primitive value");
                            rt.DefineData(thisv.O(), k, v);
                        }
                        SP[-4] = v;
                        SP -= 3;
                        break;
                    }

                    // ---- iteration
                    case OP_GetIter: {
                        IterRec* r = GetIterator(rt, SP[-1]);
                        SP[-1] = Value::Obj(r);
                        break;
                    }
                    case OP_ImportCall: {   // 13.3.10.1: [specifier] -> [promise]; failures reject the promise
                        PromiseObj* p = NewPromise(rt);
                        Value spec = SP[-1];
                        SP[-1] = Value::Obj(p);
                        try {
                            std::string s = rt.ToUtf8(spec);
                            if (rt.importHook) {
                                Value r = rt.importHook(rt, s);
                                ResolvePromise(rt, p, r);
                            } else
                                rt.ThrowType("modules can not be loaded here (import('" + s.substr(0, 200) + "'))");
                        } catch (const JsThrow&) {
                            Value e = rt.exception;
                            rt.exception = Value();
                            RejectPromise(rt, p, e);
                        }
                        break;
                    }
                    // ---- private names (6.2.12, 7.3.31-7.3.33 PrivateFieldAdd / PrivateMethodOrAccessorAdd / PrivateGet / PrivateSet)
                    case OP_NewPrivate: {
                        Symbol* y = rt.NewSymbol(static_cast<String*>(KEY(I.a)));
                        y->priv = true;
                        *SP++ = Value::Sym(y);
                        break;
                    }
                    case OP_GetPrivate: {   // [obj key] -> [value]
                        Symbol* key = SP[-1].Y();
                        if (!SP[-2].IsObj()) rt.ThrowType("can not read " + KeyText(rt, key->desc) + " of " + Describe(SP[-2]));
                        Object* o = SP[-2].O();
                        Prop* p = o->FindRaw(key);
                        if (!p) rt.ThrowType("can not read " + KeyText(rt, key->desc) + ": the object was not made by its class");
                        if (p->flags & kAccessor) {
                            if (!p->get) rt.ThrowType(KeyText(rt, key->desc) + " has a setter but no getter");
                            Value g = Value::Obj(p->get), ov = SP[-2];
                            f->pc = pc;
                            Value r = rt.Call(g, ov, nullptr, 0);
                            SP[-2] = r;
                        } else SP[-2] = p->v;
                        --SP;
                        break;
                    }
                    case OP_SetPrivate: {   // [obj key value] -> [value]
                        Symbol* key = SP[-2].Y();
                        if (!SP[-3].IsObj()) rt.ThrowType("can not set " + KeyText(rt, key->desc) + " of " + Describe(SP[-3]));
                        Object* o = SP[-3].O();
                        Prop* p = o->FindRaw(key);
                        if (!p) rt.ThrowType("can not set " + KeyText(rt, key->desc) + ": the object was not made by its class");
                        Value v = SP[-1];
                        if (p->flags & kAccessor) {
                            if (!p->set) rt.ThrowType(KeyText(rt, key->desc) + " has a getter but no setter");
                            Value s = Value::Obj(p->set), ov = SP[-3];
                            f->pc = pc;
                            rt.Call(s, ov, &v, 1);
                        } else if (!(p->flags & kWritable)) rt.ThrowType("the private method " + KeyText(rt, key->desc) + " can not be assigned");
                        else p->v = v;
                        SP[-3] = v;
                        SP -= 2;
                        break;
                    }
                    case OP_DefPrivate: {   // [obj key value] -> []; b: 0 field, 1 method, 2 getter, 3 setter
                        Symbol* key = SP[-2].Y();
                        Object* o = SP[-3].O();
                        Prop* p = o->FindRaw(key);
                        if (I.b == 2 || I.b == 3) {
                            Object* fn = SP[-1].O();
                            if (p && (!(p->flags & kAccessor) || (I.b == 2 ? p->get : p->set)))
                                rt.ThrowType(KeyText(rt, key->desc) + " is already on this object");
                            if (!p) {
                                p = o->AddRaw(key, Value(), kAccessor);
                                rt.CountBytes(sizeof(Prop));
                            }
                            (I.b == 2 ? p->get : p->set) = fn;
                        } else {
                            if (p) rt.ThrowType(KeyText(rt, key->desc) + " is already on this object (a class initialized it twice)");
                            o->AddRaw(key, SP[-1], I.b == 0 ? kWritable : 0);   // even on a non-extensible object (7.3.31)
                            rt.CountBytes(sizeof(Prop));
                        }
                        SP -= 3;
                        break;
                    }
                    case OP_HasPrivate: {   // [key obj] -> [bool]
                        if (!SP[-1].IsObj()) rt.ThrowType("the right side of 'in' must be an object, not " + Describe(SP[-1]));
                        SP[-2] = Value::Bool(SP[-1].O()->FindRaw(SP[-2].p) != nullptr);
                        --SP;
                        break;
                    }
                    case OP_SetHome:   // [fn home]: the static initializer's [[HomeObject]] is the class
                        if (SP[-2].IsObj() && SP[-2].O()->cls == Cls::Closure && SP[-1].IsObj())
                            static_cast<Closure*>(SP[-2].O())->home = SP[-1].O();
                        break;
                    case OP_GetAsyncIter: {   // 7.4.3 GetIterator(obj, async)
                        Value v = SP[-1];
                        if (v.IsNullish()) rt.ThrowType(std::string(v.IsNull() ? "null" : "undefined") + " is not iterable");
                        Value m = rt.Get(v, rt.symAsyncIterator);
                        IterRec* r;
                        if (!m.IsNullish()) {
                            if (!rt.IsCallable(m)) rt.ThrowType("[Symbol.asyncIterator] is not a function");
                            Value it = rt.Call(m, v, nullptr, 0);
                            if (!it.IsObj()) rt.ThrowType("[Symbol.asyncIterator]() must return an object");
                            rt.Keep(it);
                            r = rt.Alloc<IterRec>();
                            r->proto = nullptr;
                            r->iter = it;
                            r->next = rt.Get(it, rt.atoms->next);
                            r->async = 1;
                        } else {
                            r = GetIterator(rt, v);
                            r->async = 2;
                        }
                        SP[-1] = Value::Obj(r);
                        break;
                    }
                    case OP_AsyncIterNext: {   // [rec] -> [what next() gave: a promise, or a sync step's result object]
                        IterRec* r = static_cast<IterRec*>(SP[-1].O());
                        if (r->async == 1) {
                            if (!rt.IsCallable(r->next)) rt.ThrowType("the async iterator's next is not a function");
                            SP[-1] = rt.Call(r->next, r->iter, nullptr, 0);
                        } else {
                            Value v;
                            bool more = IterStep(rt, r, v);
                            SP[-1] = MakeIterResult(rt, more ? v : Value(), !more);
                        }
                        break;
                    }
                    case OP_AsyncIterValue: {   // [result rec] -> [value fromSync], or jump to a when done
                        IterRec* r = static_cast<IterRec*>(SP[-1].O());
                        Value res = SP[-2];
                        if (!res.IsObj()) rt.ThrowType("an async iterator's next() must give an object");
                        if (rt.ToBoolean(rt.Get(res, rt.atoms->done))) {
                            r->done = true;
                            SP -= 2;
                            pc = cl->code->code.data() + I.a;
                            break;
                        }
                        SP[-2] = rt.Get(res, rt.atoms->value);
                        SP[-1] = Value::Bool(r->async == 2);
                        break;
                    }
                    case OP_IterNext: {
                        IterRec* r = static_cast<IterRec*>(SP[-1].O());
                        Value v;
                        if (r->fast == 1 && !r->done) {   // an unmodified array: read it directly
                            ArrayObj* a = static_cast<ArrayObj*>(r->iter.O());
                            if (r->pos < a->length) {
                                std::uint32_t i = r->pos++;
                                SP[-1] = (i < a->elems.size() && !a->elems[i].IsEmpty()) ? a->elems[i] : rt.GetIndex(r->iter, i);
                                break;
                            }
                            r->done = true;
                            --SP;
                            pc = cl->code->code.data() + I.a;
                            break;
                        }
                        if (IterStep(rt, r, v)) SP[-1] = v;
                        else {
                            --SP;
                            pc = cl->code->code.data() + I.a;
                        }
                        break;
                    }
                    case OP_IterValue: {
                        IterRec* r = static_cast<IterRec*>(SP[-1].O());
                        Value v;
                        if (!IterStep(rt, r, v)) v = Value();
                        SP[-1] = v;
                        break;
                    }
                    case OP_IterStep: {
                        IterRec* r = static_cast<IterRec*>(SP[-1].O());
                        Value v;
                        IterStep(rt, r, v);
                        --SP;
                        break;
                    }
                    case OP_IterRest: {
                        IterRec* r = static_cast<IterRec*>(SP[-1].O());
                        ArrayObj* a = rt.NewArray();
                        SP[-1] = Value::Obj(a);
                        *SP++ = Value::Obj(r);
                        Value v;
                        while (IterStep(rt, r, v)) rt.DefineIndex(a, a->length, v);
                        --SP;
                        break;
                    }
                    case OP_IterClose: {
                        IterRec* r = static_cast<IterRec*>(SP[-1].O());
                        IterClose(rt, r);
                        --SP;
                        break;
                    }
                    case OP_IterSend: {
                        IterRec* r = static_cast<IterRec*>(SP[-2].O());
                        Value res;
                        if (r->fast) {   // yield* over an array or a string: step it like any iterator
                            Value v;
                            bool more = IterStep(rt, r, v);
                            res = MakeIterResult(rt, v, !more);
                        } else {
                            Value arg = SP[-1];
                            res = rt.Call(r->next, r->iter, &arg, 1);
                            if (!res.IsObj()) rt.ThrowType("an iterator's next() must return an object");
                        }
                        SP[-2] = res;
                        --SP;
                        break;
                    }
                    case OP_ForInPrep: {
                        ForInObj* fi = ForInPrepare(rt, SP[-1]);
                        SP[-1] = Value::Obj(fi);
                        break;
                    }
                    case OP_ForInNext: {
                        ForInObj* fi = static_cast<ForInObj*>(SP[-1].O());
                        bool got = false;
                        while (fi->pos < fi->keys.size()) {
                            GcObj* k = fi->keys[fi->pos++];
                            if (fi->obj.IsObj() && !rt.HasProperty(fi->obj.O(), k)) continue;   // deleted while looping
                            SP[-1] = Value::Str(static_cast<String*>(k));
                            got = true;
                            break;
                        }
                        if (!got) {
                            --SP;
                            pc = cl->code->code.data() + I.a;
                        }
                        break;
                    }

                    // ---- exceptions
                    case OP_Throw: {
                        Value v = *--SP;
                        if (M.handlers.size() > f->handlerBase) {   // caught in this function: no C++ exception
                            Handler h = M.handlers.back();
                            M.handlers.pop_back();
                            SP = base + h.sp;
                            *SP++ = v;
                            pc = cl->code->code.data() + h.pc;
                            break;
                        }
                        rt.Charge(64);
                        rt.throwWhere = rt.Where();
                        rt.Throw(v);
                    }
                    case OP_ThrowConst: rt.ThrowError((ErrorType)I.b, ConstText(cl->code, I.a));
                    case OP_TryBegin: M.handlers.push_back(Handler{(std::uint32_t)I.a, (std::uint32_t)(SP - base)}); break;
                    case OP_TryEnd: M.handlers.pop_back(); break;

                    // ---- generators and async functions
                    case OP_GenStart: {
                        GenObj* g = rt.Alloc<GenObj>();
                        Value pv = rt.Get(base[-2], rt.atoms->prototype);
                        g->proto = pv.IsObj() ? pv.O() : rt.generatorProto;
                        f->pc = pc;
                        SaveFrame(rt, g, *f);
                        g->state = GS_Start;
                        Value r = Value::Obj(g);
                        SP = f->base - 2;
                        bool wasEntry = M.nframes - 1 == entry;
                        --M.nframes;
                        if (wasEntry) {
                            if (scratch.size() > scratchBase) scratch.resize(scratchBase);
                            return r;
                        }
                        *SP++ = r;
                        LOAD();
                        break;
                    }
                    case OP_AsyncStart: {
                        GenObj* g = rt.Alloc<GenObj>();
                        g->async = true;
                        g->proto = nullptr;
                        g->promise = NewPromise(rt);
                        g->state = GS_Running;
                        f->gen = g;
                        break;
                    }
                    case OP_Yield: {
                        Value v = *--SP;
                        GenObj* g = f->gen;
                        f->pc = pc;
                        SaveFrame(rt, g, *f);
                        g->state = GS_Yield;
                        SP = f->base - 2;
                        --M.nframes;
                        M.yielded = true;
                        if (scratch.size() > scratchBase) scratch.resize(scratchBase);
                        return v;
                    }
                    case OP_Await: {
                        Value v = *--SP;
                        GenObj* g = f->gen;
                        f->pc = pc;
                        *SP++ = v;   // kept while the promise is made
                        AwaitValue(rt, g, v);
                        --SP;
                        SaveFrame(rt, g, *f);
                        g->state = GS_Yield;
                        Value r = Value::Obj(g->promise);
                        SP = f->base - 2;
                        bool wasEntry = M.nframes - 1 == entry;
                        --M.nframes;
                        if (wasEntry) {
                            M.yielded = true;
                            if (scratch.size() > scratchBase) scratch.resize(scratchBase);
                            return r;
                        }
                        *SP++ = r;
                        LOAD();
                        break;
                    }
                    case OP_ResumeCheck: {
                        int mode = M.resumeMode;
                        M.resumeMode = 0;
                        if (mode == 0) pc = cl->code->code.data() + I.a;
                        else if (mode == 1) {
                            Value v = *--SP;
                            rt.Throw(v);
                        }
                        break;   // mode 2: fall into the compiled return path
                    }

                    // ---- misc
                    case OP_Arguments: {
                        ArrayObj* a = ArgsArray(rt, *f, 0);
                        a->cls = Cls::Arguments;
                        a->proto = rt.objectProto;
                        a->AddRaw(rt.symIterator, Value::Obj(M.arrayValues), kHidden);
                        *SP++ = Value::Obj(a);
                        break;
                    }
                    case OP_RestArgs: {
                        ArrayObj* a = ArgsArray(rt, *f, I.b ? 0 : (std::uint32_t)I.a);
                        if (!I.b && f->argc <= cl->code->nparams) a->elems.clear(), a->length = 0;
                        *SP++ = Value::Obj(a);
                        break;
                    }
                    case OP_NewRegExp: {
                        Value r = MakeRegExpLiteral(rt, SP[-2].S(), SP[-1].S());
                        SP[-2] = r;
                        --SP;
                        break;
                    }
                    case OP_CheckCoercible:
                        if (TOP.IsNullish()) rt.ThrowType(std::string("can not destructure ") + (TOP.IsNull() ? "null" : "undefined"));
                        break;
                    default: rt.ThrowType("internal error: unknown instruction");
                }
            }
        } catch (const JsThrow&) {
            // 14.15.3: find the innermost try in this call level
            for (;;) {
                Frame& fr = M.frames[M.nframes - 1];
                if (M.handlers.size() > fr.handlerBase) {
                    Handler h = M.handlers.back();
                    M.handlers.pop_back();
                    M.sp = fr.base + h.sp;
                    *M.sp++ = rt.exception;
                    rt.exception = Value();
                    fr.pc = fr.fn->code->code.data() + h.pc;
                    break;
                }
                bool wasEntry = M.nframes - 1 == entry;
                if (fr.gen && fr.gen->async) {
                    // an async function's error rejects its promise; the caller gets the promise
                    GenObj* g = fr.gen;
                    g->state = GS_Done;
                    Value exc = rt.exception;
                    rt.exception = Value();
                    M.sp = fr.base - 2;
                    *M.sp++ = exc;
                    RejectPromise(rt, g->promise, exc);
                    --M.sp;
                    --M.nframes;
                    Value r = Value::Obj(g->promise);
                    if (wasEntry) {
                        if (scratch.size() > scratchBase) scratch.resize(scratchBase);
                        return r;
                    }
                    *M.sp++ = r;
                    break;
                }
                M.sp = fr.base - 2;
                --M.nframes;
                if (wasEntry) {
                    if (scratch.size() > scratchBase) scratch.resize(scratchBase);
                    throw;
                }
            }
            LOAD();
        } catch (const JsTerminate&) {
            Frame& e = M.frames[entry];
            M.handlers.resize(e.handlerBase);
            M.sp = e.base - 2;
            M.nframes = entry;
            if (scratch.size() > scratchBase) scratch.resize(scratchBase);
            throw;
        }
    }
#undef LOAD
#undef SAFEPOINT
#undef SP
#undef TOP
#undef KEY
}

} // namespace okay::js
