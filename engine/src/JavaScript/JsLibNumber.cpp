// okay::js library: Number (ECMA-262 21.1) and Math (21.3). Written from those
// sections. toFixed / toPrecision / toExponential round the exact decimal value
// of the double half-up as 21.1.3.3-21.1.3.5 say (picking the larger n on a tie);
// we get the exact digits from the C library's correctly rounded %e with enough
// precision to print every digit, then round them ourselves. toString(radix) for
// radix 2-36 writes the integer part exactly and up to 52 fraction digits.
// Math.random is our own xorshift64* generator, seeded per runtime.
#include "JsInternal.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace okay::js {

namespace {

double ThisNum(Runtime& rt, const Value& v, const char* m) {
    if (v.IsNum()) return v.n;
    if (v.IsObj() && v.O()->cls == Cls::NumberBox) return static_cast<BoxObj*>(v.O())->prim.n;
    rt.ThrowType(std::string("Number.prototype.") + m + " needs a number");
}

// x = digits[0].digits[1..] * 10^exp (x > 0, finite): every significant digit, exactly.
void ExactDigits(double x, std::string& digits, int& exp) {
    char buf[1200];
    std::snprintf(buf, sizeof buf, "%.1100e", x);
    const char* e = std::strchr(buf, 'e');
    digits.clear();
    for (const char* c = buf; c < e; ++c)
        if (*c >= '0' && *c <= '9') digits += *c;
    exp = std::atoi(e + 1);
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
}

// Round to n significant digits, half up; may carry into a new leading digit (exp + 1).
void RoundDigits(std::string& d, int& exp, int n) {
    if ((int)d.size() <= n) {
        d.append((std::size_t)(n - (int)d.size()), '0');
        return;
    }
    bool up = d[(std::size_t)n] >= '5';
    d.resize((std::size_t)n);
    if (!up) return;
    int i = n - 1;
    for (; i >= 0; --i) {
        if (d[(std::size_t)i] == '9') d[(std::size_t)i] = '0';
        else {
            ++d[(std::size_t)i];
            break;
        }
    }
    if (i < 0) {
        d.insert(d.begin(), '1');
        d.pop_back();
        ++exp;
    }
}

std::string ToFixedText(double x, int f) {
    std::string sign = x < 0 ? "-" : "";
    x = std::fabs(x);
    if (x == 0) return f ? "0." + std::string((std::size_t)f, '0') : "0";
    std::string d;
    int e;
    ExactDigits(x, d, e);
    int keep = e + 1 + f;   // significant digits up to the f-th decimal place
    std::string digits;
    int intDigits;
    if (keep < 0) {
        digits = std::string((std::size_t)f, '0');
        intDigits = 0;
    } else if (keep == 0) {
        bool up = d[0] >= '5';
        digits = std::string((std::size_t)f, '0');
        intDigits = 0;
        if (up) {
            if (f == 0) return sign + "1";
            digits[(std::size_t)f - 1] = '1';
        }
        std::string out = "0";
        if (f) out += "." + digits;
        return (up || x != 0) ? sign + out : out;
    } else {
        int e2 = e;
        RoundDigits(d, e2, keep);
        if (e2 != e) d += '0';   // a carry added a digit in front; keep the same number of decimals
        intDigits = e2 + 1;
        digits = d;
    }
    std::string out;
    if (intDigits <= 0) {
        out = "0";
        if (f) out += "." + std::string((std::size_t)(-intDigits), '0') + digits.substr(0, (std::size_t)(f + intDigits));
    } else {
        out = digits.substr(0, (std::size_t)intDigits);
        if (f) out += "." + digits.substr((std::size_t)intDigits, (std::size_t)f);
    }
    return sign + out;
}

std::string RadixText(double x, int radix) {
    const char* dig = "0123456789abcdefghijklmnopqrstuvwxyz";
    bool neg = x < 0;
    x = std::fabs(x);
    double ip = std::floor(x), fp = x - ip;
    std::string is;
    if (ip == 0) is = "0";
    while (ip > 0) {
        double q = std::floor(ip / radix);
        int r = (int)(ip - q * radix);
        if (r < 0) r = 0;
        if (r >= radix) r = radix - 1;
        is += dig[r];
        ip = q;
    }
    std::string out(is.rbegin(), is.rend());
    if (fp > 0) {
        out += '.';
        for (int i = 0; i < 52 && fp > 0; ++i) {
            fp *= radix;
            int d = (int)std::floor(fp);
            out += dig[d];
            fp -= d;
        }
    }
    return neg ? "-" + out : out;
}

std::uint64_t NextRandom(std::uint64_t& s) {
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    return s * 2685821657736338717ull;
}

double MinMax(Runtime& rt, CallArgs& a, bool isMax) {
    double r = isMax ? -INFINITY : INFINITY;
    bool nan = false;
    for (int i = 0; i < a.argc; ++i) {
        double d = rt.ToNumber(a[i]);
        if (std::isnan(d)) { nan = true; continue; }
        if (isMax ? (d > r || (d == 0 && r == 0 && !std::signbit(d))) : (d < r || (d == 0 && r == 0 && std::signbit(d)))) r = d;
    }
    return nan ? NAN : r;
}

} // namespace

