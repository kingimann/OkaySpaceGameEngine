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
#include <cstring>
#include <limits>

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
    DefNative(rt, ap, "with", [](Runtime& rt, CallArgs& a) {   // 23.1.3.39 Array.prototype.with
        ArrThis t = This(rt, a);
        double rel = rt.ToIntegerOrInfinity(a[0]);
        double at = rel >= 0 ? rel : (double)t.len + rel;
        if (at >= (double)t.len || at < 0) rt.ThrowRange("with: the index is out of range");
        if (t.len > (1u << 26)) rt.ThrowRange("with: the array is too big");
        ArrayObj* out = rt.NewArray((std::size_t)t.len);
        for (std::uint64_t k = 0; k < t.len; ++k) {
            rt.Charge(1);
            rt.DefineIndex(out, (std::uint32_t)k, k == (std::uint64_t)at ? a[1] : GetAt(rt, t.v, k));
        }
        return Value::Obj(out);
    }, 2);
    DefNative(rt, ap, "toSpliced", [](Runtime& rt, CallArgs& a) {   // 23.1.3.35 Array.prototype.toSpliced
        ArrThis t = This(rt, a);
        std::int64_t start = RelIndex(rt, a[0], (std::int64_t)t.len, 0);
        std::int64_t skip = a.argc == 0 ? 0 : a.argc == 1 ? (std::int64_t)t.len - start
                          : (std::int64_t)std::min(std::max(rt.ToIntegerOrInfinity(a[1]), 0.0), (double)((std::int64_t)t.len - start));
        int inserts = a.argc > 2 ? a.argc - 2 : 0;
        std::uint64_t newLen = t.len - (std::uint64_t)skip + (std::uint64_t)inserts;
        if (newLen > (1u << 26)) rt.ThrowRange("toSpliced: the array is too big");
        ArrayObj* out = rt.NewArray((std::size_t)newLen);
        std::uint32_t w = 0;
        for (std::int64_t k = 0; k < start; ++k, ++w) { rt.Charge(1); rt.DefineIndex(out, w, GetAt(rt, t.v, (std::uint64_t)k)); }
        for (int k = 0; k < inserts; ++k, ++w) rt.DefineIndex(out, w, a[2 + k]);
        for (std::uint64_t k = (std::uint64_t)(start + skip); k < t.len; ++k, ++w) { rt.Charge(1); rt.DefineIndex(out, w, GetAt(rt, t.v, k)); }
        return Value::Obj(out);
    }, 2);
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


// ============================================================ binary data
// Written from ECMA-262 25.1 (ArrayBuffer), 25.2 (SharedArrayBuffer), 23.2 (TypedArray objects), 10.4.5
// (integer-indexed exotic objects), 25.3 (DataView) and 25.4 (Atomics), in our own structure. Our choices where the
// spec leaves them to the implementation: elements are stored little-endian (the [[LittleEndian]] of 25.1.3.1);
// buffers are fixed-length (no resizable or growable buffers, so no length-tracking views); a buffer is at most
// 1 GiB and must fit in the heap limit, checked before any byte is allocated. Every element access goes through
// ElemPtr, which checks the index against the bytes the buffer has now, so a view of a detached buffer (or a
// method whose callback detached it) reads undefined and writes nothing instead of touching freed memory.

