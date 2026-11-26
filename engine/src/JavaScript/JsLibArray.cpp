// okay::js library: Array (ECMA-262 23.1). Written from 23.1.1-23.1.3: the
// constructor, Array.isArray / of / from and the prototype methods. They work on
// any array-like object as the spec says (through Get / Set with indices), with
// the property layer's fast paths doing the work for real arrays. Every method
// charges the run budget for each element it touches, and library loops that
// call back into a script drop their scratch values each step. sort is our own
// stable bottom-up merge sort over a rooted copy (23.1.3.30 SortIndexedProperties),
// so a comparator that mutates the array or triggers a collection is safe.
#include "JsInternal.hpp"
#include <algorithm>
#include <cmath>

namespace okay::js {

namespace {

struct ArrThis {
    Object* o;
    Value v;
    std::uint64_t len;
};

ArrThis This(Runtime& rt, CallArgs& a) {
    ArrThis t;
    t.o = rt.ToObject(a.thisv);
    rt.Keep(t.o);
    t.v = Value::Obj(t.o);
    t.len = rt.LengthOf(t.v);
    return t;
}

inline GcObj* IdxKey(Runtime& rt, std::uint64_t i) {
    return i < 4294967295ull ? rt.IndexKey((std::uint32_t)i) : rt.Intern(rt.NumberToU16((double)i));
}
inline Value GetAt(Runtime& rt, const Value& o, std::uint64_t i) {
    if (i < 4294967295ull) return rt.GetIndex(o, (std::uint32_t)i);
    return rt.Get(o, IdxKey(rt, i));
}
inline void SetAt(Runtime& rt, const Value& o, std::uint64_t i, const Value& v) {
    if (i < 4294967295ull) rt.PutIndex(o, (std::uint32_t)i, v);
    else rt.Put(o, IdxKey(rt, i), v);
}

Value Callback(Runtime& rt, const Value& fn, const char* method) {
    if (!rt.IsCallable(fn)) rt.ThrowType(std::string("Array.prototype.") + method + " needs a function");
    return fn;
}

void SetLen(Runtime& rt, const Value& o, std::uint64_t n) {
    if (o.IsObj() && (o.O()->cls == Cls::Array || o.O()->cls == Cls::Arguments)) SetLength(rt, o.O(), n);
    else rt.Put(o, rt.atoms->length, Value::Num((double)n));
}

// 23.1.3.15.1 FlattenIntoArray
void Flatten(Runtime& rt, ArrayObj* target, const Value& src, std::uint64_t len, double depth, const Value* mapFn,
             const Value& thisArg, int level) {
    if (level > 200) rt.ThrowRange("flat: the arrays are nested too deeply");
    for (std::uint64_t i = 0; i < len; ++i) {
        rt.Charge(1);
        if (!HasIndex(rt, src.O(), i)) continue;
        Runtime::ScratchScope ss(rt);
        Value e = GetAt(rt, src, i);
        if (mapFn) {
            Value args[3] = {e, Value::Num((double)i), src};
            e = rt.Call(*mapFn, thisArg, args, 3);
        }
        if (depth > 0 && rt.IsArray(e)) {
            rt.Keep(e);
            Flatten(rt, target, e, rt.LengthOf(e), depth - 1, nullptr, Value(), level + 1);
        } else rt.DefineIndex(target, target->length, e);
    }
}

int SortCompare(Runtime& rt, const Value& cmp, const Value& x, const Value& y) {
    // 23.1.3.30.2 SortCompare: undefined sorts last (handled by the caller)
    if (!cmp.IsUndef()) {
        Value args[2] = {x, y};
        Value r = rt.Call(cmp, Value(), args, 2);
        double d = rt.ToNumber(r);
        if (std::isnan(d)) return 0;
        return d < 0 ? -1 : d > 0 ? 1 : 0;
    }
    if (x.IsNum() && y.IsNum()) {   // the same order as comparing their strings, for the common integer case
        if (x.n == y.n) return 0;
        if (x.n >= 0 && y.n >= 0 && x.n == std::floor(x.n) && y.n == std::floor(y.n) && x.n < 1e15 && y.n < 1e15) {
            std::string a = Runtime::NumberToStdString(x.n), b = Runtime::NumberToStdString(y.n);
            return a < b ? -1 : a > b ? 1 : 0;
        }
    }
    String* a = rt.ToString(x);
    rt.Keep(Value::Str(a));
    String* b = rt.ToString(y);
    return a->s < b->s ? -1 : a->s > b->s ? 1 : 0;
}

// Our own stable merge sort (bottom-up runs of width 1, 2, 4, ...), calling SortCompare.
void MergeSort(Runtime& rt, std::vector<Value>& v, std::vector<Value>& tmp, const Value& cmp) {
    std::size_t n = v.size();
    tmp.assign(n, Value());
    for (std::size_t width = 1; width < n; width *= 2) {
        for (std::size_t lo = 0; lo < n; lo += 2 * width) {
            std::size_t mid = std::min(lo + width, n), hi = std::min(lo + 2 * width, n);
            std::size_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi) {
                rt.Charge(2);
                Runtime::ScratchScope ss(rt);
                if (SortCompare(rt, cmp, v[j], v[i]) < 0) tmp[k++] = v[j++];
                else tmp[k++] = v[i++];
            }
            while (i < mid) tmp[k++] = v[i++];
            while (j < hi) tmp[k++] = v[j++];
        }
        v.swap(tmp);
    }
}

std::u16string JoinImpl(Runtime& rt, const Value& o, std::uint64_t len, const std::u16string& sep) {
    auto& stack = rt.vm->joinStack;
    if (std::find(stack.begin(), stack.end(), o.O()) != stack.end()) return u"";   // a cycle joins as empty
    if (stack.size() > 100) rt.ThrowRange("join: the arrays are nested too deeply");
    stack.push_back(o.O());
    std::u16string out;
    try {
        for (std::uint64_t i = 0; i < len; ++i) {
            if (i) out += sep;
            Runtime::ScratchScope ss(rt);
            Value e = GetAt(rt, o, i);
            if (!e.IsNullish()) out += rt.ToString(e)->s;
            rt.Charge(1 + out.size() / 4096);
            if (out.size() > (1u << 28)) rt.ThrowRange("join: the result is too long");
        }
    } catch (...) {
        stack.pop_back();
        throw;
    }
    stack.pop_back();
    return out;
}

double FindIndexOf(Runtime& rt, ArrThis& t, const Value& target, const Value& fromV, bool includes, bool last) {
    if (t.len == 0) return -1;
    double from;
    if (last) {
        from = fromV.IsUndef() ? (double)t.len - 1 : rt.ToIntegerOrInfinity(fromV);
        if (from < 0) from += (double)t.len;
        if (from >= (double)t.len) from = (double)t.len - 1;
        for (double i = from; i >= 0; --i) {
            rt.Charge(1);
            std::uint64_t k = (std::uint64_t)i;
            if (!HasIndex(rt, t.o, k)) continue;
            if (StrictEquals(GetAt(rt, t.v, k), target)) return i;
        }
        return -1;
    }
    from = rt.ToIntegerOrInfinity(fromV);
    if (from < 0) from = std::max(0.0, (double)t.len + from);
    for (double i = from; i < (double)t.len; ++i) {
        rt.Charge(1);
        std::uint64_t k = (std::uint64_t)i;
        if (includes) {
            if (SameValueZero(GetAt(rt, t.v, k), target)) return i;
        } else {
            if (!HasIndex(rt, t.o, k)) continue;
            if (StrictEquals(GetAt(rt, t.v, k), target)) return i;
        }
    }
    return -1;
}

} // namespace