void InstallNumberLib(Runtime& rt) {
    BoxObj* np = rt.Alloc<BoxObj>();
    np->cls = Cls::NumberBox;
    np->prim = Value::Num(0);
    np->proto = rt.objectProto;
    rt.numberProto = np;
    NativeFunc* ctor = DefCtor(rt, "Number", [](Runtime& rt, CallArgs& a) {
        double d = a.argc ? rt.ToNumber(a[0]) : 0;
        if (!a.newTarget) return Value::Num(d);
        Object* o = rt.ToObject(Value::Num(d));
        Value pr = rt.Get(Value::Obj(a.newTarget), rt.atoms->prototype);
        if (pr.IsObj()) o->proto = pr.O();
        return Value::Obj(o);
    }, 1, np);
    DefValue(rt, ctor, "MAX_SAFE_INTEGER", Value::Num(9007199254740991.0), 0);
    DefValue(rt, ctor, "MIN_SAFE_INTEGER", Value::Num(-9007199254740991.0), 0);
    DefValue(rt, ctor, "EPSILON", Value::Num(2.220446049250313e-16), 0);
    DefValue(rt, ctor, "MAX_VALUE", Value::Num(1.7976931348623157e308), 0);
    DefValue(rt, ctor, "MIN_VALUE", Value::Num(5e-324), 0);
    DefValue(rt, ctor, "POSITIVE_INFINITY", Value::Num(INFINITY), 0);
    DefValue(rt, ctor, "NEGATIVE_INFINITY", Value::Num(-INFINITY), 0);
    DefValue(rt, ctor, "NaN", Value::Num(NAN), 0);
    DefNative(rt, ctor, "isFinite", [](Runtime&, CallArgs& a) { return Value::Bool(a[0].IsNum() && std::isfinite(a[0].n)); }, 1);
    DefNative(rt, ctor, "isNaN", [](Runtime&, CallArgs& a) { return Value::Bool(a[0].IsNum() && std::isnan(a[0].n)); }, 1);
    DefNative(rt, ctor, "isInteger", [](Runtime&, CallArgs& a) {
        return Value::Bool(a[0].IsNum() && std::isfinite(a[0].n) && std::trunc(a[0].n) == a[0].n);
    }, 1);
    DefNative(rt, ctor, "isSafeInteger", [](Runtime&, CallArgs& a) {
        return Value::Bool(a[0].IsNum() && std::isfinite(a[0].n) && std::trunc(a[0].n) == a[0].n && std::fabs(a[0].n) <= 9007199254740991.0);
    }, 1);

    DefNative(rt, np, "valueOf", [](Runtime& rt, CallArgs& a) { return Value::Num(ThisNum(rt, a.thisv, "valueOf")); }, 0);
    DefNative(rt, np, "toString", [](Runtime& rt, CallArgs& a) {
        double x = ThisNum(rt, a.thisv, "toString");
        int radix = a[0].IsUndef() ? 10 : (int)rt.ToIntegerOrInfinity(a[0]);
        if (radix < 2 || radix > 36) rt.ThrowRange("toString's radix must be between 2 and 36");
        if (radix == 10 || !std::isfinite(x)) return Value::Str(rt.NewString(rt.NumberToU16(x)));
        return Value::Str(rt.NewStringAscii(RadixText(x, radix).c_str()));
    }, 1);
    DefNative(rt, np, "toLocaleString", [](Runtime& rt, CallArgs& a) {
        return Value::Str(rt.NewString(rt.NumberToU16(ThisNum(rt, a.thisv, "toLocaleString"))));
    }, 0);
    DefNative(rt, np, "toFixed", [](Runtime& rt, CallArgs& a) {
        double x = ThisNum(rt, a.thisv, "toFixed");
        double f = rt.ToIntegerOrInfinity(a[0]);
        if (f < 0 || f > 100) rt.ThrowRange("toFixed takes 0 to 100 digits");
        if (!std::isfinite(x) || std::fabs(x) >= 1e21) return Value::Str(rt.NewString(rt.NumberToU16(x)));
        return Value::Str(rt.NewStringAscii(ToFixedText(x, (int)f).c_str()));
    }, 1);
    DefNative(rt, np, "toPrecision", [](Runtime& rt, CallArgs& a) {
        double x = ThisNum(rt, a.thisv, "toPrecision");
        if (a[0].IsUndef() || !std::isfinite(x)) return Value::Str(rt.NewString(rt.NumberToU16(x)));
        double pd = rt.ToIntegerOrInfinity(a[0]);
        if (pd < 1 || pd > 100) rt.ThrowRange("toPrecision takes 1 to 100 digits");
        int p = (int)pd;
        std::string sign = x < 0 ? "-" : "";
        x = std::fabs(x);
        std::string d;
        int e;
        if (x == 0) { d = std::string((std::size_t)p, '0'); e = 0; }
        else {
            ExactDigits(x, d, e);
            RoundDigits(d, e, p);
        }
        std::string out;
        if (e < -6 || e >= p) {
            out = d.substr(0, 1);
            if (p > 1) out += "." + d.substr(1);
            out += std::string("e") + (e < 0 ? "-" : "+") + std::to_string(e < 0 ? -e : e);
        } else if (e == p - 1) out = d;
        else if (e >= 0) out = d.substr(0, (std::size_t)e + 1) + "." + d.substr((std::size_t)e + 1);
        else out = "0." + std::string((std::size_t)(-(e + 1)), '0') + d;
        return Value::Str(rt.NewStringAscii((sign + out).c_str()));
    }, 1);
    DefNative(rt, np, "toExponential", [](Runtime& rt, CallArgs& a) {
        double x = ThisNum(rt, a.thisv, "toExponential");
        double fd = rt.ToIntegerOrInfinity(a[0]);
        if (!std::isfinite(x)) return Value::Str(rt.NewString(rt.NumberToU16(x)));
        if (fd < 0 || fd > 100) rt.ThrowRange("toExponential takes 0 to 100 digits");
        std::string sign = x < 0 ? "-" : "";
        x = std::fabs(x);
        std::string d;
        int e = 0;
        if (x == 0) d = a[0].IsUndef() ? "0" : std::string((std::size_t)fd + 1, '0');
        else if (a[0].IsUndef()) {
            // as many digits as it takes to tell x apart (the digits of Number::toString)
            std::string s = Runtime::NumberToStdString(x);
            ExactDigits(x, d, e);
            std::string shortest;
            for (char c : s) {
                if (c == 'e') break;
                if (c >= '0' && c <= '9') shortest += c;
            }
            std::size_t nz = shortest.find_first_not_of('0');
            shortest = nz == std::string::npos ? "0" : shortest.substr(nz);
            while (shortest.size() > 1 && shortest.back() == '0') shortest.pop_back();
            RoundDigits(d, e, (int)shortest.size());
        } else {
            ExactDigits(x, d, e);
            RoundDigits(d, e, (int)fd + 1);
        }
        std::string out = d.substr(0, 1);
        if (d.size() > 1) out += "." + d.substr(1);
        out += std::string("e") + (e < 0 ? "-" : "+") + std::to_string(e < 0 ? -e : e);
        return Value::Str(rt.NewStringAscii((sign + out).c_str()));
    }, 1);
    // Number.parseFloat / parseInt are the global functions (21.1.2.12-13); set after the globals exist
}