namespace {

constexpr std::uint64_t kMaxBufferBytes = 1ull << 30;
const std::uint8_t kElemSize[kTATypes] = {1, 1, 1, 2, 2, 4, 4, 4, 8, 8, 8};
const char* const kTAName[kTATypes] = {"Int8Array", "Uint8Array", "Uint8ClampedArray", "Int16Array", "Uint16Array",
                                       "Int32Array", "Uint32Array", "Float32Array", "Float64Array", "BigInt64Array",
                                       "BigUint64Array"};

// 7.1.6-7.1.11: the integer conversions, as the low bits of the mathematical integer (two's complement)
std::uint32_t ModU32(double d) {
    if (!std::isfinite(d)) return 0;
    double t = std::fmod(std::trunc(d), 4294967296.0);
    if (t < 0) t += 4294967296.0;
    return (std::uint32_t)t;
}
// 7.1.12 ToUint8Clamp: round half to even
std::uint8_t Clamp8(double d) {
    if (std::isnan(d) || d <= 0) return 0;
    if (d >= 255) return 255;
    const double f = std::floor(d);
    if (f + 0.5 < d) return (std::uint8_t)(f + 1);
    if (d < f + 0.5) return (std::uint8_t)f;
    return (std::uint8_t)(((std::uint32_t)f & 1) ? f + 1 : f);
}
// a double rounded to float without the undefined behaviour of an out-of-range conversion (6.1.6.1 rounding:
// past FLT_MAX by half a unit or more is infinity)
float ToF32(double d) {
    if (std::isnan(d)) return std::numeric_limits<float>::quiet_NaN();
    const double big = 3.4028235677973366e38;   // 2^128 - 2^103: the first value that rounds to infinity
    if (d >= big) return std::numeric_limits<float>::infinity();
    if (d <= -big) return -std::numeric_limits<float>::infinity();
    if (d > (double)std::numeric_limits<float>::max()) return std::numeric_limits<float>::max();
    if (d < -(double)std::numeric_limits<float>::max()) return -std::numeric_limits<float>::max();
    return (float)d;
}
// the low 64 bits of a BigInt in two's complement (7.1.15 ToBigInt64 / ToBigUint64 before the sign is read)
std::uint64_t BigLow64(const Value& b) {
    const BigNum& n = static_cast<BigIntV*>(b.p)->v;
    std::uint64_t lo = n.mag.empty() ? 0 : n.mag[0];
    if (n.mag.size() > 1) lo |= (std::uint64_t)n.mag[1] << 32;
    return n.neg ? (~lo + 1) : lo;
}
Value BigFrom64(Runtime& rt, std::uint64_t bits, bool isSigned) {
    bool neg = isSigned && (bits >> 63);
    std::uint64_t mag = neg ? (~bits + 1) : bits;
    return NewBigValue(rt, neg, Limbs{(std::uint32_t)mag, (std::uint32_t)(mag >> 32)});
}

// 25.1.3.17 RawBytesToNumeric / 25.1.3.18 NumericToRawBytes (the value is already a Number or a BigInt)
Value RawToValue(Runtime& rt, TAType ty, const std::uint8_t* p, bool little) {
    std::uint8_t b[8];
    const int n = kElemSize[(int)ty];
    for (int i = 0; i < n; ++i) b[i] = little ? p[i] : p[n - 1 - i];
    std::uint64_t u = 0;
    for (int i = n - 1; i >= 0; --i) u = (u << 8) | b[i];
    switch (ty) {
        case TAType::I8: return Value::Num((double)(std::int8_t)(std::uint8_t)u);
        case TAType::U8: case TAType::U8C: return Value::Num((double)(std::uint8_t)u);
        case TAType::I16: return Value::Num((double)(std::int16_t)(std::uint16_t)u);
        case TAType::U16: return Value::Num((double)(std::uint16_t)u);
        case TAType::I32: return Value::Num((double)(std::int32_t)(std::uint32_t)u);
        case TAType::U32: return Value::Num((double)(std::uint32_t)u);
        case TAType::F32: { std::uint32_t w = (std::uint32_t)u; float f; std::memcpy(&f, &w, 4); return Value::Num((double)f); }
        case TAType::F64: { double d; std::memcpy(&d, &u, 8); return Value::Num(d); }
        case TAType::I64: return BigFrom64(rt, u, true);
        case TAType::U64: return BigFrom64(rt, u, false);
        default: return Value();
    }
}
void ValueToRaw(TAType ty, const Value& v, std::uint8_t* p, bool little) {
    std::uint64_t u = 0;
    switch (ty) {
        case TAType::U8C: u = Clamp8(v.n); break;
        case TAType::F32: { float f = ToF32(v.n); std::uint32_t w; std::memcpy(&w, &f, 4); u = w; break; }
        case TAType::F64: std::memcpy(&u, &v.n, 8); break;
        case TAType::I64: case TAType::U64: u = BigLow64(v); break;
        default: u = ModU32(v.n); break;   // the narrower types keep the low bits
    }
    const int n = kElemSize[(int)ty];
    for (int i = 0; i < n; ++i) {
        const std::uint8_t byte = (std::uint8_t)(u >> (8 * i));
        p[little ? i : n - 1 - i] = byte;
    }
}
// ToNumber or ToBigInt as the element type needs (the conversion that may run script code comes first)
Value ToElemValue(Runtime& rt, TAType ty, const Value& v) {
    if (ty == TAType::I64 || ty == TAType::U64) return ToBigIntValue(rt, v);
    return Value::Num(rt.ToNumber(v));
}

// The element's bytes when index i is valid now, or nullptr.
std::uint8_t* ElemPtr(TypedArrayObj* t, std::uint64_t i) {
    if (t->buf->detached || i >= t->length) return nullptr;
    const std::uint64_t size = kElemSize[(int)t->type];
    const std::uint64_t at = t->offset + i * size;
    if (at + size > t->buf->data.size()) return nullptr;
    return t->buf->data.data() + at;
}
Value GetElem(Runtime& rt, TypedArrayObj* t, std::uint64_t i) {
    std::uint8_t* p = ElemPtr(t, i);
    return p ? RawToValue(rt, t->type, p, true) : Value();
}
// write a value already converted by ToElemValue (no script code runs here)
void PutElem(TypedArrayObj* t, std::uint64_t i, const Value& num) {
    if (std::uint8_t* p = ElemPtr(t, i)) ValueToRaw(t->type, num, p, true);
}

// 7.1.22 ToIndex
std::uint64_t ToIndexV(Runtime& rt, const Value& v, const char* what) {
    const double d = rt.ToIntegerOrInfinity(v);
    if (d < 0 || d > 9007199254740991.0) rt.ThrowRange(std::string(what) + " must be a whole number from 0 to 2^53-1");
    return (std::uint64_t)d;
}

Object* ProtoFrom(Runtime& rt, Object* newTarget, Object* dflt) {   // 10.1.14 GetPrototypeFromConstructor
    if (!newTarget) return dflt;
    Value pr = rt.Get(Value::Obj(newTarget), rt.atoms->prototype);
    return pr.IsObj() ? pr.O() : dflt;
}

// 25.1.3.1 AllocateArrayBuffer: the size is checked against our limits before anything is allocated
BufferObj* NewBuffer(Runtime& rt, std::uint64_t bytes, Object* proto, bool shared) {
    const std::size_t used = rt.BytesInUse();
    const std::size_t room = rt.memoryLimit > used ? rt.memoryLimit - used : 0;
    if (bytes > kMaxBufferBytes || bytes > room)
        rt.ThrowRange("can not allocate " + std::to_string(bytes) + " bytes for an " + (shared ? "SharedArrayBuffer" : "ArrayBuffer"));
    BufferObj* b = rt.Alloc<BufferObj>((std::size_t)bytes);
    b->shared = shared;
    b->proto = proto ? proto : Intrinsic(rt, shared ? kSharedBufferProto : kArrayBufferProto);
    b->data.assign((std::size_t)bytes, 0);
    return b;
}

BufferObj* ThisBuffer(Runtime& rt, const Value& v, bool shared, const char* what) {
    if (!v.IsObj() || v.O()->cls != Cls::ArrayBuffer || static_cast<BufferObj*>(v.O())->shared != shared)
        rt.ThrowType(std::string(what) + " needs " + (shared ? "a SharedArrayBuffer" : "an ArrayBuffer") + " as 'this'");
    return static_cast<BufferObj*>(v.O());
}

TypedArrayObj* NewTA(Runtime& rt, TAType ty, BufferObj* buf, std::uint64_t offset, std::uint64_t length, Object* proto) {
    TypedArrayObj* t = rt.Alloc<TypedArrayObj>();
    t->type = ty;
    t->buf = buf;
    t->offset = offset;
    t->length = length;
    t->proto = proto ? proto : Intrinsic(rt, kTypedProtos + (int)ty);
    return t;
}
// 23.2.5.1.6 AllocateTypedArray with a length: a new buffer, the size checked first
TypedArrayObj* AllocTA(Runtime& rt, TAType ty, std::uint64_t length, Object* proto) {
    if (length > kMaxBufferBytes / kElemSize[(int)ty]) rt.ThrowRange(std::string("a ") + kTAName[(int)ty] + " of " + std::to_string(length) + " elements is too big");
    BufferObj* b = NewBuffer(rt, length * kElemSize[(int)ty], nullptr, false);
    rt.Keep(b);
    return NewTA(rt, ty, b, 0, length, proto);
}

// 23.2.4.4 ValidateTypedArray: a typed array whose buffer is not detached
TypedArrayObj* ValidTA(Runtime& rt, const Value& v, const char* what) {
    if (!v.IsObj() || v.O()->cls != Cls::TypedArray) rt.ThrowType(std::string("%TypedArray%.prototype.") + what + " needs a typed array as 'this'");
    TypedArrayObj* t = static_cast<TypedArrayObj*>(v.O());
    if (t->OutOfBounds()) rt.ThrowType(std::string("%TypedArray%.prototype.") + what + ": the typed array's buffer is detached");
    return t;
}
TypedArrayObj* AsTA(const Value& v) {
    return v.IsObj() && v.O()->cls == Cls::TypedArray ? static_cast<TypedArrayObj*>(v.O()) : nullptr;
}

// 23.2.4.2 TypedArrayCreateFromConstructor: the result must be a valid typed array, long enough for a one-number call
TypedArrayObj* CreateFromCtor(Runtime& rt, const Value& ctor, const Value* args, int argc) {
    Value r = rt.Construct(ctor, args, argc);
    TypedArrayObj* t = AsTA(r);
    if (!t || t->OutOfBounds()) rt.ThrowType("the species constructor did not make a typed array");
    if (argc == 1 && args[0].IsNum() && (double)t->Length() < args[0].n) rt.ThrowType("the species constructor made a typed array that is too short");
    return t;
}
// 23.2.4.1 TypedArraySpeciesCreate (7.3.22 SpeciesConstructor with the exemplar's own constructor as the default)
TypedArrayObj* SpeciesCreate(Runtime& rt, TypedArrayObj* ex, const Value* args, int argc) {
    Value dflt = Value::Obj(Intrinsic(rt, kTypedCtors + (int)ex->type));
    Value c = rt.Get(Value::Obj(ex), rt.atoms->constructor);
    Value ctor = dflt;
    if (!c.IsUndef()) {
        if (!c.IsObj()) rt.ThrowType("the typed array's constructor is not an object");
        Value sp = rt.Get(c, rt.symSpecies);
        if (!sp.IsNullish()) {
            if (!rt.IsConstructor(sp)) rt.ThrowType("[Symbol.species] is not a constructor");
            ctor = sp;
        }
    }
    if (ctor.IsObj() && ctor.O() == dflt.O()) {   // the plain case, without a script call
        if (argc == 1 && args[0].IsNum()) return AllocTA(rt, ex->type, (std::uint64_t)args[0].n, nullptr);
    }
    rt.Keep(ctor);
    TypedArrayObj* r = CreateFromCtor(rt, ctor, args, argc);
    if (r->IsBigInt() != ex->IsBigInt()) rt.ThrowType("the species constructor made a typed array of the other content type");
    return r;
}

// the relative index argument of slice / fill / copyWithin / subarray, clamped into [0, len]
std::uint64_t Rel(Runtime& rt, const Value& v, std::uint64_t len, std::uint64_t dflt) {
    if (v.IsUndef()) return dflt;
    double d = rt.ToIntegerOrInfinity(v);
    if (d < 0) d = std::max(0.0, (double)len + d);
    else d = std::min(d, (double)len);
    return (std::uint64_t)d;
}

Value Fn(Runtime& rt, const Value& f, const char* what) {
    if (!rt.IsCallable(f)) rt.ThrowType(std::string("%TypedArray%.prototype.") + what + " needs a function");
    return f;
}

// 23.2.4.7 TypedArray SortCompare (no comparator): numeric order, -0 before +0, NaN last
bool NumLess(const Value& x, const Value& y) {
    if (x.IsBig()) return BigCompare(x, y) < 0;
    const double a = x.n, b = y.n;
    if (std::isnan(a)) return false;
    if (std::isnan(b)) return true;
    if (a < b) return true;
    if (a > b) return false;
    return a == 0 && std::signbit(a) && !std::signbit(b);
}
// a stable merge sort over a rooted copy; the comparator may run any script code
void SortValues(Runtime& rt, std::vector<Value>& v, const Value& cmp) {
    // every value stays in `v` (the caller's rooted vector) while a pass merges into tmp, so tmp needs no rooting
    std::vector<Value> tmp(v.size());
    auto less = [&](const Value& x, const Value& y) -> bool {
        rt.Charge(1);
        if (cmp.IsUndef()) return NumLess(x, y);
        Value args[2] = {x, y};
        Runtime::ScratchScope ss(rt);
        const double r = rt.ToNumber(rt.Call(cmp, Value(), args, 2));
        return r < 0;
    };
    for (std::size_t w = 1; w < v.size(); w *= 2) {
        for (std::size_t lo = 0; lo < v.size(); lo += 2 * w) {
            const std::size_t mid = std::min(lo + w, v.size()), hi = std::min(lo + 2 * w, v.size());
            std::size_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi) tmp[k++] = less(v[j], v[i]) ? v[j++] : v[i++];
            while (i < mid) tmp[k++] = v[i++];
            while (j < hi) tmp[k++] = v[j++];
        }
        v.swap(tmp);
    }
}

Value MakeTA(Runtime& rt, CallArgs& a, TAType ty);

