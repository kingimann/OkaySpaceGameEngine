// okay::js library: String (ECMA-262 22.1). Written from 22.1.1-22.1.3: the
// constructor, String.fromCharCode / fromCodePoint and the prototype methods,
// with strings as sequences of UTF-16 code units. Patterns are plain strings:
// regular expressions are not supported, so replace / split / includes take a
// string to look for (replace keeps the $$ $& $` $' replacement patterns of
// 22.1.3.19.1 GetSubstitution) or a RegExp (JsRegExp.cpp). Case mapping covers ASCII, Latin-1, Greek and
// Cyrillic (not the whole Unicode table). Every method charges the run budget in
// proportion to the characters it makes or scans.
#include "JsInternal.hpp"
#include <algorithm>
#include <cmath>

namespace okay::js {

namespace {

std::u16string ThisStr(Runtime& rt, CallArgs& a, const char* m) {
    if (a.thisv.IsNullish()) rt.ThrowType(std::string("String.prototype.") + m + " called on null or undefined");
    if (a.thisv.IsStr()) return a.thisv.S()->s;
    return rt.ToU16(a.thisv);
}

Value Str(Runtime& rt, std::u16string s) {
    rt.Charge(s.size() / 16);
    return Value::Str(rt.NewString(std::move(s)));
}


std::size_t FindFrom(Runtime& rt, const std::u16string& s, const std::u16string& pat, std::size_t from) {
    rt.Charge((s.size() > from ? s.size() - from : 0) / 8 + 1);
    return s.find(pat, from);
}

// 22.1.3.19.1 GetSubstitution for a string pattern
std::u16string Substitute(const std::u16string& matched, const std::u16string& str, std::size_t pos, const std::u16string& rep) {
    std::u16string out;
    for (std::size_t i = 0; i < rep.size(); ++i) {
        char16_t c = rep[i];
        if (c == '$' && i + 1 < rep.size()) {
            char16_t n = rep[i + 1];
            if (n == '$') { out += u'$'; ++i; continue; }
            if (n == '&') { out += matched; ++i; continue; }
            if (n == '`') { out += str.substr(0, pos); ++i; continue; }
            if (n == '\'') { std::size_t e = pos + matched.size(); if (e < str.size()) out += str.substr(e); ++i; continue; }
        }
        out += c;
    }
    return out;
}

Value ReplaceImpl(Runtime& rt, CallArgs& a, bool all) {
    std::u16string s = ThisStr(rt, a, all ? "replaceAll" : "replace");
    if (IsRegExp(a[0])) {
        String* str = rt.NewString(s);
        rt.Keep(Value::Str(str));
        return RegExpReplace(rt, a[0], str, a[1], all);
    }
    std::u16string pat = rt.ToU16(a[0]);
    Value rep = a[1];
    bool fn = rt.IsCallable(rep);
    std::u16string repStr = fn ? u"" : rt.ToU16(rep);
    std::vector<std::size_t> hits;
    std::size_t adv = std::max<std::size_t>(1, pat.size());
    for (std::size_t p = FindFrom(rt, s, pat, 0); p != std::u16string::npos; p = FindFrom(rt, s, pat, p + adv)) {
        hits.push_back(p);
        if (!all) break;
        if (p >= s.size()) break;
        rt.Charge(1);
    }
    if (hits.empty()) return Value::Str(a.thisv.IsStr() ? a.thisv.S() : rt.NewString(s));
    std::u16string out;
    std::size_t last = 0;
    for (std::size_t p : hits) {
        out += s.substr(last, p - last);
        if (fn) {
            Runtime::ScratchScope ss(rt);
            Value args[3] = {Value::Str(rt.NewString(pat)), Value::Num((double)p), Value::Str(rt.NewString(s))};
            out += rt.ToU16(rt.Call(rep, Value(), args, 3));
        } else out += Substitute(pat, s, p, repStr);
        last = p + pat.size();
        if (out.size() > (1u << 28)) rt.ThrowRange("replace: the result is too long");
    }
    if (last < s.size()) out += s.substr(last);
    return Str(rt, std::move(out));
}

Value StringMatch(Runtime& rt, CallArgs& a, int mode, const char* name) {
    std::u16string s = ThisStr(rt, a, name);
    String* str = rt.NewString(s);
    rt.Keep(Value::Str(str));
    Value rx = a[0];
    if (!IsRegExp(rx)) {
        Value ctor = rt.GetGlobal("RegExp");
        Value args[2] = {rx.IsUndef() ? Value::Str(rt.atoms->empty) : Value::Str(rt.ToString(rx)),
                         Value::Str(rt.NewStringAscii(mode == 1 ? "g" : ""))};
        rx = rt.Construct(ctor, args, 2);
    }
    return RegExpMatch(rt, rx, str, mode);
}

} // namespace

void InstallStringLib(Runtime& rt) {
    BoxObj* sp = rt.Alloc<BoxObj>();
    sp->cls = Cls::StringBox;
    sp->prim = Value::Str(rt.atoms->empty);
    sp->proto = rt.objectProto;
    rt.stringProto = sp;
    NativeFunc* ctor = DefCtor(rt, "String", [](Runtime& rt, CallArgs& a) {
        Value s;
        if (a.argc == 0) s = Value::Str(rt.atoms->empty);
        else if (!a.newTarget && a[0].IsSym()) {
            Symbol* y = a[0].Y();
            s = Value::Str(rt.NewString(u"Symbol(" + (y->desc ? y->desc->s : u"") + u")"));
        } else s = Value::Str(rt.ToString(a[0]));
        if (!a.newTarget) return s;
        Object* o = rt.ToObject(s);
        Value pr = rt.Get(Value::Obj(a.newTarget), rt.atoms->prototype);
        if (pr.IsObj()) o->proto = pr.O();
        return Value::Obj(o);
    }, 1, sp);
    DefNative(rt, ctor, "fromCharCode", [](Runtime& rt, CallArgs& a) {
        std::u16string s;
        for (int i = 0; i < a.argc; ++i) s += (char16_t)(rt.ToUint32(a[i]) & 0xFFFF);
        return Str(rt, std::move(s));
    }, 1);
    DefNative(rt, ctor, "raw", [](Runtime& rt, CallArgs& a) {   // 22.1.2.4 String.raw
        Object* cooked = rt.ToObject(a[0]);
        Value lits = Value::Obj(rt.ToObject(rt.Get(Value::Obj(cooked), rt.atoms->raw)));
        rt.Keep(lits);
        std::uint64_t len = rt.LengthOf(lits);
        std::u16string r;
        for (std::uint64_t k = 0; k < len; ++k) {
            Runtime::ScratchScope ss(rt);
            rt.Charge(1);
            r += rt.ToU16(rt.GetV(lits, Value::Num((double)k)));
            if (k + 1 == len) break;
            if (k + 1 < (std::uint64_t)a.argc) r += rt.ToU16(a[(int)(k + 1)]);
            rt.Charge(r.size() / 64);
            if (r.size() > (1u << 28)) rt.ThrowRange("the string is too long");
        }
        return Value::Str(rt.NewString(std::move(r)));
    }, 1);
    DefNative(rt, ctor, "fromCodePoint", [](Runtime& rt, CallArgs& a) {
        std::u16string s;
        for (int i = 0; i < a.argc; ++i) {
            double d = rt.ToNumber(a[i]);
            if (d != std::floor(d) || d < 0 || d > 0x10FFFF) rt.ThrowRange("not a valid code point");
            std::uint32_t cp = (std::uint32_t)d;
            if (cp >= 0x10000) {
                cp -= 0x10000;
                s += (char16_t)(0xD800 + (cp >> 10));
                s += (char16_t)(0xDC00 + (cp & 0x3FF));
            } else s += (char16_t)cp;
        }
        return Str(rt, std::move(s));
    }, 1);

    DefNative(rt, sp, "toString", [](Runtime& rt, CallArgs& a) {
        if (a.thisv.IsStr()) return a.thisv;
        if (a.thisv.IsObj() && a.thisv.O()->cls == Cls::StringBox) return static_cast<BoxObj*>(a.thisv.O())->prim;
        rt.ThrowType("String.prototype.toString needs a string");
    }, 0);
    DefNative(rt, sp, "valueOf", [](Runtime& rt, CallArgs& a) {
        if (a.thisv.IsStr()) return a.thisv;
        if (a.thisv.IsObj() && a.thisv.O()->cls == Cls::StringBox) return static_cast<BoxObj*>(a.thisv.O())->prim;
        rt.ThrowType("String.prototype.valueOf needs a string");
    }, 0);
    DefNative(rt, sp, "charAt", [](Runtime& rt, CallArgs& a) {
        std::u16string s = ThisStr(rt, a, "charAt");
        double p = rt.ToIntegerOrInfinity(a[0]);
        if (p < 0 || p >= (double)s.size()) return Value::Str(rt.atoms->empty);
        return Value::Str(rt.NewString(std::u16string(1, s[(std::size_t)p])));
    }, 1);
    DefNative(rt, sp, "charCodeAt", [](Runtime& rt, CallArgs& a) {
        std::u16string s = ThisStr(rt, a, "charCodeAt");
        double p = rt.ToIntegerOrInfinity(a[0]);
        if (p < 0 || p >= (double)s.size()) return Value::Num(NAN);
        return Value::Num(s[(std::size_t)p]);
    }, 1);
    DefNative(rt, sp, "codePointAt", [](Runtime& rt, CallArgs& a) {
        std::u16string s = ThisStr(rt, a, "codePointAt");
        double p = rt.ToIntegerOrInfinity(a[0]);
        if (p < 0 || p >= (double)s.size()) return Value();
        std::size_t i = (std::size_t)p;
        char16_t c = s[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF)
            return Value::Num(0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00));
        return Value::Num(c);
    }, 1);
    DefNative(rt, sp, "at", [](Runtime& rt, CallArgs& a) {
        std::u16string s = ThisStr(rt, a, "at");
        double p = rt.ToIntegerOrInfinity(a[0]);
        if (p < 0) p += (double)s.size();
        if (p < 0 || p >= (double)s.size()) return Value();
        return Value::Str(rt.NewString(std::u16string(1, s[(std::size_t)p])));
    }, 1);
    DefNative(rt, sp, "concat", [](Runtime& rt, CallArgs& a) {
        std::u16string s = ThisStr(rt, a, "concat");
        for (int i = 0; i < a.argc; ++i) {
            s += rt.ToU16(a[i]);
            if (s.size() > (1u << 28)) rt.ThrowRange("concat: the result is too long");
        }
        return Str(rt, std::move(s));
    }, 1);
    DefNative(rt, sp, "indexOf", [](Runtime& rt, CallArgs& a) {
        std::u16string s = ThisStr(rt, a, "indexOf");
        std::u16string p = rt.ToU16(a[0]);
        double from = std::min(std::max(rt.ToIntegerOrInfinity(a[1]), 0.0), (double)s.size());
        std::size_t r = FindFrom(rt, s, p, (std::size_t)from);
        return Value::Num(r == std::u16string::npos ? -1 : (double)r);
    }, 1);
    DefNative(rt, sp, "lastIndexOf", [](Runtime& rt, CallArgs& a) {
        std::u16string s = ThisStr(rt, a, "lastIndexOf");
        std::u16string p = rt.ToU16(a[0]);
        double n = rt.ToNumber(a[1]);
        double from = std::isnan(n) ? INFINITY : std::trunc(n);
        from = std::min(std::max(from, 0.0), (double)s.size());
        rt.Charge(s.size() / 8 + 1);
        std::size_t r = s.rfind(p, (std::size_t)from);
        return Value::Num(r == std::u16string::npos ? -1 : (double)r);
    }, 1);
    DefNative(rt, sp, "includes", [](Runtime& rt, CallArgs& a) {
        std::u16string s = ThisStr(rt, a, "includes");
        if (IsRegExp(a[0])) rt.ThrowType("includes needs a string, not a regular expression");
        std::u16string p = rt.ToU16(a[0]);
        double from = std::min(std::max(rt.ToIntegerOrInfinity(a[1]), 0.0), (double)s.size());
        return Value::Bool(FindFrom(rt, s, p, (std::size_t)from) != std::u16string::npos);
    }, 1);
    DefNative(rt, sp, "startsWith", [](Runtime& rt, CallArgs& a) {
        std::u16string s = ThisStr(rt, a, "startsWith");
        std::u16string p = rt.ToU16(a[0]);
        double from = std::min(std::max(rt.ToIntegerOrInfinity(a[1]), 0.0), (double)s.size());
        std::size_t f = (std::size_t)from;
        rt.Charge(p.size() / 8 + 1);
        return Value::Bool(f + p.size() <= s.size() && s.compare(f, p.size(), p) == 0);
    }, 1);
    DefNative(rt, sp, "endsWith", [](Runtime& rt, CallArgs& a) {
        std::u16string s = ThisStr(rt, a, "endsWith");
        std::u16string p = rt.ToU16(a[0]);
        double end = a[1].IsUndef() ? (double)s.size() : std::min(std::max(rt.ToIntegerOrInfinity(a[1]), 0.0), (double)s.size());
        std::size_t e = (std::size_t)end;
        rt.Charge(p.size() / 8 + 1);
        return Value::Bool(p.size() <= e && s.compare(e - p.size(), p.size(), p) == 0);
    }, 1);
    DefNative(rt, sp, "slice", [](Runtime& rt, CallArgs& a) {
        std::u16string s = ThisStr(rt, a, "slice");
        std::int64_t len = (std::int64_t)s.size();
        std::int64_t from = RelIndex(rt, a[0], len, 0), to = RelIndex(rt, a[1], len, len);
        if (from >= to) return Value::Str(rt.atoms->empty);
        return Str(rt, s.substr((std::size_t)from, (std::size_t)(to - from)));
    }, 2);
    DefNative(rt, sp, "substring", [](Runtime& rt, CallArgs& a) {
        std::u16string s = ThisStr(rt, a, "substring");
        double len = (double)s.size();
        double st = std::min(std::max(rt.ToIntegerOrInfinity(a[0]), 0.0), len);
        double en = a[1].IsUndef() ? len : std::min(std::max(rt.ToIntegerOrInfinity(a[1]), 0.0), len);
        double from = std::min(st, en), to = std::max(st, en);
        return Str(rt, s.substr((std::size_t)from, (std::size_t)(to - from)));
    }, 2);
    DefNative(rt, sp, "substr", [](Runtime& rt, CallArgs& a) {   // B.2.2.1
        std::u16string s = ThisStr(rt, a, "substr");
        std::int64_t len = (std::int64_t)s.size();
        std::int64_t st = RelIndex(rt, a[0], len, 0);
        double l = a[1].IsUndef() ? (double)len : rt.ToIntegerOrInfinity(a[1]);
        double n = std::min(std::max(l, 0.0), (double)(len - st));
        if (n <= 0) return Value::Str(rt.atoms->empty);
        return Str(rt, s.substr((std::size_t)st, (std::size_t)n));
    }, 2);
    DefNative(rt, sp, "toLowerCase", [](Runtime& rt, CallArgs& a) {
        std::u16string s = ThisStr(rt, a, "toLowerCase");
        for (char16_t& c : s) c = JsLower(c);
        return Str(rt, std::move(s));
    }, 0);
    DefNative(rt, sp, "toUpperCase", [](Runtime& rt, CallArgs& a) {
        std::u16string s = ThisStr(rt, a, "toUpperCase");
        std::u16string o;
        o.reserve(s.size());
        for (char16_t c : s) {
            if (c == 0xDF) { o += u"SS"; continue; }
            o += JsUpper(c);
        }
        return Str(rt, std::move(o));
    }, 0);
    Prop* lc = sp->FindRaw(rt.Intern("toLowerCase"));
    Prop* uc = sp->FindRaw(rt.Intern("toUpperCase"));
    Value lcv = lc->v, ucv = uc->v;
    sp->AddRaw(rt.Intern("toLocaleLowerCase"), lcv, kHidden);
    sp->AddRaw(rt.Intern("toLocaleUpperCase"), ucv, kHidden);
    DefNative(rt, sp, "trim", [](Runtime& rt, CallArgs& a) {
        std::u16string s = ThisStr(rt, a, "trim");
        std::size_t b = 0, e = s.size();
        while (b < e && IsJsSpace(s[b])) ++b;
        while (e > b && IsJsSpace(s[e - 1])) --e;
        return Str(rt, s.substr(b, e - b));
    }, 0);
    DefNative(rt, sp, "trimStart", [](Runtime& rt, CallArgs& a) {
        std::u16string s = ThisStr(rt, a, "trimStart");
        std::size_t b = 0;
        while (b < s.size() && IsJsSpace(s[b])) ++b;
        return Str(rt, s.substr(b));
    }, 0);
    DefNative(rt, sp, "trimEnd", [](Runtime& rt, CallArgs& a) {
        std::u16string s = ThisStr(rt, a, "trimEnd");
        std::size_t e = s.size();
        while (e > 0 && IsJsSpace(s[e - 1])) --e;
        return Str(rt, s.substr(0, e));
    }, 0);
    // B.2.2.15 / B.2.2.16: trimLeft and trimRight are the same function objects as trimStart and trimEnd
    DefValue(rt, sp, "trimLeft", sp->FindRaw(rt.Intern("trimStart"))->v);
    DefValue(rt, sp, "trimRight", sp->FindRaw(rt.Intern("trimEnd"))->v);
    // 22.1.3.10 isWellFormed / 22.1.3.31 toWellFormed: lone surrogates, and replacing them with U+FFFD
    DefNative(rt, sp, "isWellFormed", [](Runtime& rt, CallArgs& a) {
        std::u16string s = ThisStr(rt, a, "isWellFormed");
        rt.Charge(s.size() / 32 + 1);
        for (std::size_t k = 0; k < s.size(); ++k) {
            char16_t c = s[k];
            if (c >= 0xD800 && c <= 0xDBFF && k + 1 < s.size() && s[k + 1] >= 0xDC00 && s[k + 1] <= 0xDFFF) { ++k; continue; }
            if (c >= 0xD800 && c <= 0xDFFF) return Value::Bool(false);
        }
        return Value::Bool(true);
    }, 0);
    DefNative(rt, sp, "toWellFormed", [](Runtime& rt, CallArgs& a) {
        std::u16string s = ThisStr(rt, a, "toWellFormed");
        rt.Charge(s.size() / 32 + 1);
        for (std::size_t k = 0; k < s.size(); ++k) {
            char16_t c = s[k];
            if (c >= 0xD800 && c <= 0xDBFF && k + 1 < s.size() && s[k + 1] >= 0xDC00 && s[k + 1] <= 0xDFFF) { ++k; continue; }
            if (c >= 0xD800 && c <= 0xDFFF) s[k] = 0xFFFD;
        }
        return Str(rt, s);
    }, 0);
    DefNative(rt, sp, "padStart", [](Runtime& rt, CallArgs& a) {
        std::u16string s = ThisStr(rt, a, "padStart");
        double max = rt.ToIntegerOrInfinity(a[0]);
        if (max <= (double)s.size()) return Value::Str(rt.NewString(s));
        if (max > (double)(1u << 28)) rt.ThrowRange("padStart: the result is too long");
        std::u16string fill = a[1].IsUndef() ? u" " : rt.ToU16(a[1]);
        if (fill.empty()) return Value::Str(rt.NewString(s));
        std::size_t need = (std::size_t)max - s.size();
        std::u16string p;
        while (p.size() < need) { p += fill; rt.Charge(fill.size() / 8 + 1); }
        p.resize(need);
        return Str(rt, p + s);
    }, 2);
    DefNative(rt, sp, "padEnd", [](Runtime& rt, CallArgs& a) {
        std::u16string s = ThisStr(rt, a, "padEnd");
        double max = rt.ToIntegerOrInfinity(a[0]);
        if (max <= (double)s.size()) return Value::Str(rt.NewString(s));
        if (max > (double)(1u << 28)) rt.ThrowRange("padEnd: the result is too long");
        std::u16string fill = a[1].IsUndef() ? u" " : rt.ToU16(a[1]);
        if (fill.empty()) return Value::Str(rt.NewString(s));
        std::size_t need = (std::size_t)max - s.size();
        std::u16string p;
        while (p.size() < need) { p += fill; rt.Charge(fill.size() / 8 + 1); }
        p.resize(need);
        return Str(rt, s + p);
    }, 2);
    DefNative(rt, sp, "repeat", [](Runtime& rt, CallArgs& a) {
        std::u16string s = ThisStr(rt, a, "repeat");
        double n = rt.ToIntegerOrInfinity(a[0]);
        if (n < 0 || std::isinf(n)) rt.ThrowRange("repeat count must be a non-negative finite number");
        if (s.empty() || n == 0) return Value::Str(rt.atoms->empty);
        if (n * (double)s.size() > (double)(1u << 28)) rt.ThrowRange("repeat: the result is too long");
        std::size_t total = (std::size_t)n * s.size();
        rt.Charge(total / 8);
        std::u16string o;
        o.reserve(total);
        for (std::size_t i = 0; i < (std::size_t)n; ++i) o += s;
        return Value::Str(rt.NewString(std::move(o)));
    }, 1);
    DefNative(rt, sp, "split", [](Runtime& rt, CallArgs& a) {
        std::u16string s = ThisStr(rt, a, "split");
        std::uint32_t lim = a[1].IsUndef() ? 4294967295u : rt.ToUint32(a[1]);
        if (IsRegExp(a[0])) {
            String* str = rt.NewString(s);
            rt.Keep(Value::Str(str));
            return RegExpSplit(rt, a[0], str, lim);
        }
        ArrayObj* out = rt.NewArray();
        rt.Keep(Value::Obj(out));
        if (lim == 0) return Value::Obj(out);
        if (a[0].IsUndef()) {
            rt.DefineIndex(out, 0, Value::Str(rt.NewString(s)));
            return Value::Obj(out);
        }
        std::u16string sep = rt.ToU16(a[0]);
        if (sep.empty()) {
            for (std::size_t i = 0; i < s.size() && out->length < lim; ++i) {
                rt.Charge(1);
                rt.DefineIndex(out, out->length, Value::Str(rt.NewString(std::u16string(1, s[i]))));
            }
            return Value::Obj(out);
        }
        std::size_t start = 0;
        while (out->length < lim) {
            std::size_t p = FindFrom(rt, s, sep, start);
            rt.Charge(1);
            if (p == std::u16string::npos) {
                rt.DefineIndex(out, out->length, Value::Str(rt.NewString(s.substr(start))));
                break;
            }
            rt.DefineIndex(out, out->length, Value::Str(rt.NewString(s.substr(start, p - start))));
            start = p + sep.size();
        }
        return Value::Obj(out);
    }, 2);
    DefNative(rt, sp, "replace", [](Runtime& rt, CallArgs& a) { return ReplaceImpl(rt, a, false); }, 2);
    DefNative(rt, sp, "replaceAll", [](Runtime& rt, CallArgs& a) { return ReplaceImpl(rt, a, true); }, 2);
    // match / matchAll / search (22.1.3.13, 22.1.3.14, 22.1.3.22): a string argument becomes a RegExp
    static const NativeFn matchFns[3] = {
        [](Runtime& rt, CallArgs& a) -> Value { return StringMatch(rt, a, 0, "match"); },
        [](Runtime& rt, CallArgs& a) -> Value { return StringMatch(rt, a, 1, "matchAll"); },
        [](Runtime& rt, CallArgs& a) -> Value { return StringMatch(rt, a, 2, "search"); },
    };
    DefNative(rt, sp, "match", matchFns[0], 1);
    DefNative(rt, sp, "matchAll", matchFns[1], 1);
    DefNative(rt, sp, "search", matchFns[2], 1);
    DefNative(rt, sp, "localeCompare", [](Runtime& rt, CallArgs& a) {
        std::u16string s = ThisStr(rt, a, "localeCompare");
        std::u16string t = rt.ToU16(a[0]);
        rt.Charge(std::min(s.size(), t.size()) / 8 + 1);
        return Value::Num(s < t ? -1 : s > t ? 1 : 0);
    }, 1);
    DefNative(rt, sp, "normalize", [](Runtime& rt, CallArgs& a) { return Value::Str(rt.NewString(ThisStr(rt, a, "normalize"))); }, 0);
}


