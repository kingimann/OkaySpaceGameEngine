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

} // namespace okay::js