void InstallMathLib(Runtime& rt) {
    Object* m = rt.NewObject();
    DefValue(rt, rt.global, "Math", Value::Obj(m));
    m->AddRaw(rt.symToStringTag, Value::Str(rt.Intern("Math")), kConfigurable);
    DefValue(rt, m, "E", Value::Num(2.718281828459045), 0);
    DefValue(rt, m, "LN10", Value::Num(2.302585092994046), 0);
    DefValue(rt, m, "LN2", Value::Num(0.6931471805599453), 0);
    DefValue(rt, m, "LOG10E", Value::Num(0.4342944819032518), 0);
    DefValue(rt, m, "LOG2E", Value::Num(1.4426950408889634), 0);
    DefValue(rt, m, "PI", Value::Num(3.141592653589793), 0);
    DefValue(rt, m, "SQRT1_2", Value::Num(0.7071067811865476), 0);
    DefValue(rt, m, "SQRT2", Value::Num(1.4142135623730951), 0);
#define M1(NAME, EXPR) DefNative(rt, m, NAME, [](Runtime& rt, CallArgs& a) { double x = rt.ToNumber(a[0]); return Value::Num(EXPR); }, 1)
    M1("abs", std::fabs(x));
    M1("acos", std::acos(x));
    M1("acosh", std::acosh(x));
    M1("asin", std::asin(x));
    M1("asinh", std::asinh(x));
    M1("atan", std::atan(x));
    M1("atanh", std::atanh(x));
    M1("cbrt", std::cbrt(x));
    M1("ceil", std::ceil(x));
    M1("cos", std::cos(x));
    M1("cosh", std::cosh(x));
    M1("exp", std::exp(x));
    M1("expm1", std::expm1(x));
    M1("floor", std::floor(x));
    M1("fround", (double)(float)x);
    M1("log", std::log(x));
    M1("log1p", std::log1p(x));
    M1("log10", std::log10(x));
    M1("log2", std::log2(x));
    M1("sign", std::isnan(x) ? NAN : x > 0 ? 1.0 : x < 0 ? -1.0 : x);
    M1("sin", std::sin(x));
    M1("sinh", std::sinh(x));
    M1("sqrt", std::sqrt(x));
    M1("tan", std::tan(x));
    M1("tanh", std::tanh(x));
    M1("trunc", std::trunc(x));
#undef M1
    DefNative(rt, m, "round", [](Runtime& rt, CallArgs& a) {   // 21.3.2.28: halves go up (toward +Infinity)
        double x = rt.ToNumber(a[0]);
        if (!std::isfinite(x) || x == 0) return Value::Num(x);
        if (x < 0 && x >= -0.5) return Value::Num(-0.0);
        double r = std::floor(x);
        if (x - r >= 0.5) r += 1;
        return Value::Num(r);
    }, 1);
    DefNative(rt, m, "clz32", [](Runtime& rt, CallArgs& a) {
        std::uint32_t v = rt.ToUint32(a[0]);
        int n = 0;
        if (!v) return Value::Num(32);
        while (!(v & 0x80000000u)) { v <<= 1; ++n; }
        return Value::Num(n);
    }, 1);
    DefNative(rt, m, "imul", [](Runtime& rt, CallArgs& a) {
        std::uint32_t x = rt.ToUint32(a[0]), y = rt.ToUint32(a[1]);
        return Value::Num((double)(std::int32_t)(x * y));
    }, 2);
    DefNative(rt, m, "atan2", [](Runtime& rt, CallArgs& a) {
        double y = rt.ToNumber(a[0]), x = rt.ToNumber(a[1]);
        return Value::Num(std::atan2(y, x));
    }, 2);
    DefNative(rt, m, "pow", [](Runtime& rt, CallArgs& a) {
        double b = rt.ToNumber(a[0]), e = rt.ToNumber(a[1]);
        if (std::isnan(e)) return Value::Num(NAN);
        if (e == 0) return Value::Num(1);
        if ((b == 1 || b == -1) && std::isinf(e)) return Value::Num(NAN);
        return Value::Num(std::pow(b, e));
    }, 2);
    DefNative(rt, m, "max", [](Runtime& rt, CallArgs& a) { return Value::Num(MinMax(rt, a, true)); }, 2);
    DefNative(rt, m, "min", [](Runtime& rt, CallArgs& a) { return Value::Num(MinMax(rt, a, false)); }, 2);
    DefNative(rt, m, "hypot", [](Runtime& rt, CallArgs& a) {
        double s = 0;
        bool inf = false, nan = false;
        std::vector<double> v;
        for (int i = 0; i < a.argc; ++i) v.push_back(rt.ToNumber(a[i]));
        double big = 0;
        for (double d : v) {
            if (std::isinf(d)) inf = true;
            if (std::isnan(d)) nan = true;
            big = std::max(big, std::fabs(d));
        }
        if (inf) return Value::Num(INFINITY);
        if (nan) return Value::Num(NAN);
        if (big == 0) return Value::Num(0);
        for (double d : v) s += (d / big) * (d / big);
        return Value::Num(big * std::sqrt(s));
    }, 2);
    std::uint64_t seed = (std::uint64_t)std::chrono::steady_clock::now().time_since_epoch().count() ^ (std::uint64_t)(std::uintptr_t)&rt;
    if (!seed) seed = 88172645463325252ull;
    NativeFunc* rnd = DefNative(rt, m, "random", [](Runtime& rt, CallArgs& a) {
        (void)rt;
        std::uint64_t s = (std::uint64_t)a.callee->slots[0].n;
        std::uint64_t bits = (std::uint64_t)a.callee->slots[1].n;
        (void)bits;
        std::uint64_t st = s;
        std::uint64_t r = NextRandom(st);
        a.callee->slots[0] = Value::Num((double)(st & ((1ull << 52) - 1)) + 1);   // keep the state exactly in a double
        return Value::Num((double)(r >> 11) * (1.0 / 9007199254740992.0));
    }, 0);
    rnd->slots = {Value::Num((double)(seed & ((1ull << 52) - 1)) + 1), Value::Num(0)};

    // Number.parseFloat / parseInt are the global functions
    Value numCtor = rt.global->FindRaw(rt.Intern("Number"))->v;
    (void)numCtor;
}

} // namespace okay::js
