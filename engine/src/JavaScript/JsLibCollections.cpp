// okay::js library: iteration, generators, Map, Set and Promise. Written from
// ECMA-262 7.4 (operations on iterator objects: GetIterator, IteratorStep,
// IteratorClose), 27.1 (iteration, %IteratorPrototype%), 23.1.5 (array iterator
// objects), 22.1.5 (string iterator objects), 24.1 (Map), 24.2 (Set), 27.5
// (generator objects) and 27.2 (Promise objects and the promise jobs of 27.2.2).
// Map and Set keep entries in insertion order with an index by SameValueZero;
// deleted entries stay as gaps while an iterator may still visit them.
#include "JsInternal.hpp"
#include <cmath>

namespace okay::js {

namespace {

Value ArrayIterNextFn(Runtime& rt, CallArgs& a);
Value MapIterNextFn(Runtime& rt, CallArgs& a);
Value StringIterNextFn(Runtime& rt, CallArgs& a);

// One step of a built-in iterator object; false when it is done.
bool BuiltinStep(Runtime& rt, IterObj* it, Value& out) {
    if (it->done) return false;
    switch (it->cls) {
        case Cls::ArrayIter: {
            std::uint64_t len;
            if (it->target.IsObj() && it->target.O()->cls == Cls::TypedArray) {   // 23.1.5.1 step 2.b: a detached view throws
                TypedArrayObj* t = static_cast<TypedArrayObj*>(it->target.O());
                if (t->OutOfBounds()) rt.ThrowType("the typed array's buffer is detached");
                len = t->Length();
            } else len = rt.LengthOf(it->target);
            if (it->pos >= len) {
                it->done = true;
                return false;
            }
            std::uint32_t i = it->pos++;
            if (it->kind == 1) out = Value::Num(i);
            else {
                Value v = rt.GetIndex(it->target, i);
                if (it->kind == 0) out = v;
                else {
                    rt.Keep(v);
                    ArrayObj* pair = rt.NewArray(2);
                    pair->elems.push_back(Value::Num(i));
                    pair->elems.push_back(v);
                    pair->length = 2;
                    out = Value::Obj(pair);
                }
            }
            return true;
        }
        case Cls::MapIter: case Cls::SetIter: {
            MapObj* m = static_cast<MapObj*>(it->target.O());
            while (it->pos < m->entries.size() && !m->entries[it->pos].live) ++it->pos;
            if (it->pos >= m->entries.size()) {
                it->done = true;
                if (m->iterators) --m->iterators;
                return false;
            }
            const MapObj::Entry& e = m->entries[it->pos++];
            if (it->kind == 0) out = it->cls == Cls::MapIter ? e.v : e.k;
            else if (it->kind == 1) out = e.k;
            else {
                ArrayObj* pair = rt.NewArray(2);
                pair->elems.push_back(e.k);
                pair->elems.push_back(it->cls == Cls::MapIter ? e.v : e.k);
                pair->length = 2;
                out = Value::Obj(pair);
            }
            return true;
        }
        case Cls::StringIter: {
            const std::u16string& s = it->target.S()->s;
            if (it->pos >= s.size()) {
                it->done = true;
                return false;
            }
            std::size_t i = it->pos;
            std::size_t n = (s[i] >= 0xD800 && s[i] <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) ? 2 : 1;
            out = Value::Str(rt.NewString(s.substr(i, n)));
            it->pos += (std::uint32_t)n;
            return true;
        }
        default: return false;
    }
}

IterObj* NewIter(Runtime& rt, Cls c, const Value& target, std::uint8_t kind) {
    IterObj* it = rt.Alloc<IterObj>();
    it->cls = c;
    it->target = target;
    it->kind = kind;
    it->proto = c == Cls::ArrayIter ? rt.arrayIterProto : c == Cls::MapIter ? rt.mapIterProto : c == Cls::SetIter ? rt.setIterProto : rt.stringIterProto;
    if (c == Cls::MapIter || c == Cls::SetIter) ++static_cast<MapObj*>(target.O())->iterators;
    return it;
}

IterObj* ThisIter(Runtime& rt, CallArgs& a, Cls c, const char* what) {
    if (!a.thisv.IsObj() || a.thisv.O()->cls != c) rt.ThrowType(std::string(what) + ".next called on the wrong kind of object");
    return static_cast<IterObj*>(a.thisv.O());
}
Value ArrayIterNextFn(Runtime& rt, CallArgs& a) {
    IterObj* it = ThisIter(rt, a, Cls::ArrayIter, "Array Iterator");
    Value v;
    bool more = BuiltinStep(rt, it, v);
    return MakeIterResult(rt, v, !more);
}
Value MapIterNextFn(Runtime& rt, CallArgs& a) {
    if (!a.thisv.IsObj() || (a.thisv.O()->cls != Cls::MapIter && a.thisv.O()->cls != Cls::SetIter))
        rt.ThrowType("Map / Set Iterator.next called on the wrong kind of object");
    Value v;
    bool more = BuiltinStep(rt, static_cast<IterObj*>(a.thisv.O()), v);
    return MakeIterResult(rt, v, !more);
}
Value StringIterNextFn(Runtime& rt, CallArgs& a) {
    IterObj* it = ThisIter(rt, a, Cls::StringIter, "String Iterator");
    Value v;
    bool more = BuiltinStep(rt, it, v);
    return MakeIterResult(rt, v, !more);
}

// ---- Map and Set
MapObj* ThisMap(Runtime& rt, CallArgs& a, Cls c) {
    if (!a.thisv.IsObj() || a.thisv.O()->cls != c) rt.ThrowType(c == Cls::Map ? "this is not a Map" : "this is not a Set");
    return static_cast<MapObj*>(a.thisv.O());
}
void MapPut(Runtime& rt, MapObj* m, const Value& k0, const Value& v) {
    Value k = MapObj::Norm(k0);
    auto it = m->index.find(k);
    if (it != m->index.end()) {
        m->entries[it->second].v = v;
        return;
    }
    m->entries.push_back(MapObj::Entry{k, v, true});
    m->index[k] = (std::uint32_t)m->entries.size() - 1;
    ++m->live;
    rt.CountBytes(sizeof(MapObj::Entry) + 48);
}
bool MapRemove(Runtime& rt, MapObj* m, const Value& k0) {
    Value k = MapObj::Norm(k0);
    auto it = m->index.find(k);
    if (it == m->index.end()) return false;
    MapObj::Entry& e = m->entries[it->second];
    e.live = false;
    e.k = Value();
    e.v = Value();
    m->index.erase(it);
    --m->live;
    if (m->entries.size() > 32 && m->live * 4 < m->entries.size()) m->Compact();
    (void)rt;
    return true;
}

Value MapCtor(Runtime& rt, CallArgs& a, Cls c) {
    if (!a.newTarget) rt.ThrowType(c == Cls::Map ? "Map must be called with 'new'" : "Set must be called with 'new'");
    MapObj* m = rt.Alloc<MapObj>();
    m->cls = c;
    Value pr = rt.Get(Value::Obj(a.newTarget), rt.atoms->prototype);
    m->proto = pr.IsObj() ? pr.O() : (c == Cls::Map ? rt.mapProto : rt.setProto);
    Value src = a[0];
    if (src.IsNullish()) return Value::Obj(m);
    IterRec* r = GetIterator(rt, src);
    Value e;
    while (true) {
        Runtime::ScratchScope ss(rt);
        if (!IterStep(rt, r, e)) break;
        rt.Charge(1);
        if (c == Cls::Set) MapPut(rt, m, e, e);
        else {
            if (!e.IsObj()) {
                IterClose(rt, r);
                rt.ThrowType("a Map is made from [key, value] pairs");
            }
            rt.Keep(e);
            Value k = rt.GetIndex(e, 0);
            rt.Keep(k);
            Value v = rt.GetIndex(e, 1);
            MapPut(rt, m, k, v);
        }
    }
    return Value::Obj(m);
}

void ForEachEntry(Runtime& rt, MapObj* m, CallArgs& a, bool isMap) {
    Value fn = a[0];
    if (!rt.IsCallable(fn)) rt.ThrowType("forEach needs a function");
    ++m->iterators;
    try {
        for (std::size_t i = 0; i < m->entries.size(); ++i) {
            if (!m->entries[i].live) continue;
            Runtime::ScratchScope ss(rt);
            Value args[3] = {isMap ? m->entries[i].v : m->entries[i].k, m->entries[i].k, a.thisv};
            rt.Call(fn, a[1], args, 3);
        }
    } catch (...) {
        --m->iterators;
        throw;
    }
    --m->iterators;
}

// ---- generators
GenObj* ThisGen(Runtime& rt, CallArgs& a) {
    if (!a.thisv.IsObj() || a.thisv.O()->cls != Cls::Generator || static_cast<GenObj*>(a.thisv.O())->async)
        rt.ThrowType("this is not a generator");
    return static_cast<GenObj*>(a.thisv.O());
}

// ---- promises
Value ResolveFn(Runtime& rt, CallArgs& a);
Value RejectFn(Runtime& rt, CallArgs& a);

void MakeResolvers(Runtime& rt, PromiseObj* p, Value& res, Value& rej) {
    BoxObj* done = rt.Alloc<BoxObj>();
    done->proto = nullptr;
    done->prim = Value::Bool(false);
    NativeFunc* r1 = rt.NewNative(ResolveFn, "", 1, {Value::Obj(p), Value::Obj(done)});
    NativeFunc* r2 = rt.NewNative(RejectFn, "", 1, {Value::Obj(p), Value::Obj(done)});
    res = Value::Obj(r1);
    rej = Value::Obj(r2);
}

Value ResolveFn(Runtime& rt, CallArgs& a) {
    BoxObj* done = static_cast<BoxObj*>(a.callee->slots[1].O());
    if (done->prim.b) return Value();
    done->prim = Value::Bool(true);
    ResolvePromise(rt, static_cast<PromiseObj*>(a.callee->slots[0].O()), a[0]);
    return Value();
}
Value RejectFn(Runtime& rt, CallArgs& a) {
    BoxObj* done = static_cast<BoxObj*>(a.callee->slots[1].O());
    if (done->prim.b) return Value();
    done->prim = Value::Bool(true);
    RejectPromise(rt, static_cast<PromiseObj*>(a.callee->slots[0].O()), a[0]);
    return Value();
}

void Trigger(Runtime& rt, PromiseObj* p) {
    std::vector<Reaction> list = p->state == 1 ? p->onFulfill : p->onReject;
    p->onFulfill.clear();
    p->onReject.clear();
    for (const Reaction& r : list) {
        Job j;
        if (r.awaiter) {
            j.kind = 2;
            j.a = Value::Obj(r.awaiter);
        } else {
            j.kind = 0;
            j.a = r.handler;
            j.c = r.derived ? Value::Obj(r.derived) : Value();
        }
        j.b = p->result;
        j.flag = p->state == 2;
        rt.vm->jobs.push_back(j);
    }
}

void AddReaction(Runtime& rt, PromiseObj* p, const Value& onF, const Value& onR, PromiseObj* derived, GenObj* awaiter) {
    Reaction f{rt.IsCallable(onF) ? onF : Value(), derived, awaiter};
    Reaction r{rt.IsCallable(onR) ? onR : Value(), derived, awaiter};
    p->onFulfill.push_back(f);
    p->onReject.push_back(r);
    p->handled = true;
    if (p->state != 0) Trigger(rt, p);
}

PromiseObj* ThisPromise(Runtime& rt, CallArgs& a) {
    if (!a.thisv.IsObj() || a.thisv.O()->cls != Cls::Promise) rt.ThrowType("this is not a Promise");
    return static_cast<PromiseObj*>(a.thisv.O());
}

// Promise.all / allSettled / any: a shared record {values array, remaining count, the result promise}
struct Combinator {
    enum Kind { All, AllSettled, Any };
};
Value CombineStep(Runtime& rt, CallArgs& a) {
    // slots: 0 record object (values array in elems[0], remaining in elems[1], promise in elems[2]), 1 index, 2 kind, 3 isReject
    ArrayObj* rec = static_cast<ArrayObj*>(a.callee->slots[0].O());
    std::uint32_t i = (std::uint32_t)a.callee->slots[1].n;
    int kind = (int)a.callee->slots[2].n;
    bool isReject = a.callee->slots[3].b;
    BoxObj* done = static_cast<BoxObj*>(a.callee->slots[4].O());
    if (done->prim.b) return Value();
    done->prim = Value::Bool(true);
    ArrayObj* values = static_cast<ArrayObj*>(rec->elems[0].O());
    PromiseObj* p = static_cast<PromiseObj*>(rec->elems[2].O());
    Value v = a[0];
    if (kind == Combinator::AllSettled) {
        Object* o = rt.NewObject();
        rt.DefineData(o, rt.atoms->status, Value::Str(rt.Intern(isReject ? "rejected" : "fulfilled")));
        rt.DefineData(o, isReject ? rt.atoms->reason : rt.atoms->value, v);
        v = Value::Obj(o);
    }
    values->elems[i] = v;
    double& remaining = rec->elems[1].n;
    remaining -= 1;
    if (remaining == 0) {
        if (kind == Combinator::Any) {
            Object* err = rt.NewError(ErrorType::AggregateError, "all the promises were rejected");
            rt.DefineData(err, rt.atoms->errors, Value::Obj(values), kHidden);
            RejectPromise(rt, p, Value::Obj(err));
        } else ResolvePromise(rt, p, Value::Obj(values));
    }
    return Value();
}

Value Combine(Runtime& rt, CallArgs& a, int kind) {
    PromiseObj* p = NewPromise(rt);
    ArrayObj* rec = rt.NewArray(3);
    ArrayObj* values = rt.NewArray();
    rec->elems = {Value::Obj(values), Value::Num(1), Value::Obj(p)};
    rec->length = 3;
    try {
        IterRec* r = GetIterator(rt, a[0]);
        Value e;
        std::uint32_t i = 0;
        while (IterStep(rt, r, e)) {
            rt.Charge(1);
            rt.Keep(e);
            PromiseObj* item = PromiseResolveValue(rt, e);
            values->elems.push_back(Value());
            values->length = (std::uint32_t)values->elems.size();
            rec->elems[1].n += 1;
            BoxObj* done = rt.Alloc<BoxObj>();
            done->proto = nullptr;
            done->prim = Value::Bool(false);
            Value onF, onR;
            if (kind == Combinator::Any) {
                onF = Value::Obj(rt.NewNative(ResolveFn, "", 1, {Value::Obj(p), Value::Obj(done)}));
                onR = Value::Obj(rt.NewNative(CombineStep, "", 1, {Value::Obj(rec), Value::Num(i), Value::Num(kind), Value::Bool(true), Value::Obj(done)}));
            } else {
                onF = Value::Obj(rt.NewNative(CombineStep, "", 1, {Value::Obj(rec), Value::Num(i), Value::Num(kind), Value::Bool(false), Value::Obj(done)}));
                if (kind == Combinator::AllSettled) {
                    BoxObj* done2 = done;
                    onR = Value::Obj(rt.NewNative(CombineStep, "", 1, {Value::Obj(rec), Value::Num(i), Value::Num(kind), Value::Bool(true), Value::Obj(done2)}));
                } else onR = Value::Obj(rt.NewNative(RejectFn, "", 1, {Value::Obj(p), Value::Obj(done)}));
            }
            AddReaction(rt, item, onF, onR, NewPromise(rt), nullptr);
            ++i;
        }
        rec->elems[1].n -= 1;
        if (rec->elems[1].n == 0) {
            if (kind == Combinator::Any) {
                Object* err = rt.NewError(ErrorType::AggregateError, "all the promises were rejected");
                rt.DefineData(err, rt.atoms->errors, Value::Obj(values), kHidden);
                RejectPromise(rt, p, Value::Obj(err));
            } else ResolvePromise(rt, p, Value::Obj(values));
        }
    } catch (const JsThrow&) {
        Value e = rt.exception;
        rt.exception = Value();
        RejectPromise(rt, p, e);
    }
    return Value::Obj(p);
}

} // namespace

// ============================================================ iteration (7.4)
IterRec* GetIterator(Runtime& rt, const Value& v) {
    Runtime::Impl& M = *rt.vm;
    IterRec* r = rt.Alloc<IterRec>();
    r->proto = nullptr;
    if (v.IsObj() && v.O()->cls == Cls::Array && v.O()->proto == rt.arrayProto && !rt.arrayProto->lazy) {
        Prop* p = rt.arrayProto->FindRaw(rt.symIterator);
        Prop* n = rt.arrayIterProto->FindRaw(rt.atoms->next);
        if (p && p->v.IsObj() && p->v.O() == M.arrayValues && n && n->v.IsObj() && n->v.O() == M.arrayIterNext &&
            !v.O()->FindRaw(rt.symIterator)) {
            r->fast = 1;
            r->iter = v;
            return r;
        }
    }
    if (v.IsStr()) {
        Prop* p = rt.stringProto->FindRaw(rt.symIterator);
        Prop* n = rt.stringIterProto->FindRaw(rt.atoms->next);
        if (p && p->v.IsObj() && p->v.O() == M.stringIterator && n && n->v.IsObj() && n->v.O() == M.stringIterNext) {
            r->fast = 2;
            r->iter = v;
            return r;
        }
    }
    if (v.IsNullish()) rt.ThrowType(std::string(v.IsNull() ? "null" : "undefined") + " is not iterable");
    Value m = rt.Get(v, rt.symIterator);
    if (!rt.IsCallable(m)) rt.ThrowType("this value is not iterable (it has no [Symbol.iterator]() method)");
    Value it = rt.Call(m, v, nullptr, 0);
    if (!it.IsObj()) rt.ThrowType("[Symbol.iterator]() must return an object");
    r->iter = it;
    r->next = rt.Get(it, rt.atoms->next);
    // a built-in iterator with its own next: step it directly
    if (r->next.IsObj() && r->next.O()->cls == Cls::Native) {
        NativeFn fn = static_cast<NativeFunc*>(r->next.O())->fn;
        Cls c = it.O()->cls;
        if ((fn == ArrayIterNextFn && c == Cls::ArrayIter) || (fn == MapIterNextFn && (c == Cls::MapIter || c == Cls::SetIter)) ||
            (fn == StringIterNextFn && c == Cls::StringIter))
            r->fast = 3;
    }
    return r;
}

bool IterStep(Runtime& rt, IterRec* r, Value& out) {
    if (r->done) return false;
    rt.Charge(1);
    switch (r->fast) {
        case 1: {
            ArrayObj* a = static_cast<ArrayObj*>(r->iter.O());
            if (r->pos >= a->length) {
                r->done = true;
                return false;
            }
            out = rt.GetIndex(r->iter, r->pos++);
            return true;
        }
        case 2: {
            const std::u16string& s = r->iter.S()->s;
            if (r->pos >= s.size()) {
                r->done = true;
                return false;
            }
            std::size_t i = r->pos;
            std::size_t n = (s[i] >= 0xD800 && s[i] <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) ? 2 : 1;
            out = Value::Str(rt.NewString(s.substr(i, n)));
            r->pos += (std::uint32_t)n;
            return true;
        }
        case 3:
            if (!BuiltinStep(rt, static_cast<IterObj*>(r->iter.O()), out)) {
                r->done = true;
                return false;
            }
            return true;
        default: break;
    }
    if (!rt.IsCallable(r->next)) rt.ThrowType("the iterator's next is not a function");
    Value res = rt.Call(r->next, r->iter, nullptr, 0);
    if (!res.IsObj()) rt.ThrowType("an iterator's next() must return an object");
    if (rt.ToBoolean(rt.Get(res, rt.atoms->done))) {
        r->done = true;
        return false;
    }
    out = rt.Get(res, rt.atoms->value);
    rt.Keep(out);
    return true;
}

void IterClose(Runtime& rt, IterRec* r) {
    if (r->done) return;
    r->done = true;
    if (r->fast) return;
    Value m = rt.Get(r->iter, rt.atoms->ret);
    if (m.IsNullish()) return;
    if (!rt.IsCallable(m)) rt.ThrowType("the iterator's return is not a function");
    Value res = rt.Call(m, r->iter, nullptr, 0);
    if (!res.IsObj()) rt.ThrowType("an iterator's return() must return an object");
}

// ============================================================ promises (27.2.1)
PromiseObj* NewPromise(Runtime& rt) {
    PromiseObj* p = rt.Alloc<PromiseObj>();
    p->proto = rt.promiseProto;
    return p;
}

void ResolvePromise(Runtime& rt, PromiseObj* p, const Value& v) {
    if (p->state != 0) return;
    if (v.IsObj() && v.O() == p) {
        RejectPromise(rt, p, Value::Obj(rt.NewError(ErrorType::TypeError, "a promise can not be resolved with itself")));
        return;
    }
    if (v.IsObj()) {
        Value then;
        try {
            then = rt.Get(v, rt.atoms->then);
        } catch (const JsThrow&) {
            Value e = rt.exception;
            rt.exception = Value();
            RejectPromise(rt, p, e);
            return;
        }
        if (rt.IsCallable(then)) {
            Job j;
            j.kind = 1;
            j.a = Value::Obj(p);
            j.b = v;
            j.c = then;
            rt.vm->jobs.push_back(j);
            return;
        }
    }
    p->state = 1;
    p->result = v;
    Trigger(rt, p);
}

void RejectPromise(Runtime& rt, PromiseObj* p, const Value& v) {
    if (p->state != 0) return;
    p->state = 2;
    p->result = v;
    Trigger(rt, p);
}

PromiseObj* PromiseResolveValue(Runtime& rt, const Value& v) {
    if (v.IsObj() && v.O()->cls == Cls::Promise) return static_cast<PromiseObj*>(v.O());
    PromiseObj* p = NewPromise(rt);
    ResolvePromise(rt, p, v);
    return p;
}

void AwaitValue(Runtime& rt, GenObj* g, const Value& v) {
    PromiseObj* p = PromiseResolveValue(rt, v);
    AddReaction(rt, p, Value(), Value(), nullptr, g);
}

void RunJob(Runtime& rt, Job& j) {
    switch (j.kind) {
        case 0: {   // 27.2.2.1 NewPromiseReactionJob
            PromiseObj* derived = j.c.IsObj() ? static_cast<PromiseObj*>(j.c.O()) : nullptr;
            if (!rt.IsCallable(j.a)) {
                if (derived) {
                    if (j.flag) RejectPromise(rt, derived, j.b);
                    else ResolvePromise(rt, derived, j.b);
                }
                return;
            }
            try {
                Value r = rt.Call(j.a, Value(), &j.b, 1);
                if (derived) ResolvePromise(rt, derived, r);
            } catch (const JsThrow&) {
                Value e = rt.exception;
                rt.exception = Value();
                if (derived) RejectPromise(rt, derived, e);
            }
            return;
        }
        case 1: {   // 27.2.2.2 NewPromiseResolveThenableJob
            PromiseObj* p = static_cast<PromiseObj*>(j.a.O());
            Value res, rej;
            MakeResolvers(rt, p, res, rej);
            Value args[2] = {res, rej};
            try {
                rt.Call(j.c, j.b, args, 2);
            } catch (const JsThrow&) {
                Value e = rt.exception;
                rt.exception = Value();
                Value one[1] = {e};
                rt.Call(rej, Value(), one, 1);
            }
            return;
        }
        case 2: {   // an await resumes
            GenObj* g = static_cast<GenObj*>(j.a.O());
            try {
                ResumeGenerator(rt, g, j.flag ? 1 : 0, j.b);
            } catch (const JsThrow&) {
                Value e = rt.exception;
                rt.exception = Value();
                if (g->promise) RejectPromise(rt, g->promise, e);
            }
            return;
        }
        default: return;
    }
}

// ============================================================ install
void InstallCollectionsLib(Runtime& rt) {
    Runtime::Impl& M = *rt.vm;
    // %IteratorPrototype%
    rt.iteratorProto = rt.NewObject();
    NativeFunc* self = rt.NewNative([](Runtime&, CallArgs& a) { return a.thisv; }, "[Symbol.iterator]", 0);
    rt.iteratorProto->AddRaw(rt.symIterator, Value::Obj(self), kHidden);
    // array iterators
    rt.arrayIterProto = rt.NewObject(rt.iteratorProto);
    M.arrayIterNext = DefNative(rt, rt.arrayIterProto, "next", ArrayIterNextFn, 0);
    rt.arrayIterProto->AddRaw(rt.symToStringTag, Value::Str(rt.Intern("Array Iterator")), kConfigurable);
    M.arrayValues = DefNative(rt, rt.arrayProto, "values", [](Runtime& rt, CallArgs& a) {
        return Value::Obj(NewIter(rt, Cls::ArrayIter, Value::Obj(rt.ToObject(a.thisv)), 0));
    }, 0);
    rt.arrayProto->AddRaw(rt.symIterator, Value::Obj(M.arrayValues), kHidden);
    DefNative(rt, rt.arrayProto, "keys", [](Runtime& rt, CallArgs& a) {
        return Value::Obj(NewIter(rt, Cls::ArrayIter, Value::Obj(rt.ToObject(a.thisv)), 1));
    }, 0);
    DefNative(rt, rt.arrayProto, "entries", [](Runtime& rt, CallArgs& a) {
        return Value::Obj(NewIter(rt, Cls::ArrayIter, Value::Obj(rt.ToObject(a.thisv)), 2));
    }, 0);
    // string iterators
    rt.stringIterProto = rt.NewObject(rt.iteratorProto);
    M.stringIterNext = DefNative(rt, rt.stringIterProto, "next", StringIterNextFn, 0);
    M.stringIterator = rt.NewNative([](Runtime& rt, CallArgs& a) {
        if (a.thisv.IsNullish()) rt.ThrowType("String.prototype[Symbol.iterator] called on null or undefined");
        return Value::Obj(NewIter(rt, Cls::StringIter, Value::Str(rt.ToString(a.thisv)), 0));
    }, "[Symbol.iterator]", 0);
    rt.stringProto->AddRaw(rt.symIterator, Value::Obj(M.stringIterator), kHidden);

    // generators (27.5)
    rt.generatorProto = rt.NewObject(rt.iteratorProto);
    DefNative(rt, rt.generatorProto, "next", [](Runtime& rt, CallArgs& a) { return ResumeGenerator(rt, ThisGen(rt, a), 0, a[0]); }, 1);
    DefNative(rt, rt.generatorProto, "return", [](Runtime& rt, CallArgs& a) { return ResumeGenerator(rt, ThisGen(rt, a), 2, a[0]); }, 1);
    DefNative(rt, rt.generatorProto, "throw", [](Runtime& rt, CallArgs& a) { return ResumeGenerator(rt, ThisGen(rt, a), 1, a[0]); }, 1);
    rt.generatorProto->AddRaw(rt.symToStringTag, Value::Str(rt.Intern("Generator")), kConfigurable);

    // Map (24.1)
    rt.mapProto = rt.NewObject();
    NativeFunc* mapCtor = DefCtor(rt, "Map", [](Runtime& rt, CallArgs& a) { return MapCtor(rt, a, Cls::Map); }, 0, rt.mapProto);
    DefNative(rt, mapCtor, "groupBy", [](Runtime& rt, CallArgs& a) {   // 24.1.2.1 Map.groupBy
        if (!rt.IsCallable(a[1])) rt.ThrowType("Map.groupBy needs a function");
        MapObj* m = rt.Alloc<MapObj>();
        m->cls = Cls::Map;
        m->proto = rt.mapProto;
        IterRec* r = GetIterator(rt, a[0]);
        Value e;
        double k = 0;
        while (IterStep(rt, r, e)) {
            Runtime::ScratchScope ss(rt);
            rt.Keep(e);
            Value args[2] = {e, Value::Num(k++)};
            Value key = MapObj::Norm(rt.Call(a[1], Value(), args, 2));
            rt.Keep(key);
            auto it = m->index.find(key);
            ArrayObj* arr;
            if (it == m->index.end()) {
                arr = rt.NewArray();
                MapPut(rt, m, key, Value::Obj(arr));
            } else arr = static_cast<ArrayObj*>(m->entries[it->second].v.O());
            rt.DefineIndex(arr, arr->length, e);
        }
        return Value::Obj(m);
    }, 2);
    DefNative(rt, rt.mapProto, "get", [](Runtime& rt, CallArgs& a) {
        MapObj* m = ThisMap(rt, a, Cls::Map);
        auto it = m->index.find(MapObj::Norm(a[0]));
        return it == m->index.end() ? Value() : m->entries[it->second].v;
    }, 1);
    DefNative(rt, rt.mapProto, "set", [](Runtime& rt, CallArgs& a) {
        MapPut(rt, ThisMap(rt, a, Cls::Map), a[0], a[1]);
        return a.thisv;
    }, 2);
    DefNative(rt, rt.mapProto, "has", [](Runtime& rt, CallArgs& a) {
        MapObj* m = ThisMap(rt, a, Cls::Map);
        return Value::Bool(m->index.count(MapObj::Norm(a[0])) > 0);
    }, 1);
    DefNative(rt, rt.mapProto, "delete", [](Runtime& rt, CallArgs& a) { return Value::Bool(MapRemove(rt, ThisMap(rt, a, Cls::Map), a[0])); }, 1);
    DefNative(rt, rt.mapProto, "clear", [](Runtime& rt, CallArgs& a) {
        MapObj* m = ThisMap(rt, a, Cls::Map);
        for (auto& e : m->entries) e.live = false, e.k = Value(), e.v = Value();
        m->index.clear();
        m->live = 0;
        m->Compact();
        return Value();
    }, 0);
    DefGetter(rt, rt.mapProto, "size", [](Runtime& rt, CallArgs& a) { return Value::Num(ThisMap(rt, a, Cls::Map)->live); });
    DefNative(rt, rt.mapProto, "forEach", [](Runtime& rt, CallArgs& a) {
        ForEachEntry(rt, ThisMap(rt, a, Cls::Map), a, true);
        return Value();
    }, 1);
    DefNative(rt, rt.mapProto, "keys", [](Runtime& rt, CallArgs& a) { return Value::Obj(NewIter(rt, Cls::MapIter, Value::Obj(ThisMap(rt, a, Cls::Map)), 1)); }, 0);
    DefNative(rt, rt.mapProto, "values", [](Runtime& rt, CallArgs& a) { return Value::Obj(NewIter(rt, Cls::MapIter, Value::Obj(ThisMap(rt, a, Cls::Map)), 0)); }, 0);
    NativeFunc* mapEntries = DefNative(rt, rt.mapProto, "entries", [](Runtime& rt, CallArgs& a) {
        return Value::Obj(NewIter(rt, Cls::MapIter, Value::Obj(ThisMap(rt, a, Cls::Map)), 2));
    }, 0);
    rt.mapProto->AddRaw(rt.symIterator, Value::Obj(mapEntries), kHidden);
    rt.mapProto->AddRaw(rt.symToStringTag, Value::Str(rt.Intern("Map")), kConfigurable);
    rt.mapIterProto = rt.NewObject(rt.iteratorProto);
    DefNative(rt, rt.mapIterProto, "next", MapIterNextFn, 0);
    rt.mapIterProto->AddRaw(rt.symToStringTag, Value::Str(rt.Intern("Map Iterator")), kConfigurable);

    // Set (24.2)
    rt.setProto = rt.NewObject();
    DefCtor(rt, "Set", [](Runtime& rt, CallArgs& a) { return MapCtor(rt, a, Cls::Set); }, 0, rt.setProto);
    DefNative(rt, rt.setProto, "add", [](Runtime& rt, CallArgs& a) {
        MapPut(rt, ThisMap(rt, a, Cls::Set), a[0], a[0]);
        return a.thisv;
    }, 1);
    DefNative(rt, rt.setProto, "has", [](Runtime& rt, CallArgs& a) {
        MapObj* m = ThisMap(rt, a, Cls::Set);
        return Value::Bool(m->index.count(MapObj::Norm(a[0])) > 0);
    }, 1);
    DefNative(rt, rt.setProto, "delete", [](Runtime& rt, CallArgs& a) { return Value::Bool(MapRemove(rt, ThisMap(rt, a, Cls::Set), a[0])); }, 1);
    DefNative(rt, rt.setProto, "clear", [](Runtime& rt, CallArgs& a) {
        MapObj* m = ThisMap(rt, a, Cls::Set);
        for (auto& e : m->entries) e.live = false, e.k = Value(), e.v = Value();
        m->index.clear();
        m->live = 0;
        m->Compact();
        return Value();
    }, 0);
    DefGetter(rt, rt.setProto, "size", [](Runtime& rt, CallArgs& a) { return Value::Num(ThisMap(rt, a, Cls::Set)->live); });
    DefNative(rt, rt.setProto, "forEach", [](Runtime& rt, CallArgs& a) {
        ForEachEntry(rt, ThisMap(rt, a, Cls::Set), a, false);
        return Value();
    }, 1);
    NativeFunc* setValues = DefNative(rt, rt.setProto, "values", [](Runtime& rt, CallArgs& a) {
        return Value::Obj(NewIter(rt, Cls::SetIter, Value::Obj(ThisMap(rt, a, Cls::Set)), 0));
    }, 0);
    rt.setProto->AddRaw(rt.Intern("keys"), Value::Obj(setValues), kHidden);
    DefNative(rt, rt.setProto, "entries", [](Runtime& rt, CallArgs& a) {
        return Value::Obj(NewIter(rt, Cls::SetIter, Value::Obj(ThisMap(rt, a, Cls::Set)), 2));
    }, 0);
    rt.setProto->AddRaw(rt.symIterator, Value::Obj(setValues), kHidden);
    rt.setProto->AddRaw(rt.symToStringTag, Value::Str(rt.Intern("Set")), kConfigurable);
    rt.setIterProto = rt.NewObject(rt.iteratorProto);
    DefNative(rt, rt.setIterProto, "next", MapIterNextFn, 0);
    rt.setIterProto->AddRaw(rt.symToStringTag, Value::Str(rt.Intern("Set Iterator")), kConfigurable);

    // Promise (27.2)
    rt.promiseProto = rt.NewObject();
    NativeFunc* pc = DefCtor(rt, "Promise", [](Runtime& rt, CallArgs& a) {
        if (!a.newTarget) rt.ThrowType("Promise must be called with 'new'");
        if (!rt.IsCallable(a[0])) rt.ThrowType("new Promise needs a function (resolve, reject) => ...");
        PromiseObj* p = NewPromise(rt);
        Value res, rej;
        MakeResolvers(rt, p, res, rej);
        Value args[2] = {res, rej};
        try {
            rt.Call(a[0], Value(), args, 2);
        } catch (const JsThrow&) {
            Value e = rt.exception;
            rt.exception = Value();
            Value one[1] = {e};
            rt.Call(rej, Value(), one, 1);
        }
        return Value::Obj(p);
    }, 1, rt.promiseProto);
    rt.promiseCtor = pc;
    DefNative(rt, rt.promiseProto, "then", [](Runtime& rt, CallArgs& a) {
        PromiseObj* p = ThisPromise(rt, a);
        PromiseObj* d = NewPromise(rt);
        AddReaction(rt, p, a[0], a[1], d, nullptr);
        return Value::Obj(d);
    }, 2);
    DefNative(rt, rt.promiseProto, "catch", [](Runtime& rt, CallArgs& a) {
        Value then = rt.Get(a.thisv, rt.atoms->then);
        Value args[2] = {Value(), a[0]};
        return rt.Call(then, a.thisv, args, 2);
    }, 1);
    DefNative(rt, rt.promiseProto, "finally", [](Runtime& rt, CallArgs& a) {
        PromiseObj* p = ThisPromise(rt, a);
        PromiseObj* d = NewPromise(rt);
        Value fn = a[0];
        if (!rt.IsCallable(fn)) {
            AddReaction(rt, p, Value(), Value(), d, nullptr);
            return Value::Obj(d);
        }
        NativeFunc* onF = rt.NewNative([](Runtime& rt, CallArgs& a) {
            rt.Call(a.callee->slots[0], Value(), nullptr, 0);
            return a[0];
        }, "", 1, {fn});
        NativeFunc* onR = rt.NewNative([](Runtime& rt, CallArgs& a) -> Value {
            rt.Call(a.callee->slots[0], Value(), nullptr, 0);
            rt.Throw(a[0]);
        }, "", 1, {fn});
        AddReaction(rt, p, Value::Obj(onF), Value::Obj(onR), d, nullptr);
        return Value::Obj(d);
    }, 1);
    rt.promiseProto->AddRaw(rt.symToStringTag, Value::Str(rt.Intern("Promise")), kConfigurable);
    DefNative(rt, pc, "resolve", [](Runtime& rt, CallArgs& a) { return Value::Obj(PromiseResolveValue(rt, a[0])); }, 1);
    DefNative(rt, pc, "reject", [](Runtime& rt, CallArgs& a) {
        PromiseObj* p = NewPromise(rt);
        RejectPromise(rt, p, a[0]);
        return Value::Obj(p);
    }, 1);
    DefNative(rt, pc, "all", [](Runtime& rt, CallArgs& a) { return Combine(rt, a, Combinator::All); }, 1);
    DefNative(rt, pc, "withResolvers", [](Runtime& rt, CallArgs&) {   // 27.2.4.9 Promise.withResolvers
        PromiseObj* p = NewPromise(rt);
        Value res, rej;
        MakeResolvers(rt, p, res, rej);
        Object* o = rt.NewObject();
        rt.DefineData(o, rt.Intern("promise"), Value::Obj(p));
        rt.DefineData(o, rt.Intern("resolve"), res);
        rt.DefineData(o, rt.Intern("reject"), rej);
        return Value::Obj(o);
    }, 0);
    DefNative(rt, pc, "allSettled", [](Runtime& rt, CallArgs& a) { return Combine(rt, a, Combinator::AllSettled); }, 1);
    DefNative(rt, pc, "any", [](Runtime& rt, CallArgs& a) { return Combine(rt, a, Combinator::Any); }, 1);
    DefNative(rt, pc, "race", [](Runtime& rt, CallArgs& a) {
        PromiseObj* p = NewPromise(rt);
        try {
            IterRec* r = GetIterator(rt, a[0]);
            Value e;
            Value res, rej;
            MakeResolvers(rt, p, res, rej);
            while (IterStep(rt, r, e)) {
                rt.Keep(e);
                AddReaction(rt, PromiseResolveValue(rt, e), res, rej, NewPromise(rt), nullptr);
            }
        } catch (const JsThrow&) {
            Value e = rt.exception;
            rt.exception = Value();
            RejectPromise(rt, p, e);
        }
        return Value::Obj(p);
    }, 1);
}

// ============================================================ WeakMap, WeakSet (24.3, 24.4), WeakRef, FinalizationRegistry (26.1, 26.2)
bool CanBeHeldWeakly(const Value& v) {   // CanBeHeldWeakly (9.13): objects and symbols that Symbol.for did not register
    return v.IsObj() || (v.IsSym() && !v.Y()->registered);
}

namespace {

WeakObj* ThisWeak(Runtime& rt, CallArgs& a, Cls c, const char* what) {
    if (!a.thisv.IsObj() || a.thisv.O()->cls != c) rt.ThrowType(std::string("this is not a ") + what);
    return static_cast<WeakObj*>(a.thisv.O());
}

WeakObj* NewWeak(Runtime& rt, CallArgs& a, Cls c, Object* dflt, const char* what) {
    if (!a.newTarget) rt.ThrowType(std::string(what) + " must be called with 'new'");
    WeakObj* w = rt.Alloc<WeakObj>();
    w->cls = c;
    Value pr = rt.Get(Value::Obj(a.newTarget), rt.atoms->prototype);
    w->proto = pr.IsObj() ? pr.O() : dflt;
    rt.weakObjs.push_back(w);
    return w;
}

// new WeakMap(iterable) / new WeakSet(iterable): add each entry through this.set / this.add (24.1.1.2 AddEntriesFromIterable)
void FillWeak(Runtime& rt, WeakObj* w, const Value& src, bool isMap) {
    if (src.IsNullish()) return;
    Value adder = rt.Get(Value::Obj(w), rt.Intern(isMap ? "set" : "add"));
    if (!rt.IsCallable(adder)) rt.ThrowType(isMap ? "WeakMap.prototype.set is not a function" : "WeakSet.prototype.add is not a function");
    rt.Keep(adder);
    IterRec* r = GetIterator(rt, src);
    Value e;
    while (true) {
        Runtime::ScratchScope ss(rt);
        if (!IterStep(rt, r, e)) break;
        rt.Charge(1);
        try {
            if (isMap) {
                if (!e.IsObj()) rt.ThrowType("a WeakMap is made from [key, value] pairs");
                rt.Keep(e);
                Value k = rt.GetIndex(e, 0);
                rt.Keep(k);
                Value v = rt.GetIndex(e, 1);
                Value args[2] = {k, v};
                rt.Call(adder, Value::Obj(w), args, 2);
            } else rt.Call(adder, Value::Obj(w), &e, 1);
        } catch (const JsThrow&) {
            Value exc = rt.exception;
            rt.Keep(exc);
            try { IterClose(rt, r); } catch (const JsThrow&) {}
            rt.Throw(exc);
        }
    }
}

} // namespace

void InstallWeakLib(Runtime& rt) {
    Object* wmp = rt.NewObject();
    NativeFunc* wm = DefCtor(rt, "WeakMap", [](Runtime& rt, CallArgs& a) {
        WeakObj* w = NewWeak(rt, a, Cls::WeakMap, a.callee->slots[0].O(), "WeakMap");
        FillWeak(rt, w, a[0], true);
        return Value::Obj(w);
    }, 0, wmp);
    wm->slots = {Value::Obj(wmp)};
    DefNative(rt, wmp, "get", [](Runtime& rt, CallArgs& a) {
        WeakObj* w = ThisWeak(rt, a, Cls::WeakMap, "WeakMap");
        if (!CanBeHeldWeakly(a[0])) return Value();
        auto it = w->entries.find(a[0].p);
        return it == w->entries.end() ? Value() : it->second;
    }, 1);
    DefNative(rt, wmp, "set", [](Runtime& rt, CallArgs& a) {
        WeakObj* w = ThisWeak(rt, a, Cls::WeakMap, "WeakMap");
        if (!CanBeHeldWeakly(a[0])) rt.ThrowType("a WeakMap key must be an object or a symbol that is not registered");
        auto it = w->entries.find(a[0].p);
        if (it == w->entries.end()) rt.CountBytes(48);
        w->entries[a[0].p] = a[1];
        return a.thisv;
    }, 2);
    DefNative(rt, wmp, "has", [](Runtime& rt, CallArgs& a) {
        WeakObj* w = ThisWeak(rt, a, Cls::WeakMap, "WeakMap");
        return Value::Bool(CanBeHeldWeakly(a[0]) && w->entries.count(a[0].p) > 0);
    }, 1);
    DefNative(rt, wmp, "delete", [](Runtime& rt, CallArgs& a) {
        WeakObj* w = ThisWeak(rt, a, Cls::WeakMap, "WeakMap");
        return Value::Bool(CanBeHeldWeakly(a[0]) && w->entries.erase(a[0].p) > 0);
    }, 1);
    wmp->AddRaw(rt.symToStringTag, Value::Str(rt.Intern("WeakMap")), kConfigurable);

    Object* wsp = rt.NewObject();
    DefCtor(rt, "WeakSet", [](Runtime& rt, CallArgs& a) {
        WeakObj* w = NewWeak(rt, a, Cls::WeakSet, a.callee->slots[0].O(), "WeakSet");
        FillWeak(rt, w, a[0], false);
        return Value::Obj(w);
    }, 0, wsp)->slots = {Value::Obj(wsp)};
    DefNative(rt, wsp, "add", [](Runtime& rt, CallArgs& a) {
        WeakObj* w = ThisWeak(rt, a, Cls::WeakSet, "WeakSet");
        if (!CanBeHeldWeakly(a[0])) rt.ThrowType("a WeakSet value must be an object or a symbol that is not registered");
        if (w->entries.emplace(a[0].p, Value()).second) rt.CountBytes(48);
        return a.thisv;
    }, 1);
    DefNative(rt, wsp, "has", [](Runtime& rt, CallArgs& a) {
        WeakObj* w = ThisWeak(rt, a, Cls::WeakSet, "WeakSet");
        return Value::Bool(CanBeHeldWeakly(a[0]) && w->entries.count(a[0].p) > 0);
    }, 1);
    DefNative(rt, wsp, "delete", [](Runtime& rt, CallArgs& a) {
        WeakObj* w = ThisWeak(rt, a, Cls::WeakSet, "WeakSet");
        return Value::Bool(CanBeHeldWeakly(a[0]) && w->entries.erase(a[0].p) > 0);
    }, 1);
    wsp->AddRaw(rt.symToStringTag, Value::Str(rt.Intern("WeakSet")), kConfigurable);

    Object* wrp = rt.NewObject();
    DefCtor(rt, "WeakRef", [](Runtime& rt, CallArgs& a) {
        if (a.newTarget && !CanBeHeldWeakly(a[0])) rt.ThrowType("WeakRef needs an object or a symbol that is not registered");
        WeakObj* w = NewWeak(rt, a, Cls::WeakRef, a.callee->slots[0].O(), "WeakRef");
        w->target = a[0].p;
        rt.Keep(a[0]);   // AddToKeptObjects: alive at least until this call returns
        return Value::Obj(w);
    }, 1, wrp)->slots = {Value::Obj(wrp)};
    DefNative(rt, wrp, "deref", [](Runtime& rt, CallArgs& a) {
        WeakObj* w = ThisWeak(rt, a, Cls::WeakRef, "WeakRef");
        if (!w->target) return Value();
        Value v = w->target->kind == Kind::Symbol ? Value::Sym(static_cast<Symbol*>(w->target)) : Value::Obj(static_cast<Object*>(w->target));
        rt.Keep(v);
        return v;
    }, 0);
    wrp->AddRaw(rt.symToStringTag, Value::Str(rt.Intern("WeakRef")), kConfigurable);

    Object* frp = rt.NewObject();
    DefCtor(rt, "FinalizationRegistry", [](Runtime& rt, CallArgs& a) {
        if (a.newTarget && !rt.IsCallable(a[0])) rt.ThrowType("FinalizationRegistry needs a cleanup function");
        WeakObj* w = NewWeak(rt, a, Cls::FinalizationRegistry, a.callee->slots[0].O(), "FinalizationRegistry");
        w->cleanup = a[0];
        return Value::Obj(w);
    }, 1, frp)->slots = {Value::Obj(frp)};
    DefNative(rt, frp, "register", [](Runtime& rt, CallArgs& a) {
        WeakObj* w = ThisWeak(rt, a, Cls::FinalizationRegistry, "FinalizationRegistry");
        if (!CanBeHeldWeakly(a[0])) rt.ThrowType("register needs an object or a symbol that is not registered");
        if (SameValue(a[0], a[1])) rt.ThrowType("the target and the held value must differ");
        if (!CanBeHeldWeakly(a[2]) && !a[2].IsUndef()) rt.ThrowType("an unregister token must be an object or a symbol");
        if (w->cells.size() >= (1u << 24)) rt.ThrowRange("too many registrations");
        w->cells.push_back({a[0].p, a[1], a[2].IsUndef() ? nullptr : a[2].p});
        rt.CountBytes(sizeof(WeakObj::Cell));
        return Value();
    }, 2);
    DefNative(rt, frp, "unregister", [](Runtime& rt, CallArgs& a) {
        WeakObj* w = ThisWeak(rt, a, Cls::FinalizationRegistry, "FinalizationRegistry");
        if (!CanBeHeldWeakly(a[0])) rt.ThrowType("unregister needs an object or a symbol that is not registered");
        std::size_t c = 0;
        bool removed = false;
        rt.Charge(w->cells.size() / 8 + 1);
        for (WeakObj::Cell& cell : w->cells) {
            if (cell.token == a[0].p) { removed = true; continue; }
            w->cells[c++] = cell;
        }
        w->cells.resize(c);
        return Value::Bool(removed);
    }, 1);
    frp->AddRaw(rt.symToStringTag, Value::Str(rt.Intern("FinalizationRegistry")), kConfigurable);
}

} // namespace okay::js