// 23.2.5.1.2-5: from another typed array, a buffer, an iterable or an array-like
Value MakeTA(Runtime& rt, CallArgs& a, TAType ty) {
    if (!a.newTarget) rt.ThrowType(std::string(kTAName[(int)ty]) + " must be called with 'new'");
    Object* proto = ProtoFrom(rt, a.newTarget, Intrinsic(rt, kTypedProtos + (int)ty));
    const std::uint64_t esize = kElemSize[(int)ty];
    const bool big = ty == TAType::I64 || ty == TAType::U64;
    Value first = a[0];
    if (!first.IsObj()) return Value::Obj(AllocTA(rt, ty, ToIndexV(rt, first, "a typed array's length"), proto));
    Object* o = first.O();
    if (o->cls == Cls::ArrayBuffer) {   // 23.2.5.1.3 InitializeTypedArrayFromArrayBuffer
        BufferObj* b = static_cast<BufferObj*>(o);
        const std::uint64_t off = ToIndexV(rt, a[1], "byteOffset");
        if (off % esize) rt.ThrowRange("byteOffset must be a multiple of " + std::to_string(esize));
        std::uint64_t newLen = 0;
        const bool hasLen = !a[2].IsUndef();
        if (hasLen) newLen = ToIndexV(rt, a[2], "length");
        if (b->detached) rt.ThrowType("the ArrayBuffer is detached");
        const std::uint64_t bufLen = b->data.size();
        std::uint64_t bytes;
        if (!hasLen) {
            if (bufLen % esize) rt.ThrowRange("the buffer's length must be a multiple of " + std::to_string(esize));
            if (off > bufLen) rt.ThrowRange("byteOffset is past the end of the buffer");
            bytes = bufLen - off;
        } else {
            if (newLen > kMaxBufferBytes) rt.ThrowRange("length is past the end of the buffer");
            bytes = newLen * esize;
            if (off > bufLen || bytes > bufLen - off) rt.ThrowRange("offset + length is past the end of the buffer");
        }
        return Value::Obj(NewTA(rt, ty, b, off, bytes / esize, proto));
    }
    if (TypedArrayObj* src = AsTA(first)) {   // 23.2.5.1.2 InitializeTypedArrayFromTypedArray
        if (src->OutOfBounds()) rt.ThrowType("the source typed array's buffer is detached");
        if (src->IsBigInt() != big) rt.ThrowType("can not mix BigInt and Number typed arrays");
        const std::uint64_t n = src->Length();
        TypedArrayObj* t = AllocTA(rt, ty, n, proto);
        rt.Charge(n);
        for (std::uint64_t i = 0; i < n; ++i) PutElem(t, i, GetElem(rt, src, i));
        return Value::Obj(t);
    }
    // 23.2.5.1.4 InitializeTypedArrayFromList / 23.2.5.1.5 InitializeTypedArrayFromArrayLike
    const std::uint64_t cap = std::min<std::uint64_t>(1ull << 26, rt.memoryLimit / sizeof(Value));
    Value usingIter = rt.Get(first, rt.symIterator);
    Runtime::RootedVector vals(rt);
    if (!usingIter.IsNullish()) {
        if (!rt.IsCallable(usingIter)) rt.ThrowType("[Symbol.iterator] is not a function");
        IterRec* r = GetIterator(rt, first);
        rt.Keep(r);
        Value e;
        while (IterStep(rt, r, e)) {
            if (vals.v.size() >= cap) rt.ThrowRange("too many values for a typed array");
            vals.v.push_back(e);
        }
    } else {
        const std::uint64_t n = rt.LengthOf(first);
        if (n > cap || n > kMaxBufferBytes / esize) rt.ThrowRange("an array-like of " + std::to_string(n) + " elements is too big for a typed array");
        for (std::uint64_t i = 0; i < n; ++i) {
            rt.Charge(1);
            vals.v.push_back(GetAt(rt, first, i));
        }
    }
    TypedArrayObj* t = AllocTA(rt, ty, vals.v.size(), proto);
    rt.Keep(t);
    for (std::size_t i = 0; i < vals.v.size(); ++i) {
        Runtime::ScratchScope ss(rt);
        PutElem(t, i, ToElemValue(rt, ty, vals.v[i]));
    }
    return Value::Obj(t);
}

template <int T> Value TACtor(Runtime& rt, CallArgs& a) { return MakeTA(rt, a, (TAType)T); }
const NativeFn kTACtors[kTATypes] = {TACtor<0>, TACtor<1>, TACtor<2>, TACtor<3>, TACtor<4>, TACtor<5>,
                                     TACtor<6>, TACtor<7>, TACtor<8>, TACtor<9>, TACtor<10>};

// 25.3.1.5 GetViewValue / 25.3.1.6 SetViewValue
DataViewObj* ThisView(Runtime& rt, const Value& v, const char* what) {
    if (!v.IsObj() || v.O()->cls != Cls::DataView) rt.ThrowType(std::string("DataView.prototype.") + what + " needs a DataView as 'this'");
    return static_cast<DataViewObj*>(v.O());
}
Value ViewGet(Runtime& rt, CallArgs& a, TAType ty, const char* what) {
    DataViewObj* d = ThisView(rt, a.thisv, what);
    const std::uint64_t at = ToIndexV(rt, a[0], "the byte offset");
    const bool little = rt.ToBoolean(a[1]);
    if (d->buf->detached) rt.ThrowType(std::string("DataView.prototype.") + what + ": the buffer is detached");
    const std::uint64_t n = kElemSize[(int)ty];
    if (at > d->length || n > d->length - at) rt.ThrowRange(std::string("DataView.prototype.") + what + ": the offset is outside the view");
    const std::uint64_t pos = d->offset + at;
    if (pos + n > d->buf->data.size()) rt.ThrowRange(std::string("DataView.prototype.") + what + ": the offset is outside the buffer");
    return RawToValue(rt, ty, d->buf->data.data() + pos, little);
}
Value ViewSet(Runtime& rt, CallArgs& a, TAType ty, const char* what) {
    DataViewObj* d = ThisView(rt, a.thisv, what);
    const std::uint64_t at = ToIndexV(rt, a[0], "the byte offset");
    Value num = ToElemValue(rt, ty, a[1]);
    const bool little = rt.ToBoolean(a[2]);
    if (d->buf->detached) rt.ThrowType(std::string("DataView.prototype.") + what + ": the buffer is detached");
    const std::uint64_t n = kElemSize[(int)ty];
    if (at > d->length || n > d->length - at) rt.ThrowRange(std::string("DataView.prototype.") + what + ": the offset is outside the view");
    const std::uint64_t pos = d->offset + at;
    if (pos + n > d->buf->data.size()) rt.ThrowRange(std::string("DataView.prototype.") + what + ": the offset is outside the buffer");
    ValueToRaw(ty, num, d->buf->data.data() + pos, little);
    return Value();
}
const char* const kViewGet[kTATypes] = {"getInt8", "getUint8", "", "getInt16", "getUint16", "getInt32", "getUint32",
                                         "getFloat32", "getFloat64", "getBigInt64", "getBigUint64"};
const char* const kViewSet[kTATypes] = {"setInt8", "setUint8", "", "setInt16", "setUint16", "setInt32", "setUint32",
                                         "setFloat32", "setFloat64", "setBigInt64", "setBigUint64"};
template <int T> Value ViewGetT(Runtime& rt, CallArgs& a) { return ViewGet(rt, a, (TAType)T, kViewGet[T]); }
template <int T> Value ViewSetT(Runtime& rt, CallArgs& a) { return ViewSet(rt, a, (TAType)T, kViewSet[T]); }
const NativeFn kViewGetFns[kTATypes] = {ViewGetT<0>, ViewGetT<1>, nullptr, ViewGetT<3>, ViewGetT<4>, ViewGetT<5>,
                                        ViewGetT<6>, ViewGetT<7>, ViewGetT<8>, ViewGetT<9>, ViewGetT<10>};
const NativeFn kViewSetFns[kTATypes] = {ViewSetT<0>, ViewSetT<1>, nullptr, ViewSetT<3>, ViewSetT<4>, ViewSetT<5>,
                                        ViewSetT<6>, ViewSetT<7>, ViewSetT<8>, ViewSetT<9>, ViewSetT<10>};

// 25.4.3.1 ValidateIntegerTypedArray and 25.4.3.2 ValidateAtomicAccess
TypedArrayObj* AtomicTA(Runtime& rt, const Value& v, bool waitable) {
    TypedArrayObj* t = AsTA(v);
    if (!t) rt.ThrowType("Atomics needs an integer typed array");
    if (t->OutOfBounds()) rt.ThrowType("Atomics: the typed array's buffer is detached");
    const TAType ty = t->type;
    const bool ok = waitable ? (ty == TAType::I32 || ty == TAType::I64)
                             : (ty != TAType::F32 && ty != TAType::F64 && ty != TAType::U8C);
    if (!ok) rt.ThrowType("Atomics does not work on a " + std::string(kTAName[(int)ty]));
    return t;
}
std::uint64_t AtomicIndex(Runtime& rt, TypedArrayObj* t, const Value& v) {
    const std::uint64_t i = ToIndexV(rt, v, "the index");
    if (i >= t->Length()) rt.ThrowRange("Atomics: the index is outside the typed array");
    return i;
}
// 25.4.3.17 AtomicReadModifyWrite: there is one thread, so a plain read and write is atomic here
Value AtomicRMW(Runtime& rt, CallArgs& a, int op) {
    TypedArrayObj* t = AtomicTA(rt, a[0], false);
    const std::uint64_t i = AtomicIndex(rt, t, a[1]);
    Value v = ToElemValue(rt, t->type, a[2]);
    rt.Keep(v);
    if (t->OutOfBounds()) rt.ThrowType("Atomics: the typed array's buffer is detached");
    Value old = GetElem(rt, t, i);
    rt.Keep(old);
    Value nv;
    if (op == 0) nv = v;   // exchange
    else if (t->IsBigInt()) {
        static const int kOps[] = {0, OP_Add, OP_Sub, OP_BitAnd, OP_BitOr, OP_BitXor};
        nv = BigBinary(rt, kOps[op], old, v);
    } else {
        const std::uint32_t x = ModU32(old.n), y = ModU32(v.n);
        std::uint32_t r = 0;
        switch (op) {
            case 1: r = x + y; break;
            case 2: r = x - y; break;
            case 3: r = x & y; break;
            case 4: r = x | y; break;
            default: r = x ^ y; break;
        }
        nv = Value::Num((double)r);
    }
    PutElem(t, i, nv);
    return old;
}

} // namespace