// ============================================================ TextEncoder / TextDecoder
// Written from the WHATWG Encoding Standard (encoding.spec.whatwg.org): 8.1 TextDecoder, 8.2 TextEncoder, 9.1.1
// the UTF-8 decoder (bytes needed, lower and upper boundary) and 9.1.2 the UTF-8 encoder. Only UTF-8 is supported
// (the label "utf-8" and its aliases); any other label is a RangeError as the standard says for an unknown one.
namespace {

struct TextCodec : Object {
    bool decoder = false;
    bool fatal = false, ignoreBOM = false, bomSeen = false, streaming = false;
    std::uint32_t cp = 0;
    int seen = 0, needed = 0;
    std::uint8_t lower = 0x80, upper = 0xBF;
    TextCodec() : Object(Cls::Opaque) {}
};

TextCodec* ThisCodec(Runtime& rt, const Value& v, bool decoder, const char* what) {
    TextCodec* c = v.IsObj() && v.O()->cls == Cls::Opaque ? dynamic_cast<TextCodec*>(v.O()) : nullptr;
    if (!c || c->decoder != decoder) rt.ThrowType(std::string(what) + " called on the wrong kind of object");
    return c;
}

void AppendCodePoint(std::u16string& out, std::uint32_t cp) {
    if (cp < 0x10000) out += (char16_t)cp;
    else {
        cp -= 0x10000;
        out += (char16_t)(0xD800 + (cp >> 10));
        out += (char16_t)(0xDC00 + (cp & 0x3FF));
    }
}

// The bytes of a BufferSource (an ArrayBuffer or a view); nullptr data for undefined.
void BufferSourceBytes(Runtime& rt, const Value& v, const std::uint8_t*& data, std::size_t& n) {
    data = nullptr;
    n = 0;
    if (v.IsUndef()) return;
    if (v.IsObj()) {
        Object* o = v.O();
        if (o->cls == Cls::ArrayBuffer) {
            BufferObj* b = static_cast<BufferObj*>(o);
            data = b->data.data();
            n = b->data.size();
            return;
        }
        if (o->cls == Cls::TypedArray) {
            TypedArrayObj* t = static_cast<TypedArrayObj*>(o);
            if (t->OutOfBounds()) return;
            data = t->buf->data.data() + t->offset;
            n = (std::size_t)(t->Length() * t->ElemSize());
            return;
        }
        if (o->cls == Cls::DataView) {
            DataViewObj* d = static_cast<DataViewObj*>(o);
            if (d->buf->detached || d->offset + d->length > d->buf->data.size()) return;
            data = d->buf->data.data() + d->offset;
            n = (std::size_t)d->length;
            return;
        }
    }
    rt.ThrowType("TextDecoder.decode needs an ArrayBuffer, a typed array or a DataView");
}

} // namespace

