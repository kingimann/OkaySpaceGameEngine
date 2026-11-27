// okay::js library: Object (ECMA-262 20.1), Function (20.2), Boolean (20.3),
// Symbol (20.4), Error and the native errors (20.5), Reflect (28.1) and the
// global object's functions (19.2: parseInt, parseFloat, isNaN, isFinite) plus
// console.log for game scripts. Written from those spec sections. There is no
// eval and no Function constructor: game scripts can not compile code from strings.
#include "JsInternal.hpp"
#include <unordered_set>
#include <cmath>
#include <cstdlib>

namespace okay::js {

namespace {

Value ObjectArg(Runtime& rt, const Value& v, const char* fn) {
    if (!v.IsObj()) rt.ThrowType(std::string(fn) + " needs an object");
    return v;
}

// 6.2.6.5 ToPropertyDescriptor
Runtime::Descriptor ToDescriptor(Runtime& rt, const Value& v) {
    if (!v.IsObj()) rt.ThrowType("a property descriptor must be an object");
    Runtime::Descriptor d;
    Object* o = v.O();
    auto has = [&](GcObj* k) { return rt.HasProperty(o, k); };
    if (has(rt.atoms->enumerable)) { d.hasEnumerable = true; d.enumerable = rt.ToBoolean(rt.Get(v, rt.atoms->enumerable)); }
    if (has(rt.atoms->configurable)) { d.hasConfigurable = true; d.configurable = rt.ToBoolean(rt.Get(v, rt.atoms->configurable)); }
    if (has(rt.atoms->value)) { d.hasValue = true; d.value = rt.Get(v, rt.atoms->value); rt.Keep(d.value); }
    if (has(rt.atoms->writable)) { d.hasWritable = true; d.writable = rt.ToBoolean(rt.Get(v, rt.atoms->writable)); }
    if (has(rt.atoms->get)) {
        Value g = rt.Get(v, rt.atoms->get);
        if (!g.IsUndef() && !rt.IsCallable(g)) rt.ThrowType("a getter must be a function");
        d.hasGet = true;
        d.get = g.IsObj() ? g.O() : nullptr;
    }
    if (has(rt.atoms->set)) {
        Value s = rt.Get(v, rt.atoms->set);
        if (!s.IsUndef() && !rt.IsCallable(s)) rt.ThrowType("a setter must be a function");
        d.hasSet = true;
        d.set = s.IsObj() ? s.O() : nullptr;
    }
    if ((d.hasGet || d.hasSet) && (d.hasValue || d.hasWritable)) rt.ThrowType("a property can not have both a value and a getter or setter");
    return d;
}

// 6.2.6.4 FromPropertyDescriptor
Value FromDescriptor(Runtime& rt, const Prop& p) {
    Object* o = rt.NewObject();
    if (p.flags & kAccessor) {
        rt.DefineData(o, rt.atoms->get, p.get ? Value::Obj(p.get) : Value());
        rt.DefineData(o, rt.atoms->set, p.set ? Value::Obj(p.set) : Value());
    } else {
        rt.DefineData(o, rt.atoms->value, p.v);
        rt.DefineData(o, rt.atoms->writable, Value::Bool(p.flags & kWritable));
    }
    rt.DefineData(o, rt.atoms->enumerable, Value::Bool(p.flags & kEnumerable));
    rt.DefineData(o, rt.atoms->configurable, Value::Bool(p.flags & kConfigurable));
    return Value::Obj(o);
}

void DefineOrThrow(Runtime& rt, Object* o, GcObj* k, const Runtime::Descriptor& d) {
    if (!rt.DefineOwnProperty(o, k, d)) rt.ThrowType("can not define the property '" + KeyText(rt, k) + "'");
}

// 7.3.15 SetIntegrityLevel / 7.3.16 TestIntegrityLevel
void SetIntegrity(Runtime& rt, Object* o, bool frozen) {
    if (o->cls == Cls::Proxy || o->cls == Cls::TypedArray) {   // the spec's steps, through the object's own internal methods
        if (!rt.PreventExtensions(o)) rt.ThrowType("the object refused to stop being extensible");
        std::vector<GcObj*> keys = rt.OwnKeys(o, true, true);
        for (GcObj* k : keys) {
            rt.Charge(1);
            Runtime::Descriptor d;
            d.hasConfigurable = true;
            if (frozen) {
                Prop cur;
                if (!rt.GetOwnProperty(o, k, cur)) continue;
                if (!(cur.flags & kAccessor)) d.hasWritable = true;
            }
            DefineOrThrow(rt, o, k, d);
        }
        return;
    }
    rt.PreventExtensions(o);
    if (o->cls == Cls::Array || o->cls == Cls::Arguments) {
        ArrayObj* a = static_cast<ArrayObj*>(o);
        a->sealedElems = true;
        if (frozen) {
            a->frozenElems = true;
            a->lengthWritable = false;
        }
    }
    if (o->lazy) rt.MakeLazyProps(o);
    for (Prop& p : o->props) {
        if (p.key->kind == Kind::Symbol && static_cast<Symbol*>(p.key)->priv) continue;   // #private elements are not properties
        p.flags = (std::uint8_t)(p.flags & ~kConfigurable);
        if (frozen && !(p.flags & kAccessor)) p.flags = (std::uint8_t)(p.flags & ~kWritable);
    }
}
bool TestIntegrity(Runtime& rt, Object* o, bool frozen) {
    if (o->cls == Cls::Proxy || o->cls == Cls::TypedArray) {
        if (rt.IsExtensible(o)) return false;
        for (GcObj* k : rt.OwnKeys(o, true, true)) {
            rt.Charge(1);
            Prop cur;
            if (!rt.GetOwnProperty(o, k, cur)) continue;
            if (cur.flags & kConfigurable) return false;
            if (frozen && !(cur.flags & kAccessor) && (cur.flags & kWritable)) return false;
        }
        return true;
    }
    if (o->extensible) return false;
    if (o->cls == Cls::Array || o->cls == Cls::Arguments) {
        ArrayObj* a = static_cast<ArrayObj*>(o);
        bool anyElem = false;
        for (const Value& v : a->elems) if (!v.IsEmpty()) anyElem = true;
        if (anyElem && !a->sealedElems) return false;
        if (frozen && anyElem && !a->frozenElems) return false;
        if (frozen && a->lengthWritable) return false;
    }
    if (o->lazy) rt.MakeLazyProps(o);
    for (const Prop& p : o->props) {
        if (p.key->kind == Kind::Symbol && static_cast<Symbol*>(p.key)->priv) continue;
        if (p.flags & kConfigurable) return false;
        if (frozen && !(p.flags & kAccessor) && (p.flags & kWritable)) return false;
    }
    return true;
}

Value EnumerableOwn(Runtime& rt, const Value& v, int kind) {   // 0 keys, 1 values, 2 entries (20.1.2.x, 7.3.23)
    Object* o = rt.ToObject(v);
    rt.Keep(o);
    std::vector<GcObj*> keys = rt.OwnKeys(o, true, false);
    ArrayObj* out = rt.NewArray(keys.size());
    for (GcObj* k : keys) {
        Prop p;
        if (!rt.GetOwnProperty(o, k, p) || !(p.flags & kEnumerable)) continue;
        rt.Charge(1);
        if (kind == 0) rt.DefineIndex(out, out->length, Value::Str(static_cast<String*>(k)));
        else {
            Value val = rt.Get(Value::Obj(o), k);
            if (kind == 1) rt.DefineIndex(out, out->length, val);
            else {
                rt.Keep(val);
                ArrayObj* pair = rt.NewArray(2);
                pair->elems = {Value::Str(static_cast<String*>(k)), val};
                pair->length = 2;
                rt.DefineIndex(out, out->length, Value::Obj(pair));
            }
        }
    }
    return Value::Obj(out);
}

const char* BuiltinTag(Object* o) {
    switch (o->cls) {
        case Cls::Array: return "Array";
        case Cls::Arguments: return "Arguments";
        case Cls::Closure: case Cls::Native: case Cls::Bound: return "Function";
        case Cls::Error: return "Error";
        case Cls::BoolBox: return "Boolean";
        case Cls::NumberBox: return "Number";
        case Cls::StringBox: return "String";
        case Cls::DateBox: return "Date";
        case Cls::BigIntBox: return "BigInt";
        case Cls::Proxy: return o->proxyFn ? "Function" : "Object";   // 20.1.3.6 (IsArray is asked first)
        default: return "Object";
    }
}

Value ErrorCtorImpl(Runtime& rt, CallArgs& a, ErrorType t) {
    Object* nt = a.newTarget ? a.newTarget : a.callee;
    Value pr = rt.Get(Value::Obj(nt), rt.atoms->prototype);
    int msgArg = t == ErrorType::AggregateError ? 1 : 0;
    std::string msg = a[msgArg].IsUndef() ? "" : rt.ToUtf8(a[msgArg]);
    Object* e = rt.NewError(t, msg);
    if (a[msgArg].IsUndef()) e->RemoveRaw(rt.atoms->message);
    if (pr.IsObj()) e->proto = pr.O();
    Value opts = a[msgArg + 1];
    if (opts.IsObj() && rt.HasProperty(opts.O(), rt.atoms->cause))
        rt.DefineData(e, rt.atoms->cause, rt.Get(opts, rt.atoms->cause), kHidden);
    if (t == ErrorType::AggregateError) {
        ArrayObj* list = rt.NewArray();
        IterRec* r = GetIterator(rt, a[0]);
        Value v;
        while (IterStep(rt, r, v)) rt.DefineIndex(list, list->length, v);
        rt.DefineData(e, rt.atoms->errors, Value::Obj(list), kHidden);
    }
    return Value::Obj(e);
}

template <ErrorType T>
Value ErrorCtor(Runtime& rt, CallArgs& a) { return ErrorCtorImpl(rt, a, T); }

std::string Joined(Runtime& rt, CallArgs& a) {
    std::string s;
    for (int i = 0; i < a.argc; ++i) {
        if (i) s += ' ';
        s += rt.Display(a[i]);
    }
    return s;
}

Value ParseIntImpl(Runtime& rt, const Value& sv, const Value& rv) {
    std::u16string s = rt.ToU16(sv);
    std::size_t i = 0;
    while (i < s.size() && IsJsSpace(s[i])) ++i;
    bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) neg = s[i++] == '-';
    std::int32_t r = rt.ToInt32(rv);
    bool strip = true;
    if (r != 0) {
        if (r < 2 || r > 36) return Value::Num(NAN);
        if (r != 16) strip = false;
    } else r = 10;
    if (strip && i + 1 < s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        i += 2;
        r = 16;
    }
    double v = 0;
    bool any = false;
    std::string decimal;
    for (; i < s.size(); ++i) {
        char16_t c = s[i];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'z' ? c - 'a' + 10 : c >= 'A' && c <= 'Z' ? c - 'A' + 10 : 99;
        if (d >= r) break;
        any = true;
        if (r == 10) decimal += (char)c;
        else v = v * r + d;
    }
    if (!any) return Value::Num(NAN);
    if (r == 10) v = std::strtod(decimal.c_str(), nullptr);
    return Value::Num(neg ? -v : v);
}

} // namespace