void TypedArrayObj::Trace(Runtime& rt) {
    Object::Trace(rt);
    if (buf) rt.Mark(buf);
}
void DataViewObj::Trace(Runtime& rt) {
    Object::Trace(rt);
    if (buf) rt.Mark(buf);
}
std::uint32_t TypedArrayObj::ElemSize() const { return kElemSize[(int)type]; }
bool TypedArrayObj::OutOfBounds() const {
    if (buf->detached) return true;
    const std::uint64_t end = offset + length * kElemSize[(int)type];
    return end > buf->data.size();
}

bool CanonicalNumericKey(Runtime& rt, GcObj* key, double& n) {
    if (key->kind != Kind::String) return false;
    String* s = static_cast<String*>(key);
    const std::int64_t idx = s->ArrayIndex();
    if (idx >= 0) {
        n = (double)idx;
        return true;
    }
    const std::u16string& t = s->s;
    if (t.empty() || t.size() > 32) return false;
    const char16_t c = t[0];
    if (!(c == u'-' || c == u'I' || c == u'N' || (c >= u'0' && c <= u'9'))) return false;
    if (t == u"-0") {
        n = -0.0;
        return true;
    }
    const double d = Runtime::StringToNumber(t);
    if (rt.NumberToU16(d) != t) return false;
    n = d;
    return true;
}
bool ValidIntegerIndex(const TypedArrayObj* t, double n) {
    if (t->OutOfBounds()) return false;
    if (!(n >= 0) || n != std::floor(n) || (n == 0 && std::signbit(n))) return false;
    return n < (double)t->length;
}
Value TypedArrayGet(Runtime& rt, TypedArrayObj* t, double n) {
    if (!ValidIntegerIndex(t, n)) return Value();
    return GetElem(rt, t, (std::uint64_t)n);
}
void TypedArraySet(Runtime& rt, TypedArrayObj* t, double n, const Value& v) {
    Value num = ToElemValue(rt, t->type, v);
    if (ValidIntegerIndex(t, n)) PutElem(t, (std::uint64_t)n, num);
}

TypedArrayObj* NewUint8Array(Runtime& rt, const std::uint8_t* bytes, std::size_t n) {
    TypedArrayObj* t = AllocTA(rt, TAType::U8, n, nullptr);
    if (n) std::memcpy(t->buf->data.data(), bytes, n);
    return t;
}
BufferObj* NewBufferCopy(Runtime& rt, const std::uint8_t* bytes, std::size_t n) {
    BufferObj* b = NewBuffer(rt, n, nullptr, false);
    if (n) std::memcpy(b->data.data(), bytes, n);
    return b;
}
TypedArrayObj* NewTypedArrayOn(Runtime& rt, TAType type, BufferObj* buf, std::uint64_t offset, std::uint64_t length) {
    return NewTA(rt, type, buf, offset, length, nullptr);
}
DataViewObj* NewDataViewOn(Runtime& rt, BufferObj* buf, std::uint64_t offset, std::uint64_t length) {
    DataViewObj* d = rt.Alloc<DataViewObj>();
    d->buf = buf;
    d->offset = offset;
    d->length = length;
    d->proto = Intrinsic(rt, kDataViewProto);
    return d;
}

std::string TypedArrayDisplay(Runtime& rt, Object* o, int depth) {
    TypedArrayObj* t = static_cast<TypedArrayObj*>(o);
    const std::uint64_t n = t->Length();
    std::string out = std::string(kTAName[(int)t->type]) + "(" + std::to_string(n) + ") [";
    for (std::uint64_t i = 0; i < n && i < 100; ++i) out += (i ? ", " : " ") + rt.Display(GetElem(rt, t, i), depth + 1);
    if (n > 100) out += ", ... " + std::to_string(n - 100) + " more";
    return out + (n ? " ]" : "]");
}