void InstallTextCodingLib(Runtime& rt) {
    // 8.2 TextEncoder
    Object* tep = rt.NewObject();
    NativeFunc* te = DefCtor(rt, "TextEncoder", [](Runtime& rt, CallArgs& a) {
        if (!a.newTarget) rt.ThrowType("TextEncoder must be called with 'new'");
        TextCodec* c = rt.Alloc<TextCodec>();
        Value pr = rt.Get(Value::Obj(a.newTarget), rt.atoms->prototype);
        c->proto = pr.IsObj() ? pr.O() : a.callee->slots[0].O();
        return Value::Obj(c);
    }, 0, tep);
    te->slots = {Value::Obj(tep)};
    DefGetter(rt, tep, "encoding", [](Runtime& rt, CallArgs& a) {
        ThisCodec(rt, a.thisv, false, "TextEncoder.prototype.encoding");
        return Value::Str(rt.Intern("utf-8"));
    });
    DefNative(rt, tep, "encode", [](Runtime& rt, CallArgs& a) {
        ThisCodec(rt, a.thisv, false, "TextEncoder.prototype.encode");
        std::u16string s = a[0].IsUndef() ? std::u16string() : rt.ToU16(a[0]);
        rt.Charge(s.size() / 16 + 1);
        std::string bytes = Utf16To8(s);   // lone surrogates become U+FFFD (the USVString conversion)
        return Value::Obj(NewUint8Array(rt, reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
    }, 0);
    DefNative(rt, tep, "encodeInto", [](Runtime& rt, CallArgs& a) {   // 8.2.3 encodeInto(source, destination)
        ThisCodec(rt, a.thisv, false, "TextEncoder.prototype.encodeInto");
        std::u16string s = rt.ToU16(a[0]);
        if (!a[1].IsObj() || a[1].O()->cls != Cls::TypedArray || static_cast<TypedArrayObj*>(a[1].O())->type != TAType::U8)
            rt.ThrowType("TextEncoder.prototype.encodeInto needs a Uint8Array");
        TypedArrayObj* t = static_cast<TypedArrayObj*>(a[1].O());
        const std::size_t room = t->OutOfBounds() ? 0 : (std::size_t)t->Length();
        std::uint8_t* dst = room ? t->buf->data.data() + t->offset : nullptr;
        std::size_t read = 0, written = 0;
        rt.Charge(s.size() / 16 + 1);
        while (read < s.size()) {
            std::uint32_t cp = s[read];
            std::size_t units = 1;
            if (cp >= 0xD800 && cp <= 0xDBFF && read + 1 < s.size() && s[read + 1] >= 0xDC00 && s[read + 1] <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (s[read + 1] - 0xDC00);
                units = 2;
            } else if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
            std::uint8_t buf[4];
            int n;
            if (cp < 0x80) { buf[0] = (std::uint8_t)cp; n = 1; }
            else if (cp < 0x800) { buf[0] = (std::uint8_t)(0xC0 | (cp >> 6)); buf[1] = (std::uint8_t)(0x80 | (cp & 0x3F)); n = 2; }
            else if (cp < 0x10000) { buf[0] = (std::uint8_t)(0xE0 | (cp >> 12)); buf[1] = (std::uint8_t)(0x80 | ((cp >> 6) & 0x3F)); buf[2] = (std::uint8_t)(0x80 | (cp & 0x3F)); n = 3; }
            else { buf[0] = (std::uint8_t)(0xF0 | (cp >> 18)); buf[1] = (std::uint8_t)(0x80 | ((cp >> 12) & 0x3F)); buf[2] = (std::uint8_t)(0x80 | ((cp >> 6) & 0x3F)); buf[3] = (std::uint8_t)(0x80 | (cp & 0x3F)); n = 4; }
            if (written + (std::size_t)n > room) break;
            for (int i = 0; i < n; ++i) dst[written++] = buf[i];
            read += units;
        }
        Object* r = rt.NewObject();
        rt.DefineData(r, rt.Intern("read"), Value::Num((double)read));
        rt.DefineData(r, rt.Intern("written"), Value::Num((double)written));
        return Value::Obj(r);
    }, 2);
    tep->AddRaw(rt.symToStringTag, Value::Str(rt.Intern("TextEncoder")), kConfigurable);

    // 8.1 TextDecoder
    Object* tdp = rt.NewObject();
    NativeFunc* td = DefCtor(rt, "TextDecoder", [](Runtime& rt, CallArgs& a) {
        if (!a.newTarget) rt.ThrowType("TextDecoder must be called with 'new'");
        std::u16string label = a[0].IsUndef() ? u"utf-8" : rt.ToU16(a[0]);
        std::size_t b = 0, e = label.size();
        while (b < e && IsJsSpace(label[b])) ++b;
        while (e > b && IsJsSpace(label[e - 1])) --e;
        std::u16string l = label.substr(b, e - b);
        for (char16_t& c : l) c = (c >= u'A' && c <= u'Z') ? (char16_t)(c + 32) : c;
        if (l != u"utf-8" && l != u"utf8" && l != u"unicode-1-1-utf-8" && l != u"unicode11utf8" && l != u"unicode20utf8" &&
            l != u"x-unicode20utf8")
            rt.ThrowRange("TextDecoder: only the utf-8 encoding is supported, not '" + Utf16To8(label.substr(0, 40)) + "'");
        TextCodec* c = rt.Alloc<TextCodec>();
        c->decoder = true;
        Value pr = rt.Get(Value::Obj(a.newTarget), rt.atoms->prototype);
        c->proto = pr.IsObj() ? pr.O() : a.callee->slots[0].O();
        if (a[1].IsObj()) {
            c->fatal = rt.ToBoolean(rt.Get(a[1], rt.Intern("fatal")));
            c->ignoreBOM = rt.ToBoolean(rt.Get(a[1], rt.Intern("ignoreBOM")));
        } else if (!a[1].IsNullish()) rt.ThrowType("TextDecoder's options must be an object");
        return Value::Obj(c);
    }, 0, tdp);
    td->slots = {Value::Obj(tdp)};
    DefGetter(rt, tdp, "encoding", [](Runtime& rt, CallArgs& a) {
        ThisCodec(rt, a.thisv, true, "TextDecoder.prototype.encoding");
        return Value::Str(rt.Intern("utf-8"));
    });
    DefGetter(rt, tdp, "fatal", [](Runtime& rt, CallArgs& a) { return Value::Bool(ThisCodec(rt, a.thisv, true, "TextDecoder.prototype.fatal")->fatal); });
    DefGetter(rt, tdp, "ignoreBOM", [](Runtime& rt, CallArgs& a) { return Value::Bool(ThisCodec(rt, a.thisv, true, "TextDecoder.prototype.ignoreBOM")->ignoreBOM); });
    DefNative(rt, tdp, "decode", [](Runtime& rt, CallArgs& a) {   // 8.1.3 decode(input, options)
        TextCodec* c = ThisCodec(rt, a.thisv, true, "TextDecoder.prototype.decode");
        if (!c->streaming) {   // a new stream: the decoder starts again
            c->cp = 0;
            c->seen = c->needed = 0;
            c->lower = 0x80;
            c->upper = 0xBF;
            c->bomSeen = false;
        }
        bool stream = false;
        if (a[1].IsObj()) stream = rt.ToBoolean(rt.Get(a[1], rt.Intern("stream")));
        const std::uint8_t* data;
        std::size_t n;
        BufferSourceBytes(rt, a[0], data, n);
        std::vector<std::uint8_t> bytes(data, data + n);   // a copy: nothing below runs script code, but keep it plain
        rt.Charge(n / 16 + 1);
        std::u16string out;
        auto error = [&]() {
            if (c->fatal) {
                c->streaming = false;
                rt.ThrowType("TextDecoder: the data is not valid UTF-8");
            }
            out += u'�';
        };
        auto emit = [&](std::uint32_t cp) {
            if (!c->bomSeen) {
                c->bomSeen = true;
                if (cp == 0xFEFF && !c->ignoreBOM) return;
            }
            AppendCodePoint(out, cp);
        };
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            const std::uint8_t b = bytes[i];
            if (c->needed == 0) {
                if (b <= 0x7F) emit(b);
                else if (b >= 0xC2 && b <= 0xDF) { c->needed = 1; c->cp = b & 0x1F; }
                else if (b >= 0xE0 && b <= 0xEF) {
                    if (b == 0xE0) c->lower = 0xA0;
                    if (b == 0xED) c->upper = 0x9F;
                    c->needed = 2;
                    c->cp = b & 0xF;
                } else if (b >= 0xF0 && b <= 0xF4) {
                    if (b == 0xF0) c->lower = 0x90;
                    if (b == 0xF4) c->upper = 0x8F;
                    c->needed = 3;
                    c->cp = b & 0x7;
                } else {
                    c->bomSeen = true;
                    error();
                }
                continue;
            }
            if (b < c->lower || b > c->upper) {   // the byte is read again as the start of something new
                c->cp = 0;
                c->needed = c->seen = 0;
                c->lower = 0x80;
                c->upper = 0xBF;
                c->bomSeen = true;
                error();
                --i;
                continue;
            }
            c->lower = 0x80;
            c->upper = 0xBF;
            c->cp = (c->cp << 6) | (b & 0x3F);
            if (++c->seen == c->needed) {
                const std::uint32_t cp = c->cp;
                c->cp = 0;
                c->needed = c->seen = 0;
                emit(cp);
            }
        }
        if (!stream && c->needed != 0) {   // the end of the stream inside a sequence
            c->cp = 0;
            c->needed = c->seen = 0;
            c->lower = 0x80;
            c->upper = 0xBF;
            error();
        }
        c->streaming = stream;
        return Value::Str(rt.NewString(std::move(out)));
    }, 0);
    tdp->AddRaw(rt.symToStringTag, Value::Str(rt.Intern("TextDecoder")), kConfigurable);
}

} // namespace okay::js