void InstallObjectLib(Runtime& rt) {
    Object* op = rt.objectProto;
    NativeFunc* ctor = rt.NewNative([](Runtime& rt, CallArgs& a) {
        if (a.newTarget && a.newTarget != a.callee) {
            Value pr = rt.Get(Value::Obj(a.newTarget), rt.atoms->prototype);
            return Value::Obj(rt.NewObject(pr.IsObj() ? pr.O() : rt.objectProto));
        }
        if (a[0].IsNullish()) return Value::Obj(rt.NewObject());
        return Value::Obj(rt.ToObject(a[0]));
    }, "Object", 1);
    ctor->ctor = true;
    rt.objectCtor = ctor;
    rt.MakeLazyProps(ctor);
    ctor->AddRaw(rt.atoms->prototype, Value::Obj(op), 0);
    op->AddRaw(rt.atoms->constructor, Value::Obj(ctor), kHidden);
    rt.global->AddRaw(rt.Intern("Object"), Value::Obj(ctor), kHidden);

    DefNative(rt, op, "hasOwnProperty", [](Runtime& rt, CallArgs& a) {
        GcObj* k = rt.ToPropertyKey(a[0]);
        return Value::Bool(rt.HasOwn(rt.ToObject(a.thisv), k));
    }, 1);
    DefNative(rt, op, "isPrototypeOf", [](Runtime& rt, CallArgs& a) {
        if (!a[0].IsObj()) return Value::Bool(false);
        Object* self = rt.ToObject(a.thisv);
        int guard = 0;
        for (Object* p = rt.GetPrototypeOf(a[0].O()); p; p = rt.GetPrototypeOf(p)) {
            if (p == self) return Value::Bool(true);
            if (++guard > 100000) break;
        }
        return Value::Bool(false);
    }, 1);
    {   // B.2.2.1 Object.prototype.__proto__: an accessor over [[GetPrototypeOf]] / [[SetPrototypeOf]]
        NativeFunc* g = rt.NewNative([](Runtime& rt, CallArgs& a) {
            Object* o = rt.GetPrototypeOf(rt.ToObject(a.thisv));
            return o ? Value::Obj(o) : Value::Null();
        }, "get __proto__", 0);
        NativeFunc* s = rt.NewNative([](Runtime& rt, CallArgs& a) {
            if (a.thisv.IsNullish()) rt.ThrowType("can not set __proto__ of " + std::string(a.thisv.IsNull() ? "null" : "undefined"));
            if ((!a[0].IsObj() && !a[0].IsNull()) || !a.thisv.IsObj()) return Value();
            if (!rt.SetPrototype(a.thisv.O(), a[0].IsNull() ? nullptr : a[0].O()))
                rt.ThrowType("can not set this prototype (not extensible, or a cycle)");
            return Value();
        }, "set __proto__", 1);
        rt.DefineAccessor(op, rt.Intern("__proto__"), g, s, kConfigurable);
    }
    DefNative(rt, op, "propertyIsEnumerable", [](Runtime& rt, CallArgs& a) {
        GcObj* k = rt.ToPropertyKey(a[0]);
        Prop p;
        return Value::Bool(rt.GetOwnProperty(rt.ToObject(a.thisv), k, p) && (p.flags & kEnumerable));
    }, 1);
    DefNative(rt, op, "toString", [](Runtime& rt, CallArgs& a) {   // 20.1.3.6
        if (a.thisv.IsUndef()) return Value::Str(rt.Intern("[object Undefined]"));
        if (a.thisv.IsNull()) return Value::Str(rt.Intern("[object Null]"));
        Object* o = rt.ToObject(a.thisv);
        std::string tag = rt.IsArray(Value::Obj(o)) ? "Array" : BuiltinTag(o);
        Value t = rt.Get(Value::Obj(o), rt.symToStringTag);
        if (t.IsStr()) tag = Utf16To8(t.S()->s);
        return Value::Str(rt.NewStringUtf8("[object " + tag + "]"));
    }, 0);
    DefNative(rt, op, "toLocaleString", [](Runtime& rt, CallArgs& a) {
        Value f = rt.Get(a.thisv, rt.atoms->toString);
        return rt.Call(f, a.thisv, nullptr, 0);
    }, 0);
    DefNative(rt, op, "valueOf", [](Runtime& rt, CallArgs& a) { return Value::Obj(rt.ToObject(a.thisv)); }, 0);

    DefNative(rt, ctor, "keys", [](Runtime& rt, CallArgs& a) { return EnumerableOwn(rt, a[0], 0); }, 1);
    DefNative(rt, ctor, "values", [](Runtime& rt, CallArgs& a) { return EnumerableOwn(rt, a[0], 1); }, 1);
    DefNative(rt, ctor, "entries", [](Runtime& rt, CallArgs& a) { return EnumerableOwn(rt, a[0], 2); }, 1);
    DefNative(rt, ctor, "assign", [](Runtime& rt, CallArgs& a) {
        Object* to = rt.ToObject(a[0]);
        rt.Keep(to);
        for (int i = 1; i < a.argc; ++i) {
            if (a[i].IsNullish()) continue;
            Object* from = rt.ToObject(a[i]);
            rt.Keep(from);
            for (GcObj* k : rt.OwnKeys(from, true, true)) {
                Prop p;
                if (!rt.GetOwnProperty(from, k, p) || !(p.flags & kEnumerable)) continue;
                rt.Charge(1);
                rt.Put(Value::Obj(to), k, rt.Get(Value::Obj(from), k));
            }
        }
        return Value::Obj(to);
    }, 2);
    DefNative(rt, ctor, "create", [](Runtime& rt, CallArgs& a) {
        if (!a[0].IsObj() && !a[0].IsNull()) rt.ThrowType("Object.create needs an object or null as the prototype");
        Object* o = rt.NewObject(a[0].IsNull() ? nullptr : a[0].O());
        if (!a[1].IsUndef()) {
            Object* props = rt.ToObject(a[1]);
            for (GcObj* k : rt.OwnKeys(props, true, true)) {
                Prop p;
                if (!rt.GetOwnProperty(props, k, p) || !(p.flags & kEnumerable)) continue;
                DefineOrThrow(rt, o, k, ToDescriptor(rt, rt.Get(Value::Obj(props), k)));
            }
        }
        return Value::Obj(o);
    }, 2);
    DefNative(rt, ctor, "defineProperty", [](Runtime& rt, CallArgs& a) {
        Value o = ObjectArg(rt, a[0], "Object.defineProperty");
        GcObj* k = rt.ToPropertyKey(a[1]);
        DefineOrThrow(rt, o.O(), k, ToDescriptor(rt, a[2]));
        return o;
    }, 3);
    DefNative(rt, ctor, "defineProperties", [](Runtime& rt, CallArgs& a) {
        Value o = ObjectArg(rt, a[0], "Object.defineProperties");
        Object* props = rt.ToObject(a[1]);
        std::vector<std::pair<GcObj*, Runtime::Descriptor>> ds;
        for (GcObj* k : rt.OwnKeys(props, true, true)) {
            Prop p;
            if (!rt.GetOwnProperty(props, k, p) || !(p.flags & kEnumerable)) continue;
            ds.push_back({k, ToDescriptor(rt, rt.Get(Value::Obj(props), k))});
        }
        Runtime::RootedVector keep(rt);
        for (auto& d : ds) keep.v.push_back(d.second.value);
        for (auto& d : ds) DefineOrThrow(rt, o.O(), d.first, d.second);
        return o;
    }, 2);
    DefNative(rt, ctor, "getOwnPropertyNames", [](Runtime& rt, CallArgs& a) {
        Object* o = rt.ToObject(a[0]);
        ArrayObj* out = rt.NewArray();
        for (GcObj* k : rt.OwnKeys(o, true, false)) rt.DefineIndex(out, out->length, Value::Str(static_cast<String*>(k)));
        return Value::Obj(out);
    }, 1);
    DefNative(rt, ctor, "getOwnPropertySymbols", [](Runtime& rt, CallArgs& a) {
        Object* o = rt.ToObject(a[0]);
        ArrayObj* out = rt.NewArray();
        for (GcObj* k : rt.OwnKeys(o, false, true)) rt.DefineIndex(out, out->length, Value::Sym(static_cast<Symbol*>(k)));
        return Value::Obj(out);
    }, 1);
    DefNative(rt, ctor, "getOwnPropertyDescriptor", [](Runtime& rt, CallArgs& a) {
        Object* o = rt.ToObject(a[0]);
        GcObj* k = rt.ToPropertyKey(a[1]);
        Prop p;
        if (!rt.GetOwnProperty(o, k, p)) return Value();
        return FromDescriptor(rt, p);
    }, 2);
    DefNative(rt, ctor, "getOwnPropertyDescriptors", [](Runtime& rt, CallArgs& a) {
        Object* o = rt.ToObject(a[0]);
        Object* out = rt.NewObject();
        for (GcObj* k : rt.OwnKeys(o, true, true)) {
            Prop p;
            if (rt.GetOwnProperty(o, k, p)) rt.DefineData(out, k, FromDescriptor(rt, p));
        }
        return Value::Obj(out);
    }, 1);
    DefNative(rt, ctor, "getPrototypeOf", [](Runtime& rt, CallArgs& a) {
        Object* o = rt.GetPrototypeOf(rt.ToObject(a[0]));
        return o ? Value::Obj(o) : Value::Null();
    }, 1);
    DefNative(rt, ctor, "setPrototypeOf", [](Runtime& rt, CallArgs& a) {
        if (a[0].IsNullish()) rt.ThrowType("Object.setPrototypeOf needs an object");
        if (!a[1].IsObj() && !a[1].IsNull()) rt.ThrowType("a prototype must be an object or null");
        if (!a[0].IsObj()) return a[0];
        if (!rt.SetPrototype(a[0].O(), a[1].IsNull() ? nullptr : a[1].O())) rt.ThrowType("can not set this prototype (not extensible, or a cycle)");
        return a[0];
    }, 2);
    DefNative(rt, ctor, "freeze", [](Runtime& rt, CallArgs& a) { if (a[0].IsObj()) SetIntegrity(rt, a[0].O(), true); return a[0]; }, 1);
    DefNative(rt, ctor, "seal", [](Runtime& rt, CallArgs& a) { if (a[0].IsObj()) SetIntegrity(rt, a[0].O(), false); return a[0]; }, 1);
    DefNative(rt, ctor, "preventExtensions", [](Runtime& rt, CallArgs& a) { if (a[0].IsObj()) rt.PreventExtensions(a[0].O()); return a[0]; }, 1);
    DefNative(rt, ctor, "isFrozen", [](Runtime& rt, CallArgs& a) { return Value::Bool(!a[0].IsObj() || TestIntegrity(rt, a[0].O(), true)); }, 1);
    DefNative(rt, ctor, "isSealed", [](Runtime& rt, CallArgs& a) { return Value::Bool(!a[0].IsObj() || TestIntegrity(rt, a[0].O(), false)); }, 1);
    DefNative(rt, ctor, "isExtensible", [](Runtime& rt, CallArgs& a) { return Value::Bool(a[0].IsObj() && rt.IsExtensible(a[0].O())); }, 1);
    DefNative(rt, ctor, "is", [](Runtime&, CallArgs& a) { return Value::Bool(SameValue(a[0], a[1])); }, 2);
    DefNative(rt, ctor, "hasOwn", [](Runtime& rt, CallArgs& a) {
        Object* o = rt.ToObject(a[0]);
        return Value::Bool(rt.HasOwn(o, rt.ToPropertyKey(a[1])));
    }, 2);
    DefNative(rt, ctor, "fromEntries", [](Runtime& rt, CallArgs& a) {
        Object* o = rt.NewObject();
        IterRec* r = GetIterator(rt, a[0]);
        Value e;
        while (IterStep(rt, r, e)) {
            Runtime::ScratchScope ss(rt);
            if (!e.IsObj()) rt.ThrowType("Object.fromEntries needs [key, value] pairs");
            rt.Keep(e);
            Value k = rt.GetIndex(e, 0);
            rt.Keep(k);
            Value v = rt.GetIndex(e, 1);
            rt.DefineData(o, rt.ToPropertyKey(k), v);
        }
        return Value::Obj(o);
    }, 1);
    DefNative(rt, ctor, "groupBy", [](Runtime& rt, CallArgs& a) {   // 20.1.2.12 Object.groupBy (7.3.35 GroupBy)
        if (!rt.IsCallable(a[1])) rt.ThrowType("Object.groupBy needs a function");
        Object* o = rt.NewObject(nullptr);
        IterRec* r = GetIterator(rt, a[0]);
        Value e;
        double k = 0;
        while (IterStep(rt, r, e)) {
            Runtime::ScratchScope ss(rt);
            rt.Keep(e);
            Value args[2] = {e, Value::Num(k++)};
            GcObj* key = rt.ToPropertyKey(rt.Call(a[1], Value(), args, 2));
            rt.Keep(key);
            Value g = rt.Get(Value::Obj(o), key);
            if (!g.IsObj()) {
                g = Value::Obj(rt.NewArray());
                rt.DefineData(o, key, g);
            }
            ArrayObj* arr = static_cast<ArrayObj*>(g.O());
            rt.DefineIndex(arr, arr->length, e);
        }
        return Value::Obj(o);
    }, 2);
}