void InstallArrayLib(Runtime& rt) {
    ArrayObj* ap = rt.Alloc<ArrayObj>();
    ap->proto = rt.objectProto;
    rt.arrayProto = ap;
    NativeFunc* ctor = DefCtor(rt, "Array", [](Runtime& rt, CallArgs& a) {
        ArrayObj* arr = rt.NewArray();
        if (a.newTarget && a.newTarget != a.callee) {
            Value pr = rt.Get(Value::Obj(a.newTarget), rt.atoms->prototype);
            if (pr.IsObj()) arr->proto = pr.O();
        }
        if (a.argc == 1 && a[0].IsNum()) {
            double d = a[0].n;
            std::uint32_t n = rt.ToUint32(a[0]);
            if ((double)n != d) rt.ThrowRange("an array length must be a whole number below 2^32");
            arr->length = n;
            return Value::Obj(arr);
        }
        for (int i = 0; i < a.argc; ++i) arr->elems.push_back(a[i]);
        arr->length = (std::uint32_t)a.argc;
        return Value::Obj(arr);
    }, 1, ap);
    rt.arrayCtor = ctor;
    DefNative(rt, ctor, "isArray", [](Runtime& rt, CallArgs& a) { return Value::Bool(rt.IsArray(a[0])); }, 1);
    DefNative(rt, ctor, "of", [](Runtime& rt, CallArgs& a) {
        ArrayObj* arr = rt.NewArray((std::size_t)a.argc);
        for (int i = 0; i < a.argc; ++i) arr->elems.push_back(a[i]);
        arr->length = (std::uint32_t)a.argc;
        return Value::Obj(arr);
    }, 0);
    DefNative(rt, ctor, "from", [](Runtime& rt, CallArgs& a) {   // 23.1.2.1
        Value src = a[0];
        bool mapping = !a[1].IsUndef();
        if (mapping) Callback(rt, a[1], "from");
        ArrayObj* out = rt.NewArray();
        Value outV = Value::Obj(out);
        rt.Keep(outV);
        if (src.IsNullish()) rt.ThrowType("Array.from needs an iterable or array-like value");
        Value usingIter = rt.Get(src, rt.symIterator);
        if (!usingIter.IsNullish()) {
            IterRec* r = GetIterator(rt, src);
            Value e;
            std::uint32_t k = 0;
            while (IterStep(rt, r, e)) {
                Runtime::ScratchScope ss(rt);
                if (mapping) {
                    Value args[2] = {e, Value::Num(k)};
                    e = rt.Call(a[1], a[2], args, 2);
                }
                rt.DefineIndex(out, k++, e);
            }
            return outV;
        }
        Object* o = rt.ToObject(src);
        rt.Keep(o);
        std::uint64_t len = rt.LengthOf(Value::Obj(o));
        if (len > (1u << 26)) rt.ThrowRange("Array.from: the array would be too big");
        for (std::uint64_t k = 0; k < len; ++k) {
            rt.Charge(1);
            Runtime::ScratchScope ss(rt);
            Value e = GetAt(rt, Value::Obj(o), k);
            if (mapping) {
                Value args[2] = {e, Value::Num((double)k)};
                e = rt.Call(a[1], a[2], args, 2);
            }
            rt.DefineIndex(out, (std::uint32_t)k, e);
        }
        return outV;
    }, 1);

    DefNative(rt, ap, "push", [](Runtime& rt, CallArgs& a) {
        ArrThis t = This(rt, a);
        if (t.len + (std::uint64_t)a.argc > 9007199254740991ull) rt.ThrowType("push would make the array too long");
        for (int i = 0; i < a.argc; ++i) SetAt(rt, t.v, t.len + (std::uint64_t)i, a[i]);
        std::uint64_t n = t.len + (std::uint64_t)a.argc;
        SetLen(rt, t.v, n);
        return Value::Num((double)n);
    }, 1);
    DefNative(rt, ap, "pop", [](Runtime& rt, CallArgs& a) {
        ArrThis t = This(rt, a);
        if (t.len == 0) {
            SetLen(rt, t.v, 0);
            return Value();
        }
        Value e = GetAt(rt, t.v, t.len - 1);
        DeleteIndex(rt, t.o, t.len - 1);
        SetLen(rt, t.v, t.len - 1);
        return e;
    }, 0);
    DefNative(rt, ap, "shift", [](Runtime& rt, CallArgs& a) {
        ArrThis t = This(rt, a);
        if (t.len == 0) {
            SetLen(rt, t.v, 0);
            return Value();
        }
        if (t.o->cls == Cls::Array) {   // a dense array: move the elements in one go
            ArrayObj* arr = static_cast<ArrayObj*>(t.o);
            if (arr->sparse == 0 && arr->elems.size() == arr->length && !arr->sealedElems && !arr->frozenElems &&
                arr->lengthWritable && std::none_of(arr->elems.begin(), arr->elems.end(), [](const Value& v) { return v.IsEmpty(); })) {
                rt.Charge(arr->elems.size() / 8 + 1);
                Value first = arr->elems.front();
                arr->elems.erase(arr->elems.begin());
                arr->length -= 1;
                return first;
            }
        }
        Value first = GetAt(rt, t.v, 0);
        rt.Keep(first);
        for (std::uint64_t k = 1; k < t.len; ++k) {
            rt.Charge(1);
            if (HasIndex(rt, t.o, k)) SetAt(rt, t.v, k - 1, GetAt(rt, t.v, k));
            else DeleteIndex(rt, t.o, k - 1);
        }
        DeleteIndex(rt, t.o, t.len - 1);
        SetLen(rt, t.v, t.len - 1);
        return first;
    }, 0);
    DefNative(rt, ap, "unshift", [](Runtime& rt, CallArgs& a) {
        ArrThis t = This(rt, a);
        std::uint64_t n = (std::uint64_t)a.argc;
        if (n) {
            if (t.o->cls == Cls::Array) {
                ArrayObj* arr = static_cast<ArrayObj*>(t.o);
                if (arr->sparse == 0 && arr->elems.size() == arr->length && !arr->sealedElems && arr->extensible &&
                    arr->lengthWritable && std::none_of(arr->elems.begin(), arr->elems.end(), [](const Value& v) { return v.IsEmpty(); })) {
                    rt.Charge(arr->elems.size() / 8 + n);
                    arr->elems.insert(arr->elems.begin(), a.argv, a.argv + a.argc);
                    arr->length = (std::uint32_t)arr->elems.size();
                    rt.CountBytes((std::ptrdiff_t)(n * sizeof(Value)));
                    return Value::Num(arr->length);
                }
            }
            for (std::uint64_t k = t.len; k > 0; --k) {
                rt.Charge(1);
                if (HasIndex(rt, t.o, k - 1)) SetAt(rt, t.v, k - 1 + n, GetAt(rt, t.v, k - 1));
                else DeleteIndex(rt, t.o, k - 1 + n);
            }
            for (std::uint64_t j = 0; j < n; ++j) SetAt(rt, t.v, j, a[(int)j]);
        }
        SetLen(rt, t.v, t.len + n);
        return Value::Num((double)(t.len + n));
    }, 1);
    DefNative(rt, ap, "slice", [](Runtime& rt, CallArgs& a) {
        ArrThis t = This(rt, a);
        std::int64_t k = RelIndex(rt, a[0], (std::int64_t)t.len, 0);
        std::int64_t end = RelIndex(rt, a[1], (std::int64_t)t.len, (std::int64_t)t.len);
        ArrayObj* out = rt.NewArray(end > k ? (std::size_t)(end - k) : 0);
        std::uint32_t n = 0;
        for (; k < end; ++k, ++n) {
            rt.Charge(1);
            if (HasIndex(rt, t.o, (std::uint64_t)k)) rt.DefineIndex(out, n, GetAt(rt, t.v, (std::uint64_t)k));
        }
        SetLength(rt, out, n);
        return Value::Obj(out);
    }, 2);
    DefNative(rt, ap, "splice", [](Runtime& rt, CallArgs& a) {   // 23.1.3.31
        ArrThis t = This(rt, a);
        std::int64_t len = (std::int64_t)t.len;
        std::int64_t start = RelIndex(rt, a[0], len, 0);
        std::int64_t delCount;
        if (a.argc == 0) delCount = 0;
        else if (a.argc == 1) delCount = len - start;
        else {
            double dc = rt.ToIntegerOrInfinity(a[1]);
            delCount = (std::int64_t)std::min(std::max(dc, 0.0), (double)(len - start));
        }
        std::int64_t items = a.argc > 2 ? a.argc - 2 : 0;
        ArrayObj* removed = rt.NewArray((std::size_t)delCount);
        rt.Keep(Value::Obj(removed));
        for (std::int64_t k = 0; k < delCount; ++k) {
            rt.Charge(1);
            std::uint64_t from = (std::uint64_t)(start + k);
            if (HasIndex(rt, t.o, from)) rt.DefineIndex(removed, (std::uint32_t)k, GetAt(rt, t.v, from));
        }
        SetLength(rt, removed, (std::uint64_t)delCount);
        if (items < delCount) {
            for (std::int64_t k = start; k < len - delCount; ++k) {
                rt.Charge(1);
                std::uint64_t from = (std::uint64_t)(k + delCount), to = (std::uint64_t)(k + items);
                if (HasIndex(rt, t.o, from)) SetAt(rt, t.v, to, GetAt(rt, t.v, from));
                else DeleteIndex(rt, t.o, to);
            }
            for (std::int64_t k = len; k > len - delCount + items; --k) DeleteIndex(rt, t.o, (std::uint64_t)(k - 1));
        } else if (items > delCount) {
            for (std::int64_t k = len - delCount; k > start; --k) {
                rt.Charge(1);
                std::uint64_t from = (std::uint64_t)(k + delCount - 1), to = (std::uint64_t)(k + items - 1);
                if (HasIndex(rt, t.o, from)) SetAt(rt, t.v, to, GetAt(rt, t.v, from));
                else DeleteIndex(rt, t.o, to);
            }
        }
        for (std::int64_t k = 0; k < items; ++k) SetAt(rt, t.v, (std::uint64_t)(start + k), a[(int)(k + 2)]);
        SetLen(rt, t.v, (std::uint64_t)(len - delCount + items));
        return Value::Obj(removed);
    }, 2);
    DefNative(rt, ap, "concat", [](Runtime& rt, CallArgs& a) {
        Object* self = rt.ToObject(a.thisv);
        rt.Keep(self);
        ArrayObj* out = rt.NewArray();
        rt.Keep(Value::Obj(out));
        std::uint64_t n = 0;
        auto add = [&](const Value& e) {
            if (rt.IsArray(e)) {
                std::uint64_t len = rt.LengthOf(e);
                for (std::uint64_t k = 0; k < len; ++k, ++n) {
                    rt.Charge(1);
                    if (HasIndex(rt, e.O(), k)) rt.DefineIndex(out, (std::uint32_t)n, GetAt(rt, e, k));
                }
            } else rt.DefineIndex(out, (std::uint32_t)n++, e);
        };
        add(Value::Obj(self));
        for (int i = 0; i < a.argc; ++i) add(a[i]);
        SetLength(rt, out, n);
        return Value::Obj(out);
    }, 1);
    DefNative(rt, ap, "join", [](Runtime& rt, CallArgs& a) {
        ArrThis t = This(rt, a);
        std::u16string sep = a[0].IsUndef() ? u"," : rt.ToU16(a[0]);
        return Value::Str(rt.NewString(JoinImpl(rt, t.v, t.len, sep)));
    }, 1);
    DefNative(rt, ap, "toString", [](Runtime& rt, CallArgs& a) {
        Object* o = rt.ToObject(a.thisv);
        Value j = rt.Get(Value::Obj(o), rt.atoms->join);
        if (rt.IsCallable(j)) return rt.Call(j, Value::Obj(o), nullptr, 0);
        return Value::Str(rt.Intern("[object Array]"));
    }, 0);
    DefNative(rt, ap, "reverse", [](Runtime& rt, CallArgs& a) {
        ArrThis t = This(rt, a);
        for (std::uint64_t lo = 0, hi = t.len ? t.len - 1 : 0; lo < hi; ++lo, --hi) {
            rt.Charge(1);
            bool hl = HasIndex(rt, t.o, lo), hh = HasIndex(rt, t.o, hi);
            Value vl = hl ? GetAt(rt, t.v, lo) : Value();
            rt.Keep(vl);
            Value vh = hh ? GetAt(rt, t.v, hi) : Value();
            rt.Keep(vh);
            if (hh) SetAt(rt, t.v, lo, vh); else DeleteIndex(rt, t.o, lo);
            if (hl) SetAt(rt, t.v, hi, vl); else DeleteIndex(rt, t.o, hi);
        }
        return t.v;
    }, 0);
    DefNative(rt, ap, "indexOf", [](Runtime& rt, CallArgs& a) {
        ArrThis t = This(rt, a);
        return Value::Num(FindIndexOf(rt, t, a[0], a[1], false, false));
    }, 1);
    DefNative(rt, ap, "lastIndexOf", [](Runtime& rt, CallArgs& a) {
        ArrThis t = This(rt, a);
        return Value::Num(FindIndexOf(rt, t, a[0], a.argc > 1 ? a[1] : Value(), false, true));
    }, 1);
    DefNative(rt, ap, "includes", [](Runtime& rt, CallArgs& a) {
        ArrThis t = This(rt, a);
        return Value::Bool(FindIndexOf(rt, t, a[0], a[1], true, false) >= 0);
    }, 1);
    DefNative(rt, ap, "at", [](Runtime& rt, CallArgs& a) {
        ArrThis t = This(rt, a);
        double k = rt.ToIntegerOrInfinity(a[0]);
        if (k < 0) k += (double)t.len;
        if (k < 0 || k >= (double)t.len) return Value();
        return GetAt(rt, t.v, (std::uint64_t)k);
    }, 1);
    DefNative(rt, ap, "fill", [](Runtime& rt, CallArgs& a) {
        ArrThis t = This(rt, a);
        std::int64_t k = RelIndex(rt, a[1], (std::int64_t)t.len, 0);
        std::int64_t end = RelIndex(rt, a[2], (std::int64_t)t.len, (std::int64_t)t.len);
        for (; k < end; ++k) {
            rt.Charge(1);
            SetAt(rt, t.v, (std::uint64_t)k, a[0]);
        }
        return t.v;
    }, 1);
    DefNative(rt, ap, "copyWithin", [](Runtime& rt, CallArgs& a) {
        ArrThis t = This(rt, a);
        std::int64_t len = (std::int64_t)t.len;
        std::int64_t to = RelIndex(rt, a[0], len, 0);
        std::int64_t from = RelIndex(rt, a[1], len, 0);
        std::int64_t fin = RelIndex(rt, a[2], len, len);
        std::int64_t count = std::min(fin - from, len - to);
        int dir = 1;
        if (from < to && to < from + count) {
            dir = -1;
            from += count - 1;
            to += count - 1;
        }
        for (; count > 0; --count, from += dir, to += dir) {
            rt.Charge(1);
            if (HasIndex(rt, t.o, (std::uint64_t)from)) SetAt(rt, t.v, (std::uint64_t)to, GetAt(rt, t.v, (std::uint64_t)from));
            else DeleteIndex(rt, t.o, (std::uint64_t)to);
        }
        return t.v;
    }, 2);

    // ---- methods that call a function for each element (23.1.3.x)
    // kind: 0 forEach, 1 map, 2 filter, 3 some, 4 every, 5 find, 6 findIndex, 7 findLast, 8 findLastIndex
    static const char* const iterNames[] = {"forEach", "map", "filter", "some", "every", "find", "findIndex", "findLast", "findLastIndex"};
    static const NativeFn iterFns[] = {
        [](Runtime& rt, CallArgs& a) -> Value {   // forEach
            ArrThis t = This(rt, a);
            Value fn = Callback(rt, a[0], "forEach");
            for (std::uint64_t k = 0; k < t.len; ++k) {
                rt.Charge(1);
                if (!HasIndex(rt, t.o, k)) continue;
                Runtime::ScratchScope ss(rt);
                Value args[3] = {GetAt(rt, t.v, k), Value::Num((double)k), t.v};
                rt.Call(fn, a[1], args, 3);
            }
            return Value();
        },
        [](Runtime& rt, CallArgs& a) -> Value {   // map
            ArrThis t = This(rt, a);
            Value fn = Callback(rt, a[0], "map");
            if (t.len > (1u << 26)) rt.ThrowRange("map: the array would be too big");
            ArrayObj* out = rt.NewArray((std::size_t)t.len);
            rt.Keep(Value::Obj(out));
            for (std::uint64_t k = 0; k < t.len; ++k) {
                rt.Charge(1);
                if (!HasIndex(rt, t.o, k)) continue;
                Runtime::ScratchScope ss(rt);
                Value args[3] = {GetAt(rt, t.v, k), Value::Num((double)k), t.v};
                Value r = rt.Call(fn, a[1], args, 3);
                rt.DefineIndex(out, (std::uint32_t)k, r);
            }
            SetLength(rt, out, t.len);
            return Value::Obj(out);
        },
        [](Runtime& rt, CallArgs& a) -> Value {   // filter
            ArrThis t = This(rt, a);
            Value fn = Callback(rt, a[0], "filter");
            ArrayObj* out = rt.NewArray();
            rt.Keep(Value::Obj(out));
            for (std::uint64_t k = 0; k < t.len; ++k) {
                rt.Charge(1);
                if (!HasIndex(rt, t.o, k)) continue;
                Runtime::ScratchScope ss(rt);
                Value e = GetAt(rt, t.v, k);
                Value args[3] = {e, Value::Num((double)k), t.v};
                Runtime::RootedVector keep(rt);
                keep.v.push_back(e);
                if (rt.ToBoolean(rt.Call(fn, a[1], args, 3))) rt.DefineIndex(out, out->length, e);
            }
            return Value::Obj(out);
        },
        [](Runtime& rt, CallArgs& a) -> Value {   // some
            ArrThis t = This(rt, a);
            Value fn = Callback(rt, a[0], "some");
            for (std::uint64_t k = 0; k < t.len; ++k) {
                rt.Charge(1);
                if (!HasIndex(rt, t.o, k)) continue;
                Runtime::ScratchScope ss(rt);
                Value args[3] = {GetAt(rt, t.v, k), Value::Num((double)k), t.v};
                if (rt.ToBoolean(rt.Call(fn, a[1], args, 3))) return Value::Bool(true);
            }
            return Value::Bool(false);
        },
        [](Runtime& rt, CallArgs& a) -> Value {   // every
            ArrThis t = This(rt, a);
            Value fn = Callback(rt, a[0], "every");
            for (std::uint64_t k = 0; k < t.len; ++k) {
                rt.Charge(1);
                if (!HasIndex(rt, t.o, k)) continue;
                Runtime::ScratchScope ss(rt);
                Value args[3] = {GetAt(rt, t.v, k), Value::Num((double)k), t.v};
                if (!rt.ToBoolean(rt.Call(fn, a[1], args, 3))) return Value::Bool(false);
            }
            return Value::Bool(true);
        },
        [](Runtime& rt, CallArgs& a) -> Value {   // find
            ArrThis t = This(rt, a);
            Value fn = Callback(rt, a[0], "find");
            for (std::uint64_t k = 0; k < t.len; ++k) {
                rt.Charge(1);
                Value e = GetAt(rt, t.v, k);
                Runtime::RootedVector keep(rt);
                keep.v.push_back(e);
                Value args[3] = {e, Value::Num((double)k), t.v};
                if (rt.ToBoolean(rt.Call(fn, a[1], args, 3))) { rt.Keep(e); return e; }
            }
            return Value();
        },
        [](Runtime& rt, CallArgs& a) -> Value {   // findIndex
            ArrThis t = This(rt, a);
            Value fn = Callback(rt, a[0], "findIndex");
            for (std::uint64_t k = 0; k < t.len; ++k) {
                rt.Charge(1);
                Runtime::ScratchScope ss(rt);
                Value args[3] = {GetAt(rt, t.v, k), Value::Num((double)k), t.v};
                if (rt.ToBoolean(rt.Call(fn, a[1], args, 3))) return Value::Num((double)k);
            }
            return Value::Num(-1);
        },
        [](Runtime& rt, CallArgs& a) -> Value {   // findLast
            ArrThis t = This(rt, a);
            Value fn = Callback(rt, a[0], "findLast");
            for (std::uint64_t k = t.len; k > 0; --k) {
                rt.Charge(1);
                Value e = GetAt(rt, t.v, k - 1);
                Runtime::RootedVector keep(rt);
                keep.v.push_back(e);
                Value args[3] = {e, Value::Num((double)(k - 1)), t.v};
                if (rt.ToBoolean(rt.Call(fn, a[1], args, 3))) { rt.Keep(e); return e; }
            }
            return Value();
        },
        [](Runtime& rt, CallArgs& a) -> Value {   // findLastIndex
            ArrThis t = This(rt, a);
            Value fn = Callback(rt, a[0], "findLastIndex");
            for (std::uint64_t k = t.len; k > 0; --k) {
                rt.Charge(1);
                Runtime::ScratchScope ss(rt);
                Value args[3] = {GetAt(rt, t.v, k - 1), Value::Num((double)(k - 1)), t.v};
                if (rt.ToBoolean(rt.Call(fn, a[1], args, 3))) return Value::Num((double)(k - 1));
            }
            return Value::Num(-1);
        },
    };
    for (int i = 0; i < 9; ++i) DefNative(rt, ap, iterNames[i], iterFns[i], 1);

    // reduce / reduceRight (23.1.3.24-25)
    static const NativeFn reduceFns[] = {
        [](Runtime& rt, CallArgs& a) -> Value {
            ArrThis t = This(rt, a);
            Value fn = Callback(rt, a[0], "reduce");
            std::uint64_t k = 0;
            Runtime::RootedVector acc(rt);
            acc.v.push_back(Value());
            if (a.argc >= 2) acc.v[0] = a[1];
            else {
                bool found = false;
                for (; k < t.len && !found; ++k)
                    if (HasIndex(rt, t.o, k)) { acc.v[0] = GetAt(rt, t.v, k); found = true; }
                if (!found) rt.ThrowType("reduce of an empty array with no initial value");
            }
            for (; k < t.len; ++k) {
                rt.Charge(1);
                if (!HasIndex(rt, t.o, k)) continue;
                Runtime::ScratchScope ss(rt);
                Value args[4] = {acc.v[0], GetAt(rt, t.v, k), Value::Num((double)k), t.v};
                acc.v[0] = rt.Call(fn, Value(), args, 4);
            }
            rt.Keep(acc.v[0]);
            return acc.v[0];
        },
        [](Runtime& rt, CallArgs& a) -> Value {
            ArrThis t = This(rt, a);
            Value fn = Callback(rt, a[0], "reduceRight");
            std::int64_t k = (std::int64_t)t.len - 1;
            Runtime::RootedVector acc(rt);
            acc.v.push_back(Value());
            if (a.argc >= 2) acc.v[0] = a[1];
            else {
                bool found = false;
                for (; k >= 0 && !found; --k)
                    if (HasIndex(rt, t.o, (std::uint64_t)k)) { acc.v[0] = GetAt(rt, t.v, (std::uint64_t)k); found = true; }
                if (!found) rt.ThrowType("reduceRight of an empty array with no initial value");
            }
            for (; k >= 0; --k) {
                rt.Charge(1);
                if (!HasIndex(rt, t.o, (std::uint64_t)k)) continue;
                Runtime::ScratchScope ss(rt);
                Value args[4] = {acc.v[0], GetAt(rt, t.v, (std::uint64_t)k), Value::Num((double)k), t.v};
                acc.v[0] = rt.Call(fn, Value(), args, 4);
            }
            rt.Keep(acc.v[0]);
            return acc.v[0];
        },
    };
    DefNative(rt, ap, "reduce", reduceFns[0], 1);
    DefNative(rt, ap, "reduceRight", reduceFns[1], 1);

    DefNative(rt, ap, "sort", [](Runtime& rt, CallArgs& a) -> Value {   // 23.1.3.30
        Value cmp = a[0];
        if (!cmp.IsUndef() && !rt.IsCallable(cmp)) rt.ThrowType("sort's comparator must be a function");
        Object* o = rt.ToObject(a.thisv);
        rt.Keep(o);
        Value ov = Value::Obj(o);
        std::uint64_t len = rt.LengthOf(ov);
        if (len > (1u << 26)) rt.ThrowRange("sort: the array is too big");
        Runtime::RootedVector items(rt), tmp(rt);
        std::uint64_t undefs = 0;
        for (std::uint64_t k = 0; k < len; ++k) {
            rt.Charge(1);
            if (!HasIndex(rt, o, k)) continue;
            Value e = GetAt(rt, ov, k);
            if (e.IsUndef()) ++undefs;
            else items.v.push_back(e);
        }
        MergeSort(rt, items.v, tmp.v, cmp);
        std::uint64_t k = 0;
        for (const Value& e : items.v) SetAt(rt, ov, k++, e);
        for (std::uint64_t u = 0; u < undefs; ++u) SetAt(rt, ov, k++, Value());
        for (; k < len; ++k) DeleteIndex(rt, o, k);
        return ov;
    }, 1);
    DefNative(rt, ap, "flat", [](Runtime& rt, CallArgs& a) {
        ArrThis t = This(rt, a);
        double depth = a[0].IsUndef() ? 1 : rt.ToIntegerOrInfinity(a[0]);
        ArrayObj* out = rt.NewArray();
        rt.Keep(Value::Obj(out));
        Flatten(rt, out, t.v, t.len, depth, nullptr, Value(), 0);
        return Value::Obj(out);
    }, 0);
    DefNative(rt, ap, "flatMap", [](Runtime& rt, CallArgs& a) {
        ArrThis t = This(rt, a);
        Value fn = Callback(rt, a[0], "flatMap");
        ArrayObj* out = rt.NewArray();
        rt.Keep(Value::Obj(out));
        Flatten(rt, out, t.v, t.len, 1, &fn, a[1], 0);
        return Value::Obj(out);
    }, 1);
    DefNative(rt, ap, "toReversed", [](Runtime& rt, CallArgs& a) {
        ArrThis t = This(rt, a);
        if (t.len > (1u << 26)) rt.ThrowRange("toReversed: the array is too big");
        ArrayObj* out = rt.NewArray((std::size_t)t.len);
        for (std::uint64_t k = 0; k < t.len; ++k) {
            rt.Charge(1);
            rt.DefineIndex(out, (std::uint32_t)k, GetAt(rt, t.v, t.len - 1 - k));
        }
        return Value::Obj(out);
    }, 0);
    DefNative(rt, ap, "toSorted", [](Runtime& rt, CallArgs& a) {
        Value cmp = a[0];
        if (!cmp.IsUndef() && !rt.IsCallable(cmp)) rt.ThrowType("toSorted's comparator must be a function");
        ArrThis t = This(rt, a);
        if (t.len > (1u << 26)) rt.ThrowRange("toSorted: the array is too big");
        Runtime::RootedVector items(rt), tmp(rt);
        std::uint64_t undefs = 0;
        for (std::uint64_t k = 0; k < t.len; ++k) {
            rt.Charge(1);
            Value e = GetAt(rt, t.v, k);
            if (e.IsUndef()) ++undefs;
            else items.v.push_back(e);
        }
        MergeSort(rt, items.v, tmp.v, cmp);
        ArrayObj* out = rt.NewArray((std::size_t)t.len);
        out->elems = items.v;
        for (std::uint64_t u = 0; u < undefs; ++u) out->elems.push_back(Value());
        out->length = (std::uint32_t)out->elems.size();
        return Value::Obj(out);
    }, 1);
}

} // namespace okay::js