void InstallBinaryLib(Runtime& rt) {
    Object* g = rt.global;
    // ---- 25.1 ArrayBuffer
    Object* abp = rt.NewObject();
    Intrinsic(rt, kArrayBufferProto) = abp;
    NativeFunc* abc = DefCtor(rt, "ArrayBuffer", [](Runtime& rt, CallArgs& a) {
        if (!a.newTarget) rt.ThrowType("ArrayBuffer must be called with 'new'");
        Object* proto = ProtoFrom(rt, a.newTarget, Intrinsic(rt, kArrayBufferProto));
        return Value::Obj(NewBuffer(rt, ToIndexV(rt, a[0], "an ArrayBuffer's length"), proto, false));
    }, 1, abp);
    DefNative(rt, abc, "isView", [](Runtime&, CallArgs& a) {
        return Value::Bool(a[0].IsObj() && (a[0].O()->cls == Cls::TypedArray || a[0].O()->cls == Cls::DataView));
    }, 1);
    auto speciesGetter = [](Runtime&, CallArgs& a) { return a.thisv; };
    {
        NativeFunc* f = rt.NewNative(speciesGetter, "get [Symbol.species]", 0);
        Prop* p = abc->AddRaw(rt.symSpecies, Value(), kConfigurable | kAccessor);
        p->get = f;
    }
    DefGetter(rt, abp, "byteLength", [](Runtime& rt, CallArgs& a) {
        return Value::Num((double)ThisBuffer(rt, a.thisv, false, "ArrayBuffer.prototype.byteLength")->data.size());
    });
    DefGetter(rt, abp, "maxByteLength", [](Runtime& rt, CallArgs& a) {
        return Value::Num((double)ThisBuffer(rt, a.thisv, false, "ArrayBuffer.prototype.maxByteLength")->data.size());
    });
    DefGetter(rt, abp, "resizable", [](Runtime& rt, CallArgs& a) {
        ThisBuffer(rt, a.thisv, false, "ArrayBuffer.prototype.resizable");
        return Value::Bool(false);
    });
    DefGetter(rt, abp, "detached", [](Runtime& rt, CallArgs& a) {
        return Value::Bool(ThisBuffer(rt, a.thisv, false, "ArrayBuffer.prototype.detached")->detached);
    });
    DefNative(rt, abp, "slice", [](Runtime& rt, CallArgs& a) {   // 25.1.6.7 (a plain new buffer: species is not consulted)
        BufferObj* b = ThisBuffer(rt, a.thisv, false, "ArrayBuffer.prototype.slice");
        if (b->detached) rt.ThrowType("ArrayBuffer.prototype.slice: the buffer is detached");
        const std::uint64_t len = b->data.size();
        const std::uint64_t first = Rel(rt, a[0], len, 0), fin = Rel(rt, a[1], len, len);
        const std::uint64_t n = fin > first ? fin - first : 0;
        BufferObj* r = NewBuffer(rt, n, nullptr, false);
        if (b->detached) rt.ThrowType("ArrayBuffer.prototype.slice: the buffer is detached");
        rt.Charge(n / 64 + 1);
        if (n && first + n <= b->data.size()) std::memcpy(r->data.data(), b->data.data() + first, (std::size_t)n);
        return Value::Obj(r);
    }, 2);
    // 25.1.6.9-10 transfer / transferToFixedLength (25.1.3.3 ArrayBufferCopyAndDetach): the bytes move, the old one is detached
    auto transfer = [](Runtime& rt, CallArgs& a) {
        BufferObj* b = ThisBuffer(rt, a.thisv, false, "ArrayBuffer.prototype.transfer");
        std::uint64_t newLen = a[0].IsUndef() ? (std::uint64_t)b->data.size() : ToIndexV(rt, a[0], "the new length");
        if (b->detached) rt.ThrowType("ArrayBuffer.prototype.transfer: the buffer is detached");
        BufferObj* r = NewBuffer(rt, 0, nullptr, false);
        if (newLen > kMaxBufferBytes) rt.ThrowRange("can not allocate " + std::to_string(newLen) + " bytes for an ArrayBuffer");
        if (newLen > b->data.size()) {
            const std::size_t used = rt.BytesInUse(), room = rt.memoryLimit > used ? rt.memoryLimit - used : 0;
            if (newLen - b->data.size() > room) rt.ThrowRange("can not allocate " + std::to_string(newLen) + " bytes for an ArrayBuffer");
        }
        r->data.swap(b->data);
        r->data.resize((std::size_t)newLen, 0);
        b->data.clear();
        b->data.shrink_to_fit();
        b->detached = true;
        return Value::Obj(r);
    };
    DefNative(rt, abp, "transfer", transfer, 0);
    DefNative(rt, abp, "transferToFixedLength", transfer, 0);
    abp->AddRaw(rt.symToStringTag, Value::Str(rt.Intern("ArrayBuffer")), kConfigurable);

    // ---- 25.2 SharedArrayBuffer (no other thread can see it: a plain buffer that is never detached)
    Object* sbp = rt.NewObject();
    Intrinsic(rt, kSharedBufferProto) = sbp;
    NativeFunc* sbc = DefCtor(rt, "SharedArrayBuffer", [](Runtime& rt, CallArgs& a) {
        if (!a.newTarget) rt.ThrowType("SharedArrayBuffer must be called with 'new'");
        Object* proto = ProtoFrom(rt, a.newTarget, Intrinsic(rt, kSharedBufferProto));
        return Value::Obj(NewBuffer(rt, ToIndexV(rt, a[0], "a SharedArrayBuffer's length"), proto, true));
    }, 1, sbp);
    {
        NativeFunc* f = rt.NewNative(speciesGetter, "get [Symbol.species]", 0);
        Prop* p = sbc->AddRaw(rt.symSpecies, Value(), kConfigurable | kAccessor);
        p->get = f;
    }
    DefGetter(rt, sbp, "byteLength", [](Runtime& rt, CallArgs& a) {
        return Value::Num((double)ThisBuffer(rt, a.thisv, true, "SharedArrayBuffer.prototype.byteLength")->data.size());
    });
    DefGetter(rt, sbp, "maxByteLength", [](Runtime& rt, CallArgs& a) {
        return Value::Num((double)ThisBuffer(rt, a.thisv, true, "SharedArrayBuffer.prototype.maxByteLength")->data.size());
    });
    DefGetter(rt, sbp, "growable", [](Runtime& rt, CallArgs& a) {
        ThisBuffer(rt, a.thisv, true, "SharedArrayBuffer.prototype.growable");
        return Value::Bool(false);
    });
    DefNative(rt, sbp, "slice", [](Runtime& rt, CallArgs& a) {
        BufferObj* b = ThisBuffer(rt, a.thisv, true, "SharedArrayBuffer.prototype.slice");
        const std::uint64_t len = b->data.size();
        const std::uint64_t first = Rel(rt, a[0], len, 0), fin = Rel(rt, a[1], len, len);
        const std::uint64_t n = fin > first ? fin - first : 0;
        BufferObj* r = NewBuffer(rt, n, nullptr, true);
        rt.Charge(n / 64 + 1);
        if (n && first + n <= b->data.size()) std::memcpy(r->data.data(), b->data.data() + first, (std::size_t)n);
        return Value::Obj(r);
    }, 2);
    sbp->AddRaw(rt.symToStringTag, Value::Str(rt.Intern("SharedArrayBuffer")), kConfigurable);

    // ---- 23.2.1-23.2.3 %TypedArray% and its prototype
    Object* tap = rt.NewObject();
    Intrinsic(rt, kTypedArrayProto) = tap;
    NativeFunc* tac = rt.NewNative([](Runtime& rt, CallArgs&) -> Value {
        rt.ThrowType("TypedArray is abstract: use Uint8Array, Float32Array, ...");
    }, "TypedArray", 0);
    tac->ctor = true;
    rt.MakeLazyProps(tac);
    tac->AddRaw(rt.atoms->prototype, Value::Obj(tap), 0);
    tap->AddRaw(rt.atoms->constructor, Value::Obj(tac), kHidden);
    Intrinsic(rt, kTypedArrayCtor) = tac;
    {
        NativeFunc* f = rt.NewNative(speciesGetter, "get [Symbol.species]", 0);
        Prop* p = tac->AddRaw(rt.symSpecies, Value(), kConfigurable | kAccessor);
        p->get = f;
    }
    // 23.2.2.1 %TypedArray%.from / 23.2.2.2 %TypedArray%.of
    DefNative(rt, tac, "from", [](Runtime& rt, CallArgs& a) {
        Value C = a.thisv;
        if (!rt.IsConstructor(C)) rt.ThrowType("TypedArray.from needs a constructor as 'this'");
        Value mapFn = a[1];
        if (!mapFn.IsUndef() && !rt.IsCallable(mapFn)) rt.ThrowType("TypedArray.from's map function is not a function");
        Runtime::RootedVector vals(rt);
        const std::uint64_t cap = std::min<std::uint64_t>(1ull << 26, rt.memoryLimit / sizeof(Value));
        Value src = a[0];
        Value usingIter = src.IsNullish() ? Value() : rt.Get(src, rt.symIterator);
        if (!usingIter.IsNullish()) {
            IterRec* r = GetIterator(rt, src);
            rt.Keep(r);
            Value e;
            while (IterStep(rt, r, e)) {
                if (vals.v.size() >= cap) rt.ThrowRange("too many values for a typed array");
                vals.v.push_back(e);
            }
        } else {
            Object* o = rt.ToObject(src);
            rt.Keep(o);
            const std::uint64_t n = rt.LengthOf(Value::Obj(o));
            if (n > cap) rt.ThrowRange("an array-like of " + std::to_string(n) + " elements is too big for a typed array");
            for (std::uint64_t i = 0; i < n; ++i) {
                rt.Charge(1);
                vals.v.push_back(GetAt(rt, Value::Obj(o), i));
            }
        }
        Value len = Value::Num((double)vals.v.size());
        TypedArrayObj* t = CreateFromCtor(rt, C, &len, 1);
        rt.Keep(t);
        for (std::size_t i = 0; i < vals.v.size(); ++i) {
            Runtime::ScratchScope ss(rt);
            Value v = vals.v[i];
            if (!mapFn.IsUndef()) {
                Value args[2] = {v, Value::Num((double)i)};
                v = rt.Call(mapFn, a[2], args, 2);
            }
            rt.PutIndex(Value::Obj(t), (std::uint32_t)i, v);
        }
        return Value::Obj(t);
    }, 1);
    DefNative(rt, tac, "of", [](Runtime& rt, CallArgs& a) {
        if (!rt.IsConstructor(a.thisv)) rt.ThrowType("TypedArray.of needs a constructor as 'this'");
        Value len = Value::Num(a.argc);
        TypedArrayObj* t = CreateFromCtor(rt, a.thisv, &len, 1);
        rt.Keep(t);
        for (int i = 0; i < a.argc; ++i) rt.PutIndex(Value::Obj(t), (std::uint32_t)i, a[i]);
        return Value::Obj(t);
    }, 0);
    // getters (23.2.3.2-23.2.3.5, 23.2.3.21, 23.2.3.38): 0 once the buffer is detached
    DefGetter(rt, tap, "buffer", [](Runtime& rt, CallArgs& a) {
        TypedArrayObj* t = AsTA(a.thisv);
        if (!t) rt.ThrowType("%TypedArray%.prototype.buffer needs a typed array as 'this'");
        return Value::Obj(t->buf);
    });
    DefGetter(rt, tap, "byteLength", [](Runtime& rt, CallArgs& a) {
        TypedArrayObj* t = AsTA(a.thisv);
        if (!t) rt.ThrowType("%TypedArray%.prototype.byteLength needs a typed array as 'this'");
        return Value::Num((double)(t->Length() * t->ElemSize()));
    });
    DefGetter(rt, tap, "byteOffset", [](Runtime& rt, CallArgs& a) {
        TypedArrayObj* t = AsTA(a.thisv);
        if (!t) rt.ThrowType("%TypedArray%.prototype.byteOffset needs a typed array as 'this'");
        return Value::Num(t->OutOfBounds() ? 0.0 : (double)t->offset);
    });
    DefGetter(rt, tap, "length", [](Runtime& rt, CallArgs& a) {
        TypedArrayObj* t = AsTA(a.thisv);
        if (!t) rt.ThrowType("%TypedArray%.prototype.length needs a typed array as 'this'");
        return Value::Num((double)t->Length());
    });
    {
        NativeFunc* f = rt.NewNative([](Runtime& rt, CallArgs& a) {
            TypedArrayObj* t = AsTA(a.thisv);
            return t ? Value::Str(rt.Intern(kTAName[(int)t->type])) : Value();
        }, "get [Symbol.toStringTag]", 0);
        Prop* p = tap->AddRaw(rt.symToStringTag, Value(), kConfigurable | kAccessor);
        p->get = f;
    }
    DefNative(rt, tap, "at", [](Runtime& rt, CallArgs& a) {
        TypedArrayObj* t = ValidTA(rt, a.thisv, "at");
        const double len = (double)t->Length();
        double k = rt.ToIntegerOrInfinity(a[0]);
        if (k < 0) k += len;
        if (k < 0 || k >= len) return Value();
        return GetElem(rt, t, (std::uint64_t)k);
    }, 1);
    DefNative(rt, tap, "copyWithin", [](Runtime& rt, CallArgs& a) {
        TypedArrayObj* t = ValidTA(rt, a.thisv, "copyWithin");
        std::uint64_t len = t->Length();
        const std::uint64_t to = Rel(rt, a[0], len, 0), from = Rel(rt, a[1], len, 0), fin = Rel(rt, a[2], len, len);
        std::uint64_t count = std::min(fin > from ? fin - from : 0, len - to);
        if (count == 0) return a.thisv;
        if (t->OutOfBounds()) rt.ThrowType("%TypedArray%.prototype.copyWithin: the typed array's buffer is detached");
        len = t->Length();
        const std::uint64_t es = t->ElemSize();
        // clamp to the bytes the view has now, then move (memmove handles the overlap as the spec's direction rule does)
        const std::uint64_t limit = len * es;
        std::uint64_t fromB = from * es, toB = to * es, bytes = count * es;
        if (fromB >= limit || toB >= limit) return a.thisv;
        bytes = std::min({bytes, limit - fromB, limit - toB});
        rt.Charge(bytes / 64 + 1);
        std::memmove(t->buf->data.data() + t->offset + toB, t->buf->data.data() + t->offset + fromB, (std::size_t)bytes);
        return a.thisv;
    }, 2);
    DefNative(rt, tap, "entries", [](Runtime& rt, CallArgs& a) {
        ValidTA(rt, a.thisv, "entries");
        return rt.Call(rt.Get(Value::Obj(rt.arrayProto), rt.Intern("entries")), a.thisv, nullptr, 0);
    }, 0);
    DefNative(rt, tap, "keys", [](Runtime& rt, CallArgs& a) {
        ValidTA(rt, a.thisv, "keys");
        return rt.Call(rt.Get(Value::Obj(rt.arrayProto), rt.Intern("keys")), a.thisv, nullptr, 0);
    }, 0);
    NativeFunc* values = DefNative(rt, tap, "values", [](Runtime& rt, CallArgs& a) {
        ValidTA(rt, a.thisv, "values");
        return rt.Call(Value::Obj(rt.vm->arrayValues), a.thisv, nullptr, 0);
    }, 0);
    tap->AddRaw(rt.symIterator, Value::Obj(values), kHidden);
    // every / some / find / findIndex / findLast / findLastIndex / forEach (23.2.3.7, .26, .10-.13, .15)
    struct Walk {
        static Value Run(Runtime& rt, CallArgs& a, const char* what, int mode) {
            TypedArrayObj* t = ValidTA(rt, a.thisv, what);
            const std::uint64_t len = t->Length();
            Value fn = Fn(rt, a[0], what);
            const bool back = mode == 4 || mode == 5;
            for (std::uint64_t n = 0; n < len; ++n) {
                const std::uint64_t k = back ? len - 1 - n : n;
                rt.Charge(1);
                Runtime::ScratchScope ss(rt);
                Value v = GetElem(rt, t, k);
                Value args[3] = {v, Value::Num((double)k), a.thisv};
                const bool r = rt.ToBoolean(rt.Call(fn, a[1], args, 3));
                switch (mode) {
                    case 0: if (!r) return Value::Bool(false); break;    // every
                    case 1: if (r) return Value::Bool(true); break;      // some
                    case 2: case 4: if (r) return v; break;              // find, findLast
                    case 3: case 5: if (r) return Value::Num((double)k); break;   // findIndex, findLastIndex
                    default: break;                                      // forEach
                }
            }
            switch (mode) {
                case 0: return Value::Bool(true);
                case 1: return Value::Bool(false);
                case 3: case 5: return Value::Num(-1);
                default: return Value();
            }
        }
    };
    DefNative(rt, tap, "every", [](Runtime& rt, CallArgs& a) { return Walk::Run(rt, a, "every", 0); }, 1);
    DefNative(rt, tap, "some", [](Runtime& rt, CallArgs& a) { return Walk::Run(rt, a, "some", 1); }, 1);
    DefNative(rt, tap, "find", [](Runtime& rt, CallArgs& a) { return Walk::Run(rt, a, "find", 2); }, 1);
    DefNative(rt, tap, "findIndex", [](Runtime& rt, CallArgs& a) { return Walk::Run(rt, a, "findIndex", 3); }, 1);
    DefNative(rt, tap, "findLast", [](Runtime& rt, CallArgs& a) { return Walk::Run(rt, a, "findLast", 4); }, 1);
    DefNative(rt, tap, "findLastIndex", [](Runtime& rt, CallArgs& a) { return Walk::Run(rt, a, "findLastIndex", 5); }, 1);
    DefNative(rt, tap, "forEach", [](Runtime& rt, CallArgs& a) { return Walk::Run(rt, a, "forEach", 6); }, 1);
    DefNative(rt, tap, "fill", [](Runtime& rt, CallArgs& a) {   // 23.2.3.9 (the buffer is checked again after the conversions)
        TypedArrayObj* t = ValidTA(rt, a.thisv, "fill");
        std::uint64_t len = t->Length();
        Value v = ToElemValue(rt, t->type, a[0]);
        rt.Keep(v);
        std::uint64_t k = Rel(rt, a[1], len, 0), fin = Rel(rt, a[2], len, len);
        if (t->OutOfBounds()) rt.ThrowType("%TypedArray%.prototype.fill: the typed array's buffer is detached");
        len = t->Length();
        fin = std::min(fin, len);
        if (fin > k) rt.Charge((fin - k) / 8 + 1);
        for (; k < fin; ++k) PutElem(t, k, v);
        return a.thisv;
    }, 1);
    DefNative(rt, tap, "filter", [](Runtime& rt, CallArgs& a) {
        TypedArrayObj* t = ValidTA(rt, a.thisv, "filter");
        const std::uint64_t len = t->Length();
        Value fn = Fn(rt, a[0], "filter");
        Runtime::RootedVector kept(rt);
        for (std::uint64_t k = 0; k < len; ++k) {
            rt.Charge(1);
            Runtime::ScratchScope ss(rt);
            Value v = GetElem(rt, t, k);
            Value args[3] = {v, Value::Num((double)k), a.thisv};
            if (rt.ToBoolean(rt.Call(fn, a[1], args, 3))) kept.v.push_back(v);
        }
        Value n = Value::Num((double)kept.v.size());
        TypedArrayObj* r = SpeciesCreate(rt, t, &n, 1);
        rt.Keep(r);
        for (std::size_t i = 0; i < kept.v.size(); ++i) rt.PutIndex(Value::Obj(r), (std::uint32_t)i, kept.v[i]);
        return Value::Obj(r);
    }, 1);
    struct Search {   // includes (SameValueZero), indexOf and lastIndexOf (IsStrictlyEqual): 23.2.3.16-18, .20
        static Value Run(Runtime& rt, CallArgs& a, int mode) {
            TypedArrayObj* t = ValidTA(rt, a.thisv, mode == 0 ? "includes" : mode == 1 ? "indexOf" : "lastIndexOf");
            const std::uint64_t len = t->Length();
            const Value none = mode == 0 ? Value::Bool(false) : Value::Num(-1);
            if (len == 0) return none;
            if (mode == 2) {
                double n = a.argc > 1 ? rt.ToIntegerOrInfinity(a[1]) : (double)len - 1;
                if (n == -INFINITY) return none;
                double k = n >= 0 ? std::min(n, (double)len - 1) : (double)len + n;
                for (; k >= 0; --k) {
                    rt.Charge(1);
                    if (!ValidIntegerIndex(t, k)) continue;
                    if (StrictEquals(GetElem(rt, t, (std::uint64_t)k), a[0])) return Value::Num(k);
                }
                return none;
            }
            double n = rt.ToIntegerOrInfinity(a[1]);
            if (n == INFINITY) return none;
            if (n < 0) n = std::max(0.0, (double)len + n);
            for (double k = n; k < (double)len; ++k) {
                rt.Charge(1);
                Value e = GetElem(rt, t, (std::uint64_t)k);   // undefined once detached (includes(undefined) is then true)
                if (mode == 0 ? SameValueZero(e, a[0]) : (ValidIntegerIndex(t, k) && StrictEquals(e, a[0])))
                    return mode == 0 ? Value::Bool(true) : Value::Num(k);
            }
            return none;
        }
    };
    DefNative(rt, tap, "includes", [](Runtime& rt, CallArgs& a) { return Search::Run(rt, a, 0); }, 1);
    DefNative(rt, tap, "indexOf", [](Runtime& rt, CallArgs& a) { return Search::Run(rt, a, 1); }, 1);
    DefNative(rt, tap, "lastIndexOf", [](Runtime& rt, CallArgs& a) { return Search::Run(rt, a, 2); }, 1);
    DefNative(rt, tap, "join", [](Runtime& rt, CallArgs& a) {
        TypedArrayObj* t = ValidTA(rt, a.thisv, "join");
        const std::uint64_t len = t->Length();
        std::u16string sep = a[0].IsUndef() ? u"," : rt.ToU16(a[0]);
        std::u16string out;
        for (std::uint64_t k = 0; k < len; ++k) {
            rt.Charge(1);
            if (k) out += sep;
            Value e = GetElem(rt, t, k);
            if (!e.IsUndef()) out += rt.ToU16(e);
            if (out.size() > (1u << 28)) rt.ThrowRange("the joined string is too long");
        }
        rt.Charge(out.size() / 16);
        return Value::Str(rt.NewString(std::move(out)));
    }, 1);
    DefNative(rt, tap, "map", [](Runtime& rt, CallArgs& a) {
        TypedArrayObj* t = ValidTA(rt, a.thisv, "map");
        const std::uint64_t len = t->Length();
        Value fn = Fn(rt, a[0], "map");
        Value n = Value::Num((double)len);
        TypedArrayObj* r = SpeciesCreate(rt, t, &n, 1);
        rt.Keep(r);
        for (std::uint64_t k = 0; k < len; ++k) {
            rt.Charge(1);
            Runtime::ScratchScope ss(rt);
            Value args[3] = {GetElem(rt, t, k), Value::Num((double)k), a.thisv};
            Value m = rt.Call(fn, a[1], args, 3);
            rt.PutIndex(Value::Obj(r), (std::uint32_t)k, m);
        }
        return Value::Obj(r);
    }, 1);
    struct Reduce {
        static Value Run(Runtime& rt, CallArgs& a, bool right) {
            TypedArrayObj* t = ValidTA(rt, a.thisv, right ? "reduceRight" : "reduce");
            const std::uint64_t len = t->Length();
            Value fn = Fn(rt, a[0], right ? "reduceRight" : "reduce");
            std::uint64_t n = 0;
            Value acc;
            if (a.argc > 1) acc = a[1];
            else {
                if (len == 0) rt.ThrowType("reduce of an empty typed array with no initial value");
                acc = GetElem(rt, t, right ? len - 1 : 0);
                n = 1;
            }
            rt.Keep(acc);
            for (; n < len; ++n) {
                const std::uint64_t k = right ? len - 1 - n : n;
                rt.Charge(1);
                Value args[4] = {acc, GetElem(rt, t, k), Value::Num((double)k), a.thisv};
                acc = rt.Call(fn, Value(), args, 4);
            }
            return acc;
        }
    };
    DefNative(rt, tap, "reduce", [](Runtime& rt, CallArgs& a) { return Reduce::Run(rt, a, false); }, 1);
    DefNative(rt, tap, "reduceRight", [](Runtime& rt, CallArgs& a) { return Reduce::Run(rt, a, true); }, 1);
    DefNative(rt, tap, "reverse", [](Runtime& rt, CallArgs& a) {
        TypedArrayObj* t = ValidTA(rt, a.thisv, "reverse");
        const std::uint64_t len = t->Length();
        rt.Charge(len / 8 + 1);
        for (std::uint64_t lo = 0, hi = len; lo + 1 < hi; ++lo, --hi) {
            Value x = GetElem(rt, t, lo), y = GetElem(rt, t, hi - 1);
            PutElem(t, lo, y);
            PutElem(t, hi - 1, x);
        }
        return a.thisv;
    }, 0);
    DefNative(rt, tap, "set", [](Runtime& rt, CallArgs& a) {   // 23.2.3.26 with 23.2.3.26.1 / .2
        TypedArrayObj* t = AsTA(a.thisv);
        if (!t) rt.ThrowType("%TypedArray%.prototype.set needs a typed array as 'this'");
        const double off = rt.ToIntegerOrInfinity(a[1]);
        if (off < 0) rt.ThrowRange("%TypedArray%.prototype.set: the offset must not be negative");
        if (t->OutOfBounds()) rt.ThrowType("%TypedArray%.prototype.set: the typed array's buffer is detached");
        const std::uint64_t len = t->Length();
        if (TypedArrayObj* src = AsTA(a[0])) {
            if (src->OutOfBounds()) rt.ThrowType("%TypedArray%.prototype.set: the source's buffer is detached");
            if (src->IsBigInt() != t->IsBigInt()) rt.ThrowType("can not mix BigInt and Number typed arrays");
            const std::uint64_t n = src->Length();
            if (off == INFINITY || (double)n + off > (double)len) rt.ThrowRange("%TypedArray%.prototype.set: the source does not fit");
            const std::uint64_t o = (std::uint64_t)off;
            rt.Charge(n / 8 + 1);
            // copy through a list of values so an overlapping view of the same buffer reads every element first
            std::vector<Value> tmp;   // no script runs in these two loops, so no collection either
            tmp.reserve((std::size_t)n);
            for (std::uint64_t i = 0; i < n; ++i) tmp.push_back(GetElem(rt, src, i));
            for (std::uint64_t i = 0; i < n; ++i) PutElem(t, o + i, tmp[(std::size_t)i]);
            return Value();
        }
        Object* src = rt.ToObject(a[0]);
        rt.Keep(src);
        const std::uint64_t n = rt.LengthOf(Value::Obj(src));
        if (off == INFINITY || (double)n + off > (double)len) rt.ThrowRange("%TypedArray%.prototype.set: the source does not fit");
        const std::uint64_t o = (std::uint64_t)off;
        for (std::uint64_t i = 0; i < n; ++i) {
            rt.Charge(1);
            Runtime::ScratchScope ss(rt);
            Value v = ToElemValue(rt, t->type, GetAt(rt, Value::Obj(src), i));
            if (ValidIntegerIndex(t, (double)(o + i))) PutElem(t, o + i, v);
        }
        return Value();
    }, 1);
    DefNative(rt, tap, "slice", [](Runtime& rt, CallArgs& a) {   // 23.2.3.27
        TypedArrayObj* t = ValidTA(rt, a.thisv, "slice");
        std::uint64_t len = t->Length();
        const std::uint64_t k = Rel(rt, a[0], len, 0), fin = Rel(rt, a[1], len, len);
        const std::uint64_t count = fin > k ? fin - k : 0;
        Value n = Value::Num((double)count);
        TypedArrayObj* r = SpeciesCreate(rt, t, &n, 1);
        rt.Keep(r);
        if (count > 0) {
            if (t->OutOfBounds()) rt.ThrowType("%TypedArray%.prototype.slice: the typed array's buffer is detached");
            len = t->Length();
            const std::uint64_t end = std::min(fin, len);
            rt.Charge(count / 8 + 1);
            if (r->type == t->type) {
                const std::uint64_t es = t->ElemSize();
                if (end > k) {
                    std::uint64_t bytes = (end - k) * es;
                    const std::uint64_t room = r->Length() * es;
                    bytes = std::min(bytes, room);
                    std::memmove(r->buf->data.data() + r->offset, t->buf->data.data() + t->offset + k * es, (std::size_t)bytes);
                }
            } else {
                for (std::uint64_t i = k, j = 0; i < end; ++i, ++j) PutElem(r, j, GetElem(rt, t, i));
            }
        }
        return Value::Obj(r);
    }, 2);
    DefNative(rt, tap, "sort", [](Runtime& rt, CallArgs& a) {   // 23.2.3.29
        if (!a[0].IsUndef() && !rt.IsCallable(a[0])) rt.ThrowType("%TypedArray%.prototype.sort's comparator is not a function");
        TypedArrayObj* t = ValidTA(rt, a.thisv, "sort");
        const std::uint64_t len = t->Length();
        Runtime::RootedVector vals(rt);
        vals.v.reserve((std::size_t)len);
        for (std::uint64_t i = 0; i < len; ++i) vals.v.push_back(GetElem(rt, t, i));
        SortValues(rt, vals.v, a[0]);
        for (std::uint64_t i = 0; i < len; ++i) PutElem(t, i, vals.v[(std::size_t)i]);   // a no-op once detached
        return a.thisv;
    }, 1);
    DefNative(rt, tap, "subarray", [](Runtime& rt, CallArgs& a) {   // 23.2.3.30
        TypedArrayObj* t = AsTA(a.thisv);
        if (!t) rt.ThrowType("%TypedArray%.prototype.subarray needs a typed array as 'this'");
        const std::uint64_t len = t->Length();
        const std::uint64_t b = Rel(rt, a[0], len, 0), e = Rel(rt, a[1], len, len);
        const std::uint64_t n = e > b ? e - b : 0;
        Value args[3] = {Value::Obj(t->buf), Value::Num((double)(t->offset + b * t->ElemSize())), Value::Num((double)n)};
        Value dflt = Value::Obj(Intrinsic(rt, kTypedCtors + (int)t->type));
        Value c = rt.Get(a.thisv, rt.atoms->constructor);
        Value ctor = dflt;
        if (!c.IsUndef()) {
            if (!c.IsObj()) rt.ThrowType("the typed array's constructor is not an object");
            Value sp = rt.Get(c, rt.symSpecies);
            if (!sp.IsNullish()) {
                if (!rt.IsConstructor(sp)) rt.ThrowType("[Symbol.species] is not a constructor");
                ctor = sp;
            }
        }
        rt.Keep(ctor);
        TypedArrayObj* r = CreateFromCtor(rt, ctor, args, 3);
        if (r->IsBigInt() != t->IsBigInt()) rt.ThrowType("the species constructor made a typed array of the other content type");
        return Value::Obj(r);
    }, 2);
    DefNative(rt, tap, "toLocaleString", [](Runtime& rt, CallArgs& a) {
        TypedArrayObj* t = ValidTA(rt, a.thisv, "toLocaleString");
        const std::uint64_t len = t->Length();
        std::u16string out;
        for (std::uint64_t k = 0; k < len; ++k) {
            rt.Charge(1);
            if (k) out += u",";
            Value e = GetElem(rt, t, k);
            if (!e.IsNullish()) out += rt.ToU16(rt.Call(rt.Get(e, rt.Intern("toLocaleString")), e, nullptr, 0));
            if (out.size() > (1u << 28)) rt.ThrowRange("the joined string is too long");
        }
        return Value::Str(rt.NewString(std::move(out)));
    }, 0);
    DefNative(rt, tap, "toReversed", [](Runtime& rt, CallArgs& a) {   // 23.2.3.32
        TypedArrayObj* t = ValidTA(rt, a.thisv, "toReversed");
        const std::uint64_t len = t->Length();
        TypedArrayObj* r = AllocTA(rt, t->type, len, nullptr);
        rt.Charge(len / 8 + 1);
        for (std::uint64_t k = 0; k < len; ++k) PutElem(r, k, GetElem(rt, t, len - 1 - k));
        return Value::Obj(r);
    }, 0);
    DefNative(rt, tap, "toSorted", [](Runtime& rt, CallArgs& a) {   // 23.2.3.33
        if (!a[0].IsUndef() && !rt.IsCallable(a[0])) rt.ThrowType("%TypedArray%.prototype.toSorted's comparator is not a function");
        TypedArrayObj* t = ValidTA(rt, a.thisv, "toSorted");
        const std::uint64_t len = t->Length();
        TypedArrayObj* r = AllocTA(rt, t->type, len, nullptr);
        rt.Keep(r);
        Runtime::RootedVector vals(rt);
        for (std::uint64_t i = 0; i < len; ++i) vals.v.push_back(GetElem(rt, t, i));
        SortValues(rt, vals.v, a[0]);
        for (std::uint64_t i = 0; i < len; ++i) PutElem(r, i, vals.v[(std::size_t)i]);
        return Value::Obj(r);
    }, 1);
    tap->AddRaw(rt.atoms->toString, rt.Get(Value::Obj(rt.arrayProto), rt.atoms->toString), kHidden);   // 23.2.3.35: the same function
    DefNative(rt, tap, "with", [](Runtime& rt, CallArgs& a) {   // 23.2.3.37
        TypedArrayObj* t = ValidTA(rt, a.thisv, "with");
        const std::uint64_t len = t->Length();
        double rel = rt.ToIntegerOrInfinity(a[0]);
        const double idx = rel >= 0 ? rel : (double)len + rel;
        Value v = ToElemValue(rt, t->type, a[1]);
        rt.Keep(v);
        if (!ValidIntegerIndex(t, idx)) rt.ThrowRange("%TypedArray%.prototype.with: the index is outside the typed array");
        TypedArrayObj* r = AllocTA(rt, t->type, len, nullptr);
        rt.Charge(len / 8 + 1);
        for (std::uint64_t k = 0; k < len; ++k) PutElem(r, k, (double)k == idx ? v : GetElem(rt, t, k));
        return Value::Obj(r);
    }, 2);

    // ---- 23.2.6-23.2.7 the eleven constructors
    for (int i = 0; i < kTATypes; ++i) {
        Object* p = rt.NewObject(tap);
        Intrinsic(rt, kTypedProtos + i) = p;
        NativeFunc* c = DefCtor(rt, kTAName[i], kTACtors[i], 3, p);
        c->proto = tac;
        Intrinsic(rt, kTypedCtors + i) = c;
        DefValue(rt, c, "BYTES_PER_ELEMENT", Value::Num(kElemSize[i]), 0);
        DefValue(rt, p, "BYTES_PER_ELEMENT", Value::Num(kElemSize[i]), 0);
    }

    // ---- 25.3 DataView
    Object* dvp = rt.NewObject();
    Intrinsic(rt, kDataViewProto) = dvp;
    DefCtor(rt, "DataView", [](Runtime& rt, CallArgs& a) {
        if (!a.newTarget) rt.ThrowType("DataView must be called with 'new'");
        if (!a[0].IsObj() || a[0].O()->cls != Cls::ArrayBuffer) rt.ThrowType("DataView needs an ArrayBuffer");
        BufferObj* b = static_cast<BufferObj*>(a[0].O());
        const std::uint64_t off = ToIndexV(rt, a[1], "byteOffset");
        if (b->detached) rt.ThrowType("DataView: the buffer is detached");
        const std::uint64_t bufLen = b->data.size();
        if (off > bufLen) rt.ThrowRange("DataView: byteOffset is past the end of the buffer");
        std::uint64_t len = bufLen - off;
        if (!a[2].IsUndef()) {
            len = ToIndexV(rt, a[2], "byteLength");
            if (len > bufLen - off) rt.ThrowRange("DataView: byteOffset + byteLength is past the end of the buffer");
        }
        Object* proto = ProtoFrom(rt, a.newTarget, Intrinsic(rt, kDataViewProto));
        if (b->detached) rt.ThrowType("DataView: the buffer is detached");
        DataViewObj* d = rt.Alloc<DataViewObj>();
        d->buf = b;
        d->offset = off;
        d->length = len;
        d->proto = proto;
        return Value::Obj(d);
    }, 1, dvp);
    DefGetter(rt, dvp, "buffer", [](Runtime& rt, CallArgs& a) { return Value::Obj(ThisView(rt, a.thisv, "buffer")->buf); });
    DefGetter(rt, dvp, "byteLength", [](Runtime& rt, CallArgs& a) {
        DataViewObj* d = ThisView(rt, a.thisv, "byteLength");
        if (d->buf->detached) rt.ThrowType("DataView.prototype.byteLength: the buffer is detached");
        return Value::Num((double)d->length);
    });
    DefGetter(rt, dvp, "byteOffset", [](Runtime& rt, CallArgs& a) {
        DataViewObj* d = ThisView(rt, a.thisv, "byteOffset");
        if (d->buf->detached) rt.ThrowType("DataView.prototype.byteOffset: the buffer is detached");
        return Value::Num((double)d->offset);
    });
    for (int t = 0; t < kTATypes; ++t)   // 25.3.4: every element type but Uint8Clamped, the getters then the setters
        if (kViewGetFns[t]) DefNative(rt, dvp, kViewGet[t], kViewGetFns[t], 1);
    for (int t = 0; t < kTATypes; ++t)
        if (kViewSetFns[t]) DefNative(rt, dvp, kViewSet[t], kViewSetFns[t], 2);
    dvp->AddRaw(rt.symToStringTag, Value::Str(rt.Intern("DataView")), kConfigurable);

    // ---- 25.4 Atomics (one thread: every operation is atomic; wait can not block the only thread)
    Object* at = rt.NewObject();
    DefValue(rt, g, "Atomics", Value::Obj(at));
    at->AddRaw(rt.symToStringTag, Value::Str(rt.Intern("Atomics")), kConfigurable);
    DefNative(rt, at, "add", [](Runtime& rt, CallArgs& a) { return AtomicRMW(rt, a, 1); }, 3);
    DefNative(rt, at, "sub", [](Runtime& rt, CallArgs& a) { return AtomicRMW(rt, a, 2); }, 3);
    DefNative(rt, at, "and", [](Runtime& rt, CallArgs& a) { return AtomicRMW(rt, a, 3); }, 3);
    DefNative(rt, at, "or", [](Runtime& rt, CallArgs& a) { return AtomicRMW(rt, a, 4); }, 3);
    DefNative(rt, at, "xor", [](Runtime& rt, CallArgs& a) { return AtomicRMW(rt, a, 5); }, 3);
    DefNative(rt, at, "exchange", [](Runtime& rt, CallArgs& a) { return AtomicRMW(rt, a, 0); }, 3);
    DefNative(rt, at, "load", [](Runtime& rt, CallArgs& a) {
        TypedArrayObj* t = AtomicTA(rt, a[0], false);
        return GetElem(rt, t, AtomicIndex(rt, t, a[1]));
    }, 2);
    DefNative(rt, at, "store", [](Runtime& rt, CallArgs& a) {
        TypedArrayObj* t = AtomicTA(rt, a[0], false);
        const std::uint64_t i = AtomicIndex(rt, t, a[1]);
        Value v = t->IsBigInt() ? ToBigIntValue(rt, a[2]) : Value::Num(rt.ToIntegerOrInfinity(a[2]));
        rt.Keep(v);
        if (t->OutOfBounds()) rt.ThrowType("Atomics.store: the typed array's buffer is detached");
        PutElem(t, i, v);
        return v.IsNum() && v.n == 0 ? Value::Num(0) : v;
    }, 3);
    DefNative(rt, at, "compareExchange", [](Runtime& rt, CallArgs& a) {
        TypedArrayObj* t = AtomicTA(rt, a[0], false);
        const std::uint64_t i = AtomicIndex(rt, t, a[1]);
        Value expected = ToElemValue(rt, t->type, a[2]);
        rt.Keep(expected);
        Value repl = ToElemValue(rt, t->type, a[3]);
        rt.Keep(repl);
        if (t->OutOfBounds()) rt.ThrowType("Atomics.compareExchange: the typed array's buffer is detached");
        std::uint8_t* p = ElemPtr(t, i);
        if (!p) return Value();
        // compare the bytes the expected value would have (25.4.11 step 13: a byte-wise comparison)
        std::uint8_t want[8];
        ValueToRaw(t->type, expected, want, true);
        Value old = RawToValue(rt, t->type, p, true);
        if (std::memcmp(want, p, t->ElemSize()) == 0) ValueToRaw(t->type, repl, p, true);
        return old;
    }, 4);
    DefNative(rt, at, "isLockFree", [](Runtime& rt, CallArgs& a) {
        const double n = rt.ToIntegerOrInfinity(a[0]);
        return Value::Bool(n == 1 || n == 2 || n == 4 || n == 8);
    }, 1);
    DefNative(rt, at, "wait", [](Runtime& rt, CallArgs& a) -> Value {
        AtomicTA(rt, a[0], true);
        rt.ThrowType("Atomics.wait can not block here: there is only one thread");
    }, 4);
    DefNative(rt, at, "notify", [](Runtime& rt, CallArgs& a) {
        TypedArrayObj* t = AtomicTA(rt, a[0], true);
        AtomicIndex(rt, t, a[1]);
        return Value::Num(0);   // nobody can be waiting
    }, 3);
}

} // namespace okay::js