void InstallFunctionLib(Runtime& rt) {
    Object* fp = rt.functionProto;
    NativeFunc* ctor = rt.NewNative([](Runtime& rt, CallArgs&) -> Value {
        rt.ThrowError(ErrorType::EvalError, "compiling code from a string (Function / eval) is not allowed in game scripts");
    }, "Function", 1);
    ctor->ctor = true;
    rt.MakeLazyProps(ctor);
    ctor->AddRaw(rt.atoms->prototype, Value::Obj(fp), 0);
    fp->AddRaw(rt.atoms->constructor, Value::Obj(ctor), kHidden);
    rt.global->AddRaw(rt.Intern("Function"), Value::Obj(ctor), kHidden);
    rt.MakeLazyProps(fp);

    DefNative(rt, fp, "call", [](Runtime& rt, CallArgs& a) {
        if (!rt.IsCallable(a.thisv)) rt.ThrowType("Function.prototype.call needs a function");
        return rt.Call(a.thisv, a[0], a.argc > 1 ? a.argv + 1 : nullptr, a.argc > 1 ? a.argc - 1 : 0);
    }, 1);
    DefNative(rt, fp, "apply", [](Runtime& rt, CallArgs& a) {   // 20.2.3.1 with 7.3.19 CreateListFromArrayLike
        if (!rt.IsCallable(a.thisv)) rt.ThrowType("Function.prototype.apply needs a function");
        if (a[1].IsNullish()) return rt.Call(a.thisv, a[0], nullptr, 0);
        if (!a[1].IsObj()) rt.ThrowType("apply's second argument must be an array-like object");
        std::uint64_t n = rt.LengthOf(a[1]);
        if (n > 100000) rt.ThrowRange("too many arguments for apply");
        Runtime::RootedVector args(rt);
        for (std::uint64_t i = 0; i < n; ++i) args.v.push_back(rt.GetIndex(a[1], (std::uint32_t)i));
        rt.Charge(n);
        return rt.Call(a.thisv, a[0], args.v.data(), (int)args.v.size());
    }, 2);
    DefNative(rt, fp, "bind", [](Runtime& rt, CallArgs& a) {
        if (!rt.IsCallable(a.thisv)) rt.ThrowType("Function.prototype.bind needs a function");
        BoundFunc* b = rt.Alloc<BoundFunc>();
        b->proto = a.thisv.O()->proto;
        b->target = a.thisv.O();
        b->thisv = a[0];
        for (int i = 1; i < a.argc; ++i) b->args.push_back(a[i]);
        b->lazy = true;
        return Value::Obj(b);
    }, 1);
    DefNative(rt, fp, "toString", [](Runtime& rt, CallArgs& a) {
        if (!rt.IsCallable(a.thisv)) rt.ThrowType("Function.prototype.toString needs a function");
        std::string n = Utf16To8(NameOf(rt, a.thisv.O())->s);
        bool native = a.thisv.O()->cls != Cls::Closure;
        return Value::Str(rt.NewStringUtf8("function " + n + "() { " + (native ? "[native code]" : "[script code]") + " }"));
    }, 0);
    // %ThrowTypeError%
    rt.throwTypeErrorFn = rt.NewNative([](Runtime& rt, CallArgs&) -> Value { rt.ThrowType("this property is not available"); }, "", 0);
}

void InstallErrorLib(Runtime& rt) {
    static const char* const names[] = {"Error", "TypeError", "RangeError", "SyntaxError", "ReferenceError",
                                        "EvalError", "URIError", "AggregateError"};
    NativeFn fns[] = {ErrorCtor<ErrorType::Error>, ErrorCtor<ErrorType::TypeError>, ErrorCtor<ErrorType::RangeError>,
                      ErrorCtor<ErrorType::SyntaxError>, ErrorCtor<ErrorType::ReferenceError>, ErrorCtor<ErrorType::EvalError>,
                      ErrorCtor<ErrorType::URIError>, ErrorCtor<ErrorType::AggregateError>};
    NativeFunc* base = nullptr;
    for (int i = 0; i < 8; ++i) {
        Object* proto = rt.NewObject(i == 0 ? rt.objectProto : rt.errorProtos[0]);
        rt.errorProtos[i] = proto;
        NativeFunc* c = DefCtor(rt, names[i], fns[i], i == 7 ? 2 : 1, proto);
        if (i == 0) base = c;
        else c->proto = base;
        proto->AddRaw(rt.atoms->name, Value::Str(rt.Intern(names[i])), kHidden);
        proto->AddRaw(rt.atoms->message, Value::Str(rt.atoms->empty), kHidden);
    }
    rt.errorProto = rt.errorProtos[0];
    DefNative(rt, rt.errorProto, "toString", [](Runtime& rt, CallArgs& a) {   // 20.5.3.4
        if (!a.thisv.IsObj()) rt.ThrowType("Error.prototype.toString needs an object");
        Value n = rt.Get(a.thisv, rt.atoms->name), m = rt.Get(a.thisv, rt.atoms->message);
        std::u16string name = n.IsUndef() ? u"Error" : rt.ToU16(n);
        std::u16string msg = m.IsUndef() ? u"" : rt.ToU16(m);
        if (name.empty()) return Value::Str(rt.NewString(msg));
        if (msg.empty()) return Value::Str(rt.NewString(name));
        return Value::Str(rt.NewString(name + u": " + msg));
    }, 0);
}


namespace {
// ---- 19.2.6 URI handling: Encode (19.2.6.5) and Decode (19.2.6.6) with UTF-8 (RFC 3629), and B.2.1 escape / unescape
bool UriUnreserved(char16_t c) {   // uriAlpha, DecimalDigit and uriMark
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'-' || c == u'_' ||
           c == u'.' || c == u'!' || c == u'~' || c == u'*' || c == u'\'' || c == u'(' || c == u')';
}
bool UriReservedOrHash(char16_t c) {   // uriReserved and "#"
    return c == u';' || c == u'/' || c == u'?' || c == u':' || c == u'@' || c == u'&' || c == u'=' || c == u'+' ||
           c == u'$' || c == u',' || c == u'#';
}
const char kHex[] = "0123456789ABCDEF";
void PutEscaped(std::u16string& out, unsigned byte) {
    out += u'%';
    out += (char16_t)kHex[byte >> 4];
    out += (char16_t)kHex[byte & 15];
}
Value UriEncode(Runtime& rt, const Value& v, bool keepReserved) {
    const std::u16string s = rt.ToU16(v);
    rt.Charge(s.size() / 4 + 1);
    std::u16string out;
    out.reserve(s.size());
    for (std::size_t k = 0; k < s.size(); ++k) {
        char16_t c = s[k];
        if (UriUnreserved(c) || (keepReserved && UriReservedOrHash(c))) {
            out += c;
            continue;
        }
        std::uint32_t cp = c;
        if (c >= 0xDC00 && c <= 0xDFFF) rt.ThrowError(ErrorType::URIError, "URI malformed: a lone low surrogate");
        if (c >= 0xD800 && c <= 0xDBFF) {
            if (k + 1 >= s.size() || s[k + 1] < 0xDC00 || s[k + 1] > 0xDFFF)
                rt.ThrowError(ErrorType::URIError, "URI malformed: a lone high surrogate");
            cp = 0x10000 + ((cp - 0xD800) << 10) + (s[++k] - 0xDC00);
        }
        if (cp < 0x80) PutEscaped(out, cp);
        else if (cp < 0x800) { PutEscaped(out, 0xC0 | (cp >> 6)); PutEscaped(out, 0x80 | (cp & 63)); }
        else if (cp < 0x10000) {
            PutEscaped(out, 0xE0 | (cp >> 12));
            PutEscaped(out, 0x80 | ((cp >> 6) & 63));
            PutEscaped(out, 0x80 | (cp & 63));
        } else {
            PutEscaped(out, 0xF0 | (cp >> 18));
            PutEscaped(out, 0x80 | ((cp >> 12) & 63));
            PutEscaped(out, 0x80 | ((cp >> 6) & 63));
            PutEscaped(out, 0x80 | (cp & 63));
        }
        if (out.size() > (1u << 28)) rt.ThrowRange("the string is too long");
    }
    return Value::Str(rt.NewString(std::move(out)));
}
// "%XX" at s[k]: the byte, or -1.
int EscapedByte(const std::u16string& s, std::size_t k) {
    if (k + 2 >= s.size() || s[k] != u'%') return -1;
    int h = HexDigitValue(s[k + 1]), l = HexDigitValue(s[k + 2]);
    return h < 0 || l < 0 ? -1 : h * 16 + l;
}
Value UriDecode(Runtime& rt, const Value& v, bool keepReserved) {
    const std::u16string s = rt.ToU16(v);
    rt.Charge(s.size() / 4 + 1);
    std::u16string out;
    out.reserve(s.size());
    auto bad = [&rt]() { rt.ThrowError(ErrorType::URIError, "URI malformed"); };
    for (std::size_t k = 0; k < s.size(); ++k) {
        if (s[k] != u'%') {
            out += s[k];
            continue;
        }
        int b = EscapedByte(s, k);
        if (b < 0) bad();
        if (b < 0x80) {
            if (keepReserved && UriReservedOrHash((char16_t)b)) out.append(s, k, 3);   // decodeURI leaves these escaped
            else out += (char16_t)b;
            k += 2;
            continue;
        }
        int n = (b & 0xE0) == 0xC0 ? 2 : (b & 0xF0) == 0xE0 ? 3 : (b & 0xF8) == 0xF0 ? 4 : 0;
        if (!n) bad();
        std::uint32_t cp = (std::uint32_t)b & (n == 2 ? 0x1F : n == 3 ? 0x0F : 0x07);
        k += 2;
        for (int j = 1; j < n; ++j) {
            int c = EscapedByte(s, k + 1);
            if (c < 0 || (c & 0xC0) != 0x80) bad();
            cp = (cp << 6) | (std::uint32_t)(c & 0x3F);
            k += 3;
        }
        // RFC 3629: no overlong forms, no surrogates, nothing past U+10FFFF
        static const std::uint32_t minFor[5] = {0, 0, 0x80, 0x800, 0x10000};
        if (cp < minFor[n] || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) bad();
        if (cp < 0x10000) out += (char16_t)cp;
        else {
            cp -= 0x10000;
            out += (char16_t)(0xD800 + (cp >> 10));
            out += (char16_t)(0xDC00 + (cp & 0x3FF));
        }
    }
    return Value::Str(rt.NewString(std::move(out)));
}
} // namespace

void InstallGlobalLib(Runtime& rt) {
    Object* g = rt.global;
    DefValue(rt, g, "globalThis", Value::Obj(g));
    DefValue(rt, g, "undefined", Value(), 0);
    DefValue(rt, g, "NaN", Value::Num(NAN), 0);
    DefValue(rt, g, "Infinity", Value::Num(INFINITY), 0);
    DefNative(rt, g, "isNaN", [](Runtime& rt, CallArgs& a) { return Value::Bool(std::isnan(rt.ToNumber(a[0]))); }, 1);
    DefNative(rt, g, "isFinite", [](Runtime& rt, CallArgs& a) { return Value::Bool(std::isfinite(rt.ToNumber(a[0]))); }, 1);
    DefNative(rt, g, "parseFloat", [](Runtime& rt, CallArgs& a) {   // 19.2.4: the longest prefix that is a decimal literal
        std::u16string s = rt.ToU16(a[0]);
        std::size_t i = 0;
        while (i < s.size() && IsJsSpace(s[i])) ++i;
        std::string t;
        std::size_t j = i;
        if (j < s.size() && (s[j] == '+' || s[j] == '-')) t += (char)s[j++];
        static const char inf[] = "Infinity";
        bool isInf = true;
        for (int k = 0; k < 8; ++k)
            if (j + (std::size_t)k >= s.size() || s[j + (std::size_t)k] != (char16_t)inf[k]) { isInf = false; break; }
        if (isInf) return Value::Num(t == "-" ? -INFINITY : INFINITY);
        bool digits = false, dot = false;
        while (j < s.size()) {
            char16_t c = s[j];
            if (c >= '0' && c <= '9') { t += (char)c; digits = true; ++j; continue; }
            if (c == '.' && !dot) { t += '.'; dot = true; ++j; continue; }
            break;
        }
        if (!digits) return Value::Num(NAN);
        if (j < s.size() && (s[j] == 'e' || s[j] == 'E')) {
            std::string e = "e";
            std::size_t k = j + 1;
            if (k < s.size() && (s[k] == '+' || s[k] == '-')) e += (char)s[k++];
            bool ed = false;
            while (k < s.size() && s[k] >= '0' && s[k] <= '9') { e += (char)s[k++]; ed = true; }
            if (ed) t += e;
        }
        return Value::Num(std::strtod(t.c_str(), nullptr));
    }, 1);
    NativeFunc* pi = DefNative(rt, g, "parseInt", [](Runtime& rt, CallArgs& a) { return ParseIntImpl(rt, a[0], a[1]); }, 2);
    if (Prop* num = g->FindRaw(rt.Intern("Number"))) {   // 21.1.2.12 / 21.1.2.13: the same function objects as the globals
        DefValue(rt, num->v.O(), "parseInt", Value::Obj(pi));
        DefValue(rt, num->v.O(), "parseFloat", g->FindRaw(rt.Intern("parseFloat"))->v);
    }
    DefNative(rt, g, "encodeURIComponent", [](Runtime& rt, CallArgs& a) { return UriEncode(rt, a[0], false); }, 1);
    DefNative(rt, g, "encodeURI", [](Runtime& rt, CallArgs& a) { return UriEncode(rt, a[0], true); }, 1);
    DefNative(rt, g, "decodeURIComponent", [](Runtime& rt, CallArgs& a) { return UriDecode(rt, a[0], false); }, 1);
    DefNative(rt, g, "decodeURI", [](Runtime& rt, CallArgs& a) { return UriDecode(rt, a[0], true); }, 1);
    DefNative(rt, g, "escape", [](Runtime& rt, CallArgs& a) {   // B.2.1.1
        const std::u16string s = rt.ToU16(a[0]);
        rt.Charge(s.size() / 4 + 1);
        std::u16string out;
        for (char16_t c : s) {
            bool keep = (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'@' ||
                        c == u'*' || c == u'_' || c == u'+' || c == u'-' || c == u'.' || c == u'/';
            if (keep) out += c;
            else if (c < 256) PutEscaped(out, c);
            else {
                out += u"%u";
                for (int sh = 12; sh >= 0; sh -= 4) out += (char16_t)kHex[(c >> sh) & 15];
            }
        }
        return Value::Str(rt.NewString(std::move(out)));
    }, 1);
    DefNative(rt, g, "unescape", [](Runtime& rt, CallArgs& a) {   // B.2.1.2
        const std::u16string s = rt.ToU16(a[0]);
        rt.Charge(s.size() / 4 + 1);
        std::u16string out;
        for (std::size_t k = 0; k < s.size(); ++k) {
            char16_t c = s[k];
            if (c == u'%' && k + 5 < s.size() && s[k + 1] == u'u') {
                int h[4] = {HexDigitValue(s[k + 2]), HexDigitValue(s[k + 3]), HexDigitValue(s[k + 4]), HexDigitValue(s[k + 5])};
                if (h[0] >= 0 && h[1] >= 0 && h[2] >= 0 && h[3] >= 0) {
                    out += (char16_t)((h[0] << 12) | (h[1] << 8) | (h[2] << 4) | h[3]);
                    k += 5;
                    continue;
                }
            }
            int b = c == u'%' ? EscapedByte(s, k) : -1;
            if (b >= 0) {
                out += (char16_t)b;
                k += 2;
            } else out += c;
        }
        return Value::Str(rt.NewString(std::move(out)));
    }, 1);
    DefNative(rt, g, "queueMicrotask", [](Runtime& rt, CallArgs& a) {
        if (!rt.IsCallable(a[0])) rt.ThrowType("queueMicrotask needs a function");
        Job j;
        j.kind = 0;
        j.a = a[0];
        rt.vm->jobs.push_back(j);
        return Value();
    }, 1);
    DefNative(rt, g, "eval", [](Runtime& rt, CallArgs&) -> Value {
        rt.ThrowError(ErrorType::EvalError, "eval is not allowed in game scripts");
    }, 1);

    // console (not in ECMA-262; the usual host object): one line per call
    Object* con = rt.NewObject();
    DefValue(rt, g, "console", Value::Obj(con));
    for (const char* n : {"log", "info", "debug", "warn", "error"})
        DefNative(rt, con, n, [](Runtime& rt, CallArgs& a) {
            std::string s = Joined(rt, a);
            rt.Charge(s.size() / 16);
            if (rt.printSink) rt.printSink(s);
            return Value();
        }, 0);

    // Boolean (20.3)
    rt.booleanProto = rt.Alloc<BoxObj>();
    rt.booleanProto->cls = Cls::BoolBox;
    static_cast<BoxObj*>(rt.booleanProto)->prim = Value::Bool(false);
    rt.booleanProto->proto = rt.objectProto;
    DefCtor(rt, "Boolean", [](Runtime& rt, CallArgs& a) {
        bool b = rt.ToBoolean(a[0]);
        if (!a.newTarget) return Value::Bool(b);
        Object* o = rt.ToObject(Value::Bool(b));
        Value pr = rt.Get(Value::Obj(a.newTarget), rt.atoms->prototype);
        if (pr.IsObj()) o->proto = pr.O();
        return Value::Obj(o);
    }, 1, rt.booleanProto);
    auto thisBool = [](Runtime& rt, const Value& v) {
        if (v.IsBool()) return v.b;
        if (v.IsObj() && v.O()->cls == Cls::BoolBox) return static_cast<BoxObj*>(v.O())->prim.b;
        rt.ThrowType("this is not a Boolean");
    };
    (void)thisBool;
    DefNative(rt, rt.booleanProto, "toString", [](Runtime& rt, CallArgs& a) {
        Value v = a.thisv;
        if (v.IsObj() && v.O()->cls == Cls::BoolBox) v = static_cast<BoxObj*>(v.O())->prim;
        if (!v.IsBool()) rt.ThrowType("this is not a Boolean");
        return Value::Str(v.b ? rt.atoms->true_ : rt.atoms->false_);
    }, 0);
    DefNative(rt, rt.booleanProto, "valueOf", [](Runtime& rt, CallArgs& a) {
        Value v = a.thisv;
        if (v.IsObj() && v.O()->cls == Cls::BoolBox) v = static_cast<BoxObj*>(v.O())->prim;
        if (!v.IsBool()) rt.ThrowType("this is not a Boolean");
        return v;
    }, 0);

    // Symbol (20.4)
    rt.symbolProto = rt.NewObject();
    NativeFunc* sc = DefCtor(rt, "Symbol", [](Runtime& rt, CallArgs& a) {
        if (a.newTarget) rt.ThrowType("Symbol is not a constructor (call Symbol() without new)");
        String* d = a[0].IsUndef() ? nullptr : rt.ToString(a[0]);
        return Value::Sym(rt.NewSymbol(d));
    }, 0, rt.symbolProto);
    sc->ctor = true;   // so `new Symbol` reaches it and gets the message above
    DefValue(rt, sc, "iterator", Value::Sym(rt.symIterator), 0);
    DefValue(rt, sc, "asyncIterator", Value::Sym(rt.symAsyncIterator), 0);
    DefValue(rt, sc, "toPrimitive", Value::Sym(rt.symToPrimitive), 0);
    DefValue(rt, sc, "toStringTag", Value::Sym(rt.symToStringTag), 0);
    DefValue(rt, sc, "hasInstance", Value::Sym(rt.symHasInstance), 0);
    // 20.4.2: the other well-known symbols, so code can name them (species is read by the typed arrays; the others are not consulted yet)
    for (const char* name : {"species", "match", "matchAll", "replace", "search", "split", "unscopables", "isConcatSpreadable"}) {
        Symbol* y = std::string(name) == "species" ? rt.symSpecies : rt.NewSymbol(rt.Intern((std::string("Symbol.") + name).c_str()));
        DefValue(rt, sc, name, Value::Sym(y), 0);
    }
    DefNative(rt, sc, "for", [](Runtime& rt, CallArgs& a) {
        std::u16string k = rt.ToU16(a[0]);
        auto it = rt.symbolRegistry.find(k);
        if (it != rt.symbolRegistry.end()) return Value::Sym(it->second);
        Symbol* s = rt.NewSymbol(rt.NewString(k));
        s->registered = true;
        rt.symbolRegistry[k] = s;
        return Value::Sym(s);
    }, 1);
    DefNative(rt, sc, "keyFor", [](Runtime& rt, CallArgs& a) {
        if (!a[0].IsSym()) rt.ThrowType("Symbol.keyFor needs a symbol");
        Symbol* s = a[0].Y();
        return s->registered && s->desc ? Value::Str(s->desc) : Value();
    }, 1);
    auto thisSym = [](Runtime& rt, const Value& v) -> Symbol* {
        if (v.IsSym()) return v.Y();
        if (v.IsObj() && v.O()->cls == Cls::SymbolBox) return static_cast<BoxObj*>(v.O())->prim.Y();
        rt.ThrowType("this is not a Symbol");
    };
    (void)thisSym;
    DefNative(rt, rt.symbolProto, "toString", [](Runtime& rt, CallArgs& a) {
        Value v = a.thisv;
        if (v.IsObj() && v.O()->cls == Cls::SymbolBox) v = static_cast<BoxObj*>(v.O())->prim;
        if (!v.IsSym()) rt.ThrowType("this is not a Symbol");
        Symbol* s = v.Y();
        return Value::Str(rt.NewString(u"Symbol(" + (s->desc ? s->desc->s : u"") + u")"));
    }, 0);
    DefNative(rt, rt.symbolProto, "valueOf", [](Runtime& rt, CallArgs& a) {
        Value v = a.thisv;
        if (v.IsObj() && v.O()->cls == Cls::SymbolBox) v = static_cast<BoxObj*>(v.O())->prim;
        if (!v.IsSym()) rt.ThrowType("this is not a Symbol");
        return v;
    }, 0);
    DefGetter(rt, rt.symbolProto, "description", [](Runtime& rt, CallArgs& a) {
        Value v = a.thisv;
        if (v.IsObj() && v.O()->cls == Cls::SymbolBox) v = static_cast<BoxObj*>(v.O())->prim;
        if (!v.IsSym()) rt.ThrowType("this is not a Symbol");
        return v.Y()->desc ? Value::Str(v.Y()->desc) : Value();
    });
    rt.symbolProto->AddRaw(rt.symToStringTag, Value::Str(rt.Intern("Symbol")), kConfigurable);
    NativeFunc* toPrim = rt.NewNative([](Runtime& rt, CallArgs& a) {
        Value v = a.thisv;
        if (v.IsObj() && v.O()->cls == Cls::SymbolBox) v = static_cast<BoxObj*>(v.O())->prim;
        if (!v.IsSym()) rt.ThrowType("this is not a Symbol");
        return v;
    }, "[Symbol.toPrimitive]", 1);
    rt.symbolProto->AddRaw(rt.symToPrimitive, Value::Obj(toPrim), kConfigurable);

    // Reflect (28.1)
    Object* rf = rt.NewObject();
    DefValue(rt, g, "Reflect", Value::Obj(rf));
    rf->AddRaw(rt.symToStringTag, Value::Str(rt.Intern("Reflect")), kConfigurable);
    DefNative(rt, rf, "apply", [](Runtime& rt, CallArgs& a) {
        if (!rt.IsCallable(a[0])) rt.ThrowType("Reflect.apply needs a function");
        if (!a[2].IsObj()) rt.ThrowType("Reflect.apply needs an array-like argument list");
        std::uint64_t n = rt.LengthOf(a[2]);
        if (n > 100000) rt.ThrowRange("too many arguments");
        Runtime::RootedVector args(rt);
        for (std::uint64_t i = 0; i < n; ++i) args.v.push_back(rt.GetIndex(a[2], (std::uint32_t)i));
        return rt.Call(a[0], a[1], args.v.data(), (int)n);
    }, 3);
    DefNative(rt, rf, "construct", [](Runtime& rt, CallArgs& a) {
        if (!rt.IsConstructor(a[0])) rt.ThrowType("Reflect.construct needs a constructor");
        Value nt = a.argc > 2 ? a[2] : a[0];
        if (!rt.IsConstructor(nt)) rt.ThrowType("Reflect.construct's newTarget must be a constructor");
        if (!a[1].IsObj()) rt.ThrowType("Reflect.construct needs an array-like argument list");
        std::uint64_t n = rt.LengthOf(a[1]);
        if (n > 100000) rt.ThrowRange("too many arguments");
        Runtime::RootedVector args(rt);
        for (std::uint64_t i = 0; i < n; ++i) args.v.push_back(rt.GetIndex(a[1], (std::uint32_t)i));
        return rt.Construct(a[0], args.v.data(), (int)n, nt.O());
    }, 2);
    DefNative(rt, rf, "defineProperty", [](Runtime& rt, CallArgs& a) {
        Value o = ObjectArg(rt, a[0], "Reflect.defineProperty");
        GcObj* k = rt.ToPropertyKey(a[1]);
        return Value::Bool(rt.DefineOwnProperty(o.O(), k, ToDescriptor(rt, a[2])));
    }, 3);
    DefNative(rt, rf, "deleteProperty", [](Runtime& rt, CallArgs& a) {
        Value o = ObjectArg(rt, a[0], "Reflect.deleteProperty");
        return Value::Bool(rt.Delete(o.O(), rt.ToPropertyKey(a[1])));
    }, 2);
    DefNative(rt, rf, "get", [](Runtime& rt, CallArgs& a) {
        Value o = ObjectArg(rt, a[0], "Reflect.get");
        return rt.Get(o, rt.ToPropertyKey(a[1]), a.argc > 2 ? a[2] : o);
    }, 2);
    DefNative(rt, rf, "set", [](Runtime& rt, CallArgs& a) {   // 28.1.12: [[Set]] with the receiver, false when refused
        Value o = ObjectArg(rt, a[0], "Reflect.set");
        GcObj* k = rt.ToPropertyKey(a[1]);
        return Value::Bool(rt.SetWithReceiver(o.O(), k, a[2], a.argc > 3 ? a[3] : o));
    }, 3);
    DefNative(rt, rf, "has", [](Runtime& rt, CallArgs& a) {
        Value o = ObjectArg(rt, a[0], "Reflect.has");
        return Value::Bool(rt.HasProperty(o.O(), rt.ToPropertyKey(a[1])));
    }, 2);
    DefNative(rt, rf, "getPrototypeOf", [](Runtime& rt, CallArgs& a) {
        Value o = ObjectArg(rt, a[0], "Reflect.getPrototypeOf");
        Object* p = rt.GetPrototypeOf(o.O());
        return p ? Value::Obj(p) : Value::Null();
    }, 1);
    DefNative(rt, rf, "setPrototypeOf", [](Runtime& rt, CallArgs& a) {
        Value o = ObjectArg(rt, a[0], "Reflect.setPrototypeOf");
        if (!a[1].IsObj() && !a[1].IsNull()) rt.ThrowType("a prototype must be an object or null");
        return Value::Bool(rt.SetPrototype(o.O(), a[1].IsNull() ? nullptr : a[1].O()));
    }, 2);
    DefNative(rt, rf, "getOwnPropertyDescriptor", [](Runtime& rt, CallArgs& a) {
        Value o = ObjectArg(rt, a[0], "Reflect.getOwnPropertyDescriptor");
        Prop p;
        if (!rt.GetOwnProperty(o.O(), rt.ToPropertyKey(a[1]), p)) return Value();
        return FromDescriptor(rt, p);
    }, 2);
    DefNative(rt, rf, "isExtensible", [](Runtime& rt, CallArgs& a) {
        return Value::Bool(rt.IsExtensible(ObjectArg(rt, a[0], "Reflect.isExtensible").O()));
    }, 1);
    DefNative(rt, rf, "preventExtensions", [](Runtime& rt, CallArgs& a) {
        return Value::Bool(rt.PreventExtensions(ObjectArg(rt, a[0], "Reflect.preventExtensions").O()));
    }, 1);
    DefNative(rt, rf, "ownKeys", [](Runtime& rt, CallArgs& a) {
        Value o = ObjectArg(rt, a[0], "Reflect.ownKeys");
        ArrayObj* out = rt.NewArray();
        for (GcObj* k : rt.OwnKeys(o.O(), true, true))
            rt.DefineIndex(out, out->length, k->kind == Kind::String ? Value::Str(static_cast<String*>(k)) : Value::Sym(static_cast<Symbol*>(k)));
        return Value::Obj(out);
    }, 1);
}


// ============================================================ Proxy (10.5, 28.2)
// Written from ECMA-262 10.5.1-10.5.13 (the proxy internal methods and the invariants each one enforces), 10.5.14
// ProxyCreate, 28.2.1 the constructor and 28.2.2.1 Proxy.revocable, with 6.2.6 for descriptors and 10.1.6.2
// IsCompatiblePropertyDescriptor. Every proxy step charges the budget, and proxies running inside each other (a
// chain of proxies, or a proxy target that is itself a proxy) are bounded by Impl::kMaxProxyDepth, so a long chain
// is a RangeError instead of a deep native recursion. A trap that calls back into its own proxy goes through
// Runtime::Call, whose library-call depth limit stops it.
namespace {

struct ProxyGuard {
    Runtime& rt;
    explicit ProxyGuard(Runtime& r) : rt(r) {
        if (rt.vm->proxyDepth >= Runtime::Impl::kMaxProxyDepth) rt.ThrowRange("too much recursion (proxies nested too deeply)");
        ++rt.vm->proxyDepth;
        rt.Charge(1);
    }
    ~ProxyGuard() { --rt.vm->proxyDepth; }
    ProxyGuard(const ProxyGuard&) = delete;
    ProxyGuard& operator=(const ProxyGuard&) = delete;
};

// the handler, or a TypeError when the proxy was revoked; the trap (7.3.11 GetMethod), or undefined
Value Trap(Runtime& rt, ProxyObj* p, const char* name) {
    if (!p->handler) rt.ThrowType(std::string("can not use a revoked proxy ('") + name + "')");
    rt.Keep(p->handler);
    rt.Keep(p->target);
    Value t = rt.Get(Value::Obj(p->handler), rt.Intern(name));
    if (t.IsNullish()) return Value();
    if (!rt.IsCallable(t)) rt.ThrowType(std::string("the proxy's '") + name + "' trap is not a function");
    rt.Keep(t);
    return t;
}
// after the trap ran the proxy may have been revoked; the target and handler were read before (10.5.x step 1-3)
Value KeyValue(GcObj* k) {
    return k->kind == Kind::String ? Value::Str(static_cast<String*>(k)) : Value::Sym(static_cast<Symbol*>(k));
}
Value DescToObject(Runtime& rt, const Runtime::Descriptor& d) {   // 6.2.6.4 FromPropertyDescriptor (only the fields given)
    Object* o = rt.NewObject();
    if (d.hasValue) rt.DefineData(o, rt.atoms->value, d.value);
    if (d.hasWritable) rt.DefineData(o, rt.atoms->writable, Value::Bool(d.writable));
    if (d.hasGet) rt.DefineData(o, rt.atoms->get, d.get ? Value::Obj(d.get) : Value());
    if (d.hasSet) rt.DefineData(o, rt.atoms->set, d.set ? Value::Obj(d.set) : Value());
    if (d.hasEnumerable) rt.DefineData(o, rt.atoms->enumerable, Value::Bool(d.enumerable));
    if (d.hasConfigurable) rt.DefineData(o, rt.atoms->configurable, Value::Bool(d.configurable));
    return Value::Obj(o);
}
bool IsAccessorDesc(const Runtime::Descriptor& d) { return d.hasGet || d.hasSet; }
bool IsDataDesc(const Runtime::Descriptor& d) { return d.hasValue || d.hasWritable; }
// 10.1.6.2 IsCompatiblePropertyDescriptor (10.1.6.3 ValidateAndApplyPropertyDescriptor with O undefined)
bool Compatible(bool extensible, const Runtime::Descriptor& d, bool hasCurrent, const Prop& cur) {
    if (!hasCurrent) return extensible;
    if (cur.flags & kConfigurable) return true;
    if (d.hasConfigurable && d.configurable) return false;
    if (d.hasEnumerable && d.enumerable != (bool)(cur.flags & kEnumerable)) return false;
    const bool curAcc = cur.flags & kAccessor;
    if ((IsAccessorDesc(d) || IsDataDesc(d)) && IsAccessorDesc(d) != curAcc) return false;
    if (curAcc) {
        if (d.hasGet && d.get != cur.get) return false;
        if (d.hasSet && d.set != cur.set) return false;
    } else if (!(cur.flags & kWritable)) {
        if (d.hasWritable && d.writable) return false;
        if (d.hasValue && !SameValue(d.value, cur.v)) return false;
    }
    return true;
}
Value ArgList(Runtime& rt, const Value* args, int argc) {   // 7.3.18 CreateArrayFromList
    ArrayObj* a = rt.NewArray((std::size_t)argc);
    for (int i = 0; i < argc; ++i) a->elems.push_back(args[i]);
    a->length = (std::uint32_t)argc;
    rt.CountBytes((std::ptrdiff_t)(argc * sizeof(Value)));
    return Value::Obj(a);
}

} // namespace

void ProxyObj::Trace(Runtime& rt) {
    Object::Trace(rt);
    if (target) rt.Mark(target);
    if (handler) rt.Mark(handler);
}

// 10.5.1 [[GetPrototypeOf]]
Object* ProxyGetPrototypeOf(Runtime& rt, ProxyObj* p) {
    ProxyGuard g(rt);
    Value trap = Trap(rt, p, "getPrototypeOf");
    Object* target = p->target;
    if (trap.IsUndef()) return rt.GetPrototypeOf(target);
    Value targetV = Value::Obj(target);
    Value r = rt.Call(trap, Value::Obj(p->handler), &targetV, 1);
    if (!r.IsObj() && !r.IsNull()) rt.ThrowType("the proxy's getPrototypeOf trap must return an object or null");
    Object* proto = r.IsNull() ? nullptr : r.O();
    if (rt.IsExtensible(target)) return proto;
    if (rt.GetPrototypeOf(target) != proto) rt.ThrowType("the proxy's getPrototypeOf trap disagrees with its non-extensible target");
    return proto;
}

// 10.5.2 [[SetPrototypeOf]]
bool ProxySetPrototypeOf(Runtime& rt, ProxyObj* p, Object* proto) {
    ProxyGuard g(rt);
    Value trap = Trap(rt, p, "setPrototypeOf");
    Object* target = p->target;
    if (trap.IsUndef()) return rt.SetPrototype(target, proto);
    Value args[2] = {Value::Obj(target), proto ? Value::Obj(proto) : Value::Null()};
    if (!rt.ToBoolean(rt.Call(trap, Value::Obj(p->handler), args, 2))) return false;
    if (rt.IsExtensible(target)) return true;
    if (rt.GetPrototypeOf(target) != proto) rt.ThrowType("the proxy's setPrototypeOf trap disagrees with its non-extensible target");
    return true;
}

// 10.5.3 [[IsExtensible]]
bool ProxyIsExtensible(Runtime& rt, ProxyObj* p) {
    ProxyGuard g(rt);
    Value trap = Trap(rt, p, "isExtensible");
    Object* target = p->target;
    if (trap.IsUndef()) return rt.IsExtensible(target);
    Value targetV = Value::Obj(target);
    const bool r = rt.ToBoolean(rt.Call(trap, Value::Obj(p->handler), &targetV, 1));
    if (r != rt.IsExtensible(target)) rt.ThrowType("the proxy's isExtensible trap disagrees with its target");
    return r;
}

// 10.5.4 [[PreventExtensions]]
bool ProxyPreventExtensions(Runtime& rt, ProxyObj* p) {
    ProxyGuard g(rt);
    Value trap = Trap(rt, p, "preventExtensions");
    Object* target = p->target;
    if (trap.IsUndef()) return rt.PreventExtensions(target);
    Value targetV = Value::Obj(target);
    const bool r = rt.ToBoolean(rt.Call(trap, Value::Obj(p->handler), &targetV, 1));
    if (r && rt.IsExtensible(target)) rt.ThrowType("the proxy's preventExtensions trap returned true but its target is still extensible");
    return r;
}

// 10.5.5 [[GetOwnProperty]]
bool ProxyGetOwnProperty(Runtime& rt, ProxyObj* p, GcObj* key, Prop& out) {
    ProxyGuard g(rt);
    Value trap = Trap(rt, p, "getOwnPropertyDescriptor");
    Object* target = p->target;
    if (trap.IsUndef()) return rt.GetOwnProperty(target, key, out);
    Value args[2] = {Value::Obj(target), KeyValue(key)};
    Value r = rt.Call(trap, Value::Obj(p->handler), args, 2);
    if (!r.IsObj() && !r.IsUndef()) rt.ThrowType("the proxy's getOwnPropertyDescriptor trap must return an object or undefined");
    Prop cur;
    const bool hasCur = rt.GetOwnProperty(target, key, cur);
    if (hasCur) {
        rt.Keep(cur.v);
        if (cur.get) rt.Keep(cur.get);
        if (cur.set) rt.Keep(cur.set);
    }
    const std::string what = "the proxy's getOwnPropertyDescriptor trap for '" + KeyText(rt, key) + "'";
    if (r.IsUndef()) {
        if (!hasCur) return false;
        if (!(cur.flags & kConfigurable)) rt.ThrowType(what + " hides a property its target can not lose");
        if (!rt.IsExtensible(target)) rt.ThrowType(what + " hides a property of a non-extensible target");
        return false;
    }
    const bool extensible = rt.IsExtensible(target);
    Runtime::Descriptor d = ToDescriptor(rt, r);
    // 6.2.6.6 CompletePropertyDescriptor
    if (!IsAccessorDesc(d)) {
        if (!d.hasValue) { d.hasValue = true; d.value = Value(); }
        if (!d.hasWritable) { d.hasWritable = true; d.writable = false; }
    } else {
        if (!d.hasGet) { d.hasGet = true; d.get = nullptr; }
        if (!d.hasSet) { d.hasSet = true; d.set = nullptr; }
    }
    if (!d.hasEnumerable) { d.hasEnumerable = true; d.enumerable = false; }
    if (!d.hasConfigurable) { d.hasConfigurable = true; d.configurable = false; }
    if (!Compatible(extensible, d, hasCur, cur)) rt.ThrowType(what + " reports a property its target can not have");
    if (!d.configurable) {
        if (!hasCur || (cur.flags & kConfigurable)) rt.ThrowType(what + " reports a non-configurable property the target does not have");
        if (d.hasWritable && !d.writable && !(cur.flags & kAccessor) && (cur.flags & kWritable))
            rt.ThrowType(what + " reports a read-only property that is writable on the target");
    }
    out = Prop{};
    out.key = key;
    if (IsAccessorDesc(d)) {
        out.flags = kAccessor;
        out.get = d.get;
        out.set = d.set;
    } else {
        out.v = d.value;
        out.flags = d.writable ? kWritable : 0;
    }
    if (d.enumerable) out.flags |= kEnumerable;
    if (d.configurable) out.flags |= kConfigurable;
    return true;
}

// 10.5.6 [[DefineOwnProperty]]
bool ProxyDefineOwnProperty(Runtime& rt, ProxyObj* p, GcObj* key, const Runtime::Descriptor& d) {
    ProxyGuard g(rt);
    Value trap = Trap(rt, p, "defineProperty");
    Object* target = p->target;
    if (trap.IsUndef()) return rt.DefineOwnProperty(target, key, d);
    Value args[3] = {Value::Obj(target), KeyValue(key), DescToObject(rt, d)};
    if (!rt.ToBoolean(rt.Call(trap, Value::Obj(p->handler), args, 3))) return false;
    Prop cur;
    const bool hasCur = rt.GetOwnProperty(target, key, cur);
    const bool extensible = rt.IsExtensible(target);
    const bool settingConfigFalse = d.hasConfigurable && !d.configurable;
    const std::string what = "the proxy's defineProperty trap for '" + KeyText(rt, key) + "'";
    if (!hasCur) {
        if (!extensible) rt.ThrowType(what + " added a property to a non-extensible target");
        if (settingConfigFalse) rt.ThrowType(what + " reports a non-configurable property the target does not have");
    } else {
        if (!Compatible(extensible, d, true, cur)) rt.ThrowType(what + " reports a change its target can not make");
        if (settingConfigFalse && (cur.flags & kConfigurable)) rt.ThrowType(what + " reports a non-configurable property that is configurable on the target");
        if (!(cur.flags & kAccessor) && !(cur.flags & kConfigurable) && (cur.flags & kWritable) && d.hasWritable && !d.writable)
            rt.ThrowType(what + " reports a read-only property that is writable on the target");
    }
    return true;
}

// 10.5.7 [[HasProperty]]
bool ProxyHas(Runtime& rt, ProxyObj* p, GcObj* key) {
    ProxyGuard g(rt);
    Value trap = Trap(rt, p, "has");
    Object* target = p->target;
    if (trap.IsUndef()) return rt.HasProperty(target, key);
    Value args[2] = {Value::Obj(target), KeyValue(key)};
    const bool r = rt.ToBoolean(rt.Call(trap, Value::Obj(p->handler), args, 2));
    if (!r) {
        Prop cur;
        if (rt.GetOwnProperty(target, key, cur)) {
            if (!(cur.flags & kConfigurable)) rt.ThrowType("the proxy's has trap hides the non-configurable property '" + KeyText(rt, key) + "'");
            if (!rt.IsExtensible(target)) rt.ThrowType("the proxy's has trap hides '" + KeyText(rt, key) + "' of a non-extensible target");
        }
    }
    return r;
}

// 10.5.8 [[Get]]
Value ProxyGet(Runtime& rt, ProxyObj* p, GcObj* key, const Value& receiver) {
    ProxyGuard g(rt);
    Value trap = Trap(rt, p, "get");
    Object* target = p->target;
    if (trap.IsUndef()) return rt.Get(Value::Obj(target), key, receiver);
    Value args[3] = {Value::Obj(target), KeyValue(key), receiver};
    Value r = rt.Call(trap, Value::Obj(p->handler), args, 3);
    Prop cur;
    if (rt.GetOwnProperty(target, key, cur) && !(cur.flags & kConfigurable)) {
        if (!(cur.flags & kAccessor) && !(cur.flags & kWritable) && !SameValue(r, cur.v))
            rt.ThrowType("the proxy's get trap reports a different value for the read-only property '" + KeyText(rt, key) + "'");
        if ((cur.flags & kAccessor) && !cur.get && !r.IsUndef())
            rt.ThrowType("the proxy's get trap reports a value for '" + KeyText(rt, key) + "', which has no getter on the target");
    }
    return r;
}

// 10.5.9 [[Set]]
bool ProxySet(Runtime& rt, ProxyObj* p, GcObj* key, const Value& v, const Value& receiver) {
    ProxyGuard g(rt);
    Value trap = Trap(rt, p, "set");
    Object* target = p->target;
    if (trap.IsUndef()) return rt.SetWithReceiver(target, key, v, receiver);
    Value args[4] = {Value::Obj(target), KeyValue(key), v, receiver};
    if (!rt.ToBoolean(rt.Call(trap, Value::Obj(p->handler), args, 4))) return false;
    Prop cur;
    if (rt.GetOwnProperty(target, key, cur) && !(cur.flags & kConfigurable)) {
        if (!(cur.flags & kAccessor) && !(cur.flags & kWritable) && !SameValue(v, cur.v))
            rt.ThrowType("the proxy's set trap reports a change to the read-only property '" + KeyText(rt, key) + "'");
        if ((cur.flags & kAccessor) && !cur.set)
            rt.ThrowType("the proxy's set trap reports a change to '" + KeyText(rt, key) + "', which has no setter on the target");
    }
    return true;
}

// 10.5.10 [[Delete]]
bool ProxyDelete(Runtime& rt, ProxyObj* p, GcObj* key) {
    ProxyGuard g(rt);
    Value trap = Trap(rt, p, "deleteProperty");
    Object* target = p->target;
    if (trap.IsUndef()) return rt.Delete(target, key);
    Value args[2] = {Value::Obj(target), KeyValue(key)};
    if (!rt.ToBoolean(rt.Call(trap, Value::Obj(p->handler), args, 2))) return false;
    Prop cur;
    if (!rt.GetOwnProperty(target, key, cur)) return true;
    if (!(cur.flags & kConfigurable)) rt.ThrowType("the proxy's deleteProperty trap reports deleting the non-configurable property '" + KeyText(rt, key) + "'");
    if (!rt.IsExtensible(target)) rt.ThrowType("the proxy's deleteProperty trap reports deleting '" + KeyText(rt, key) + "' of a non-extensible target");
    return true;
}

// 10.5.11 [[OwnPropertyKeys]]
std::vector<GcObj*> ProxyOwnKeys(Runtime& rt, ProxyObj* p) {
    ProxyGuard g(rt);
    Value trap = Trap(rt, p, "ownKeys");
    Object* target = p->target;
    if (trap.IsUndef()) return rt.OwnKeys(target, true, true);
    Value targetV = Value::Obj(target);
    Value r = rt.Call(trap, Value::Obj(p->handler), &targetV, 1);
    // 7.3.19 CreateListFromArrayLike with the types String and Symbol
    if (!r.IsObj()) rt.ThrowType("the proxy's ownKeys trap must return an array-like object");
    const std::uint64_t n = rt.LengthOf(r);
    if (n > (1u << 24)) rt.ThrowRange("the proxy's ownKeys trap returned too many keys");
    std::vector<GcObj*> keys;
    std::unordered_set<GcObj*> seen;
    for (std::uint64_t i = 0; i < n; ++i) {
        rt.Charge(1);
        Value k = rt.GetIndex(r, (std::uint32_t)i);
        if (!k.IsStr() && !k.IsSym()) rt.ThrowType("the proxy's ownKeys trap returned a key that is not a string or a symbol");
        GcObj* key = rt.ToPropertyKey(k);
        rt.Keep(key);
        if (!seen.insert(key).second) rt.ThrowType("the proxy's ownKeys trap returned '" + KeyText(rt, key) + "' twice");
        keys.push_back(key);
    }
    const bool extensible = rt.IsExtensible(target);
    std::vector<GcObj*> targetKeys = rt.OwnKeys(target, true, true);
    std::vector<GcObj*> configurable, fixed;
    for (GcObj* k : targetKeys) {
        rt.Charge(1);
        Prop cur;
        if (rt.GetOwnProperty(target, k, cur) && !(cur.flags & kConfigurable)) fixed.push_back(k);
        else configurable.push_back(k);
    }
    if (extensible && fixed.empty()) return keys;
    std::unordered_set<GcObj*> unchecked(keys.begin(), keys.end());
    for (GcObj* k : fixed)
        if (!unchecked.erase(k)) rt.ThrowType("the proxy's ownKeys trap leaves out the non-configurable property '" + KeyText(rt, k) + "'");
    if (extensible) return keys;
    for (GcObj* k : configurable)
        if (!unchecked.erase(k)) rt.ThrowType("the proxy's ownKeys trap leaves out '" + KeyText(rt, k) + "' of a non-extensible target");
    if (!unchecked.empty()) rt.ThrowType("the proxy's ownKeys trap adds keys to a non-extensible target");
    return keys;
}

// 10.5.12 [[Call]]
Value ProxyCall(Runtime& rt, ProxyObj* p, const Value& thisv, const Value* args, int argc) {
    ProxyGuard g(rt);
    Value trap = Trap(rt, p, "apply");
    Object* target = p->target;
    if (trap.IsUndef()) return rt.Call(Value::Obj(target), thisv, args, argc);
    Value list = ArgList(rt, args, argc);
    Value targs[3] = {Value::Obj(target), thisv, list};
    return rt.Call(trap, Value::Obj(p->handler), targs, 3);
}

// 10.5.13 [[Construct]]
Value ProxyConstruct(Runtime& rt, ProxyObj* p, const Value* args, int argc, Object* newTarget) {
    ProxyGuard g(rt);
    Value trap = Trap(rt, p, "construct");
    Object* target = p->target;
    if (trap.IsUndef()) return rt.Construct(Value::Obj(target), args, argc, newTarget ? newTarget : p);
    Value list = ArgList(rt, args, argc);
    Value targs[3] = {Value::Obj(target), list, Value::Obj(newTarget ? newTarget : p)};
    Value r = rt.Call(trap, Value::Obj(p->handler), targs, 3);
    if (!r.IsObj()) rt.ThrowType("the proxy's construct trap must return an object");
    return r;
}

namespace {
// 10.5.14 ProxyCreate
ProxyObj* ProxyCreate(Runtime& rt, const Value& target, const Value& handler) {
    if (!target.IsObj() || !handler.IsObj()) rt.ThrowType("a Proxy needs an object as its target and its handler");
    ProxyObj* p = rt.Alloc<ProxyObj>();
    p->proto = nullptr;
    p->extensible = true;
    p->target = target.O();
    p->handler = handler.O();
    if (rt.IsCallable(target)) p->proxyFn = rt.IsConstructor(target) ? 3 : 1;
    return p;
}
}

void InstallProxyLib(Runtime& rt) {
    NativeFunc* pc = rt.NewNative([](Runtime& rt, CallArgs& a) {
        if (!a.newTarget) rt.ThrowType("Proxy must be called with 'new'");
        return Value::Obj(ProxyCreate(rt, a[0], a[1]));
    }, "Proxy", 2);
    pc->ctor = true;   // 28.2.1: no "prototype" property
    rt.global->AddRaw(rt.Intern("Proxy"), Value::Obj(pc), kHidden);
    DefNative(rt, pc, "revocable", [](Runtime& rt, CallArgs& a) {   // 28.2.2.1
        ProxyObj* p = ProxyCreate(rt, a[0], a[1]);
        NativeFunc* revoke = rt.NewNative([](Runtime&, CallArgs& a) {
            NativeFunc* self = a.callee;
            if (!self->slots.empty() && self->slots[0].IsObj()) {
                ProxyObj* q = static_cast<ProxyObj*>(self->slots[0].O());
                q->handler = nullptr;
                q->target = nullptr;
                self->slots[0] = Value();
            }
            return Value();
        }, "", 0, {Value::Obj(p)});
        Object* r = rt.NewObject();
        rt.DefineData(r, rt.Intern("proxy"), Value::Obj(p));
        rt.DefineData(r, rt.Intern("revoke"), Value::Obj(revoke));
        return Value::Obj(r);
    }, 2);
}

} // namespace okay::js
