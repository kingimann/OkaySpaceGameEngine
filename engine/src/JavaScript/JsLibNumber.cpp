// okay::js library: Number (ECMA-262 21.1) and Math (21.3). Written from those
// sections. toFixed / toPrecision / toExponential round the exact decimal value
// of the double half-up as 21.1.3.3-21.1.3.5 say (picking the larger n on a tie);
// we get the exact digits from the C library's correctly rounded %e with enough
// precision to print every digit, then round them ourselves. toString(radix) for
// radix 2-36 writes the integer part exactly and up to 52 fraction digits.
// Math.random is our own xorshift64* generator, seeded per runtime. Date (21.4) is at the end: see its notes.
#include "JsInternal.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

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

// ============================================================ Date (ECMA-262 21.4)
// Time values are milliseconds since 1970-01-01T00:00:00Z in a double (21.4.1.1); the day, year and month
// arithmetic is the spec's own (21.4.1.3-21.4.1.13: Day, DayFromYear, YearFromTime, MonthFromTime,
// DateFromTime, MakeTime, MakeDay, MakeDate, TimeClip). The local time zone comes from the platform's C
// runtime (localtime_r / localtime_s): we ask it for the local wall clock of a UTC second and take the
// difference, so there is no time zone database of our own. Date.parse reads the Date Time String Format
// of 21.4.1.32 and, as 21.4.3.2 allows, the forms toString and toUTCString write plus the usual
// "Month D, YYYY [h:mm[:ss] [AM|PM]] [GMT+hhmm]" and "M/D/YYYY" forms.
namespace {

constexpr double kMsPerDay = 86400000.0;
constexpr double kMaxTime = 8.64e15;
const char* const kMonthNames[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char* const kDayNames[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

double DayOf(double t) { return std::floor(t / kMsPerDay); }
double TimeInDay(double t) {
    double r = std::fmod(t, kMsPerDay);
    return r < 0 ? r + kMsPerDay : r;
}
double DayFromYear(double y) {
    return 365.0 * (y - 1970) + std::floor((y - 1969) / 4) - std::floor((y - 1901) / 100) + std::floor((y - 1601) / 400);
}
bool LeapYear(double y) {
    std::int64_t n = (std::int64_t)y;
    return (n % 4 == 0 && n % 100 != 0) || n % 400 == 0;
}
double YearFromTime(double t) {
    double day = DayOf(t);
    double y = std::floor(day / 365.2425) + 1970;   // within one year of the answer
    while (DayFromYear(y) > day) y -= 1;
    while (DayFromYear(y + 1) <= day) y += 1;
    return y;
}
// Days before month m (0-11) in a year.
int DaysBeforeMonth(int m, bool leap) {
    static const int cum[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    return cum[m] + (leap && m >= 2 ? 1 : 0);
}
struct Fields {
    double year, month, date, hour, minute, second, ms, weekday;
};
Fields Split(double t) {
    Fields f;
    f.year = YearFromTime(t);
    bool leap = LeapYear(f.year);
    int inYear = (int)(DayOf(t) - DayFromYear(f.year));
    int m = 11;
    while (m > 0 && DaysBeforeMonth(m, leap) > inYear) --m;
    f.month = m;
    f.date = inYear - DaysBeforeMonth(m, leap) + 1;
    double tid = TimeInDay(t);
    f.hour = std::floor(tid / 3600000.0);
    f.minute = std::fmod(std::floor(tid / 60000.0), 60.0);
    f.second = std::fmod(std::floor(tid / 1000.0), 60.0);
    f.ms = std::fmod(tid, 1000.0);
    double wd = std::fmod(DayOf(t) + 4, 7.0);
    f.weekday = wd < 0 ? wd + 7 : wd;
    return f;
}
double MakeTime(double h, double m, double s, double ms) {
    if (!std::isfinite(h) || !std::isfinite(m) || !std::isfinite(s) || !std::isfinite(ms)) return NAN;
    return std::trunc(h) * 3600000.0 + std::trunc(m) * 60000.0 + std::trunc(s) * 1000.0 + std::trunc(ms);
}
double MakeDay(double year, double month, double date) {
    if (!std::isfinite(year) || !std::isfinite(month) || !std::isfinite(date)) return NAN;
    double y = std::trunc(year), m = std::trunc(month), dt = std::trunc(date);
    double ym = y + std::floor(m / 12);
    if (!std::isfinite(ym) || std::fabs(ym) > 400000) return NAN;   // past every representable time value
    int mn = (int)(m - std::floor(m / 12) * 12);
    return DayFromYear(ym) + DaysBeforeMonth(mn, LeapYear(ym)) + dt - 1;
}
double MakeDate(double day, double time) {
    double tv = day * kMsPerDay + time;
    return std::isfinite(tv) ? tv : NAN;
}
double TimeClip(double t) {
    if (!std::isfinite(t) || std::fabs(t) > kMaxTime) return NAN;
    return std::trunc(t) + 0.0;
}

// The local time zone's offset from UTC (ms) at the UTC time t, from the C runtime.
double LocalOffset(double t) {
    if (!std::isfinite(t)) return 0;
    double s = std::floor(t / 1000);
    // the C runtimes handle 1970..3000 everywhere (Windows' localtime_s stops at 3000); outside that use the edge's offset
    const double hi = sizeof(std::time_t) >= 8 ? 32503593600.0 : 2147483647.0;
    if (s < 86400) s = 86400;
    if (s > hi) s = hi;
    std::time_t tt = (std::time_t)s;
    std::tm lt{};
#ifdef _WIN32
    if (localtime_s(&lt, &tt) != 0) return 0;
#else
    if (!localtime_r(&tt, &lt)) return 0;
#endif
    double localSec = MakeDay(lt.tm_year + 1900.0, lt.tm_mon, lt.tm_mday) * 86400.0 + lt.tm_hour * 3600.0 + lt.tm_min * 60.0 + lt.tm_sec;
    return (localSec - s) * 1000.0;
}
double LocalTime(double t) { return t + LocalOffset(t); }
// UTC(t) of 21.4.1.26: the offset in force at the local wall clock time t (the earlier one in a repeated hour).
double UtcFromLocal(double t) {
    if (!std::isfinite(t)) return NAN;
    double guess = t - LocalOffset(t);
    return t - LocalOffset(guess);
}

double NowMs() {
    using namespace std::chrono;
    return (double)duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

double ThisTime(Runtime& rt, const Value& v, const char* m) {
    if (v.IsObj() && v.O()->cls == Cls::DateBox) return static_cast<BoxObj*>(v.O())->prim.n;
    rt.ThrowType(std::string("Date.prototype.") + m + " needs a Date as 'this'");
}
void SetThisTime(const Value& v, double t) { static_cast<BoxObj*>(v.O())->prim = Value::Num(t); }

std::string Pad(double v, int width) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%0*lld", width, (long long)v);
    return buf;
}
std::string YearText(double y) { return (y < 0 ? "-" : "") + Pad(std::fabs(y), 4); }
std::string OffsetText(double offMs, bool colon) {
    double mins = std::fabs(std::round(offMs / 60000));
    return std::string(offMs < 0 ? "-" : "+") + Pad(std::floor(mins / 60), 2) + (colon ? ":" : "") + Pad(std::fmod(mins, 60), 2);
}
// 21.4.4.41.2 DateString, 21.4.4.41.1 TimeString, 21.4.4.41.3 TimeZoneString (no zone name: it is optional)
std::string DatePart(const Fields& f) {
    return std::string(kDayNames[(int)f.weekday]) + " " + kMonthNames[(int)f.month] + " " + Pad(f.date, 2) + " " + YearText(f.year);
}
std::string TimePart(const Fields& f) { return Pad(f.hour, 2) + ":" + Pad(f.minute, 2) + ":" + Pad(f.second, 2); }
std::string ToDateText(double tv, int which) {   // 0 toString, 1 toDateString, 2 toTimeString
    if (std::isnan(tv)) return "Invalid Date";
    double off = LocalOffset(tv);
    Fields f = Split(tv + off);
    if (which == 1) return DatePart(f);
    std::string tz = " GMT" + OffsetText(off, false);
    if (which == 2) return TimePart(f) + tz;
    return DatePart(f) + " " + TimePart(f) + tz;
}
std::string IsoText(double tv) {
    Fields f = Split(tv);
    std::string y = (f.year >= 0 && f.year <= 9999) ? Pad(f.year, 4) : (f.year < 0 ? "-" : "+") + Pad(std::fabs(f.year), 6);
    return y + "-" + Pad(f.month + 1, 2) + "-" + Pad(f.date, 2) + "T" + TimePart(f) + "." + Pad(f.ms, 3) + "Z";
}
std::string UtcText(double tv) {
    if (std::isnan(tv)) return "Invalid Date";
    Fields f = Split(tv);
    return std::string(kDayNames[(int)f.weekday]) + ", " + Pad(f.date, 2) + " " + kMonthNames[(int)f.month] + " " + YearText(f.year) +
           " " + TimePart(f) + " GMT";
}

// ---- Date.parse
bool Digits(const std::u16string& s, std::size_t& i, int n, double& out) {
    double v = 0;
    for (int k = 0; k < n; ++k, ++i) {
        if (i >= s.size() || s[i] < u'0' || s[i] > u'9') return false;
        v = v * 10 + (s[i] - u'0');
    }
    out = v;
    return true;
}
// 21.4.1.32: [+-YY]YYYY[-MM[-DD]][THH:mm[:ss[.sss]][Z|+HH:mm|-HH:mm]]; NaN when it does not match.
double ParseIso(const std::u16string& s, bool& matched) {
    matched = false;
    std::size_t i = 0;
    double y = 0, mo = 1, d = 1, h = 0, mi = 0, sec = 0, ms = 0;
    if (i < s.size() && (s[i] == u'+' || s[i] == u'-')) {
        bool neg = s[i] == u'-';
        ++i;
        if (!Digits(s, i, 6, y)) return NAN;
        if (neg && y == 0) return NAN;   // -000000 is not allowed
        if (neg) y = -y;
    } else if (!Digits(s, i, 4, y)) return NAN;
    if (i < s.size() && s[i] == u'-') {
        ++i;
        if (!Digits(s, i, 2, mo)) return NAN;
        if (i < s.size() && s[i] == u'-') {
            ++i;
            if (!Digits(s, i, 2, d)) return NAN;
        }
    }
    bool hasTime = false, hasZone = false;
    double off = 0;
    if (i < s.size() && (s[i] == u'T' || s[i] == u't')) {
        ++i;
        hasTime = true;
        if (!Digits(s, i, 2, h) || i >= s.size() || s[i] != u':') return NAN;
        ++i;
        if (!Digits(s, i, 2, mi)) return NAN;
        if (i < s.size() && s[i] == u':') {
            ++i;
            if (!Digits(s, i, 2, sec)) return NAN;
            if (i < s.size() && (s[i] == u'.' || s[i] == u',')) {
                ++i;
                double scale = 100;
                std::size_t start = i;
                while (i < s.size() && s[i] >= u'0' && s[i] <= u'9') {   // more than 3 digits: the rest are dropped
                    ms += (s[i] - u'0') * scale;
                    scale /= 10;
                    ++i;
                }
                if (i == start) return NAN;
                ms = std::floor(ms);
            }
        }
        if (i < s.size() && (s[i] == u'Z' || s[i] == u'z')) {
            ++i;
            hasZone = true;
        } else if (i < s.size() && (s[i] == u'+' || s[i] == u'-')) {
            double sign = s[i] == u'-' ? -1 : 1, oh = 0, om = 0;
            ++i;
            if (!Digits(s, i, 2, oh)) return NAN;
            if (i < s.size() && s[i] == u':') ++i;
            if (!Digits(s, i, 2, om)) return NAN;
            if (oh > 23 || om > 59) return NAN;
            off = sign * (oh * 60 + om) * 60000;
            hasZone = true;
        }
    }
    if (i != s.size()) return NAN;
    matched = true;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 24 || mi > 59 || sec > 59) return NAN;
    if (h == 24 && (mi != 0 || sec != 0 || ms != 0)) return NAN;
    double dayNum = MakeDay(y, mo - 1, d);
    if (std::isnan(dayNum) || Split(dayNum * kMsPerDay).month != mo - 1) return NAN;   // 2021-02-30
    double t = MakeDate(dayNum, MakeTime(h, mi, sec, ms));
    if (hasZone) t -= off;
    else if (hasTime) t = UtcFromLocal(t);   // a date-time with no offset is local time; a date alone is UTC
    return TimeClip(t);
}

bool StartsWithNoCase(const std::u16string& w, const char* p) {
    std::size_t n = std::strlen(p);
    if (w.size() < n) return false;
    for (std::size_t k = 0; k < n; ++k)
        if (JsLower(w[k]) != (char16_t)p[k]) return false;
    return true;
}
bool WordIs(const std::u16string& w, const char* p) { return w.size() == std::strlen(p) && StartsWithNoCase(w, p); }

// The other forms (implementation-defined by 21.4.3.2): words, numbers, h:mm[:ss[.ms]], M/D/Y, Y-M-D, zones.
double ParseLoose(const std::u16string& s) {
    double year = NAN, month = NAN, day = NAN, h = 0, mi = 0, sec = 0, ms = 0;
    int ampm = 0;                     // 1 am, 2 pm
    bool hasZone = false, haveTime = false;
    double off = 0;
    std::vector<double> nums;         // plain numbers in order
    std::size_t i = 0;
    const std::size_t n = s.size();
    auto isDigit = [&](std::size_t k) { return k < n && s[k] >= u'0' && s[k] <= u'9'; };
    auto readNum = [&](double& v, int& count) {
        v = 0;
        count = 0;
        while (isDigit(i)) {
            if (count < 12) v = v * 10 + (s[i] - u'0');
            ++count;
            ++i;
        }
    };
    while (i < n) {
        char16_t c = s[i];
        if (IsJsSpace(c) || c == u',') { ++i; continue; }
        if (c == u'(') {   // a comment such as "(Central European Standard Time)"
            int depth = 0;
            while (i < n) {
                if (s[i] == u'(') ++depth;
                else if (s[i] == u')' && --depth == 0) { ++i; break; }
                ++i;
            }
            continue;
        }
        if ((c == u'+' || c == u'-') && isDigit(i + 1) && (haveTime || hasZone)) {   // +hhmm / -hh:mm after a time or GMT
            double sign = c == u'-' ? -1 : 1, v;
            int cnt;
            ++i;
            readNum(v, cnt);
            double oh, om = 0;
            if (cnt <= 2) {
                oh = v;
                if (i < n && s[i] == u':') {
                    ++i;
                    readNum(om, cnt);
                }
            } else {
                oh = std::floor(v / 100);
                om = std::fmod(v, 100);
            }
            if (oh > 23 || om > 59) return NAN;
            off = sign * (oh * 60 + om) * 60000;
            hasZone = true;
            continue;
        }
        if (isDigit(i)) {
            double v;
            int cnt;
            readNum(v, cnt);
            if (cnt > 9) return NAN;
            if (i < n && s[i] == u':') {   // a time
                if (haveTime) return NAN;
                haveTime = true;
                h = v;
                ++i;
                readNum(mi, cnt);
                if (!cnt) return NAN;
                if (i < n && s[i] == u':') {
                    ++i;
                    readNum(sec, cnt);
                    if (!cnt) return NAN;
                    if (i < n && s[i] == u'.') {
                        ++i;
                        double scale = 100;
                        while (isDigit(i)) { ms += (s[i] - u'0') * scale; scale /= 10; ++i; }
                        ms = std::floor(ms);
                    }
                }
                continue;
            }
            if (i < n && (s[i] == u'/' || (s[i] == u'-' && isDigit(i + 1)))) {   // M/D/Y or Y/M/D or Y-M-D
                char16_t sep = s[i];
                double b = 0, c3 = NAN;
                ++i;
                readNum(b, cnt);
                if (i < n && s[i] == sep && isDigit(i + 1)) {
                    ++i;
                    readNum(c3, cnt);
                }
                if (v > 31 || cnt == 0) {   // Y/M/D
                    year = v;
                    month = b - 1;
                    day = std::isnan(c3) ? 1 : c3;
                } else {                    // M/D/Y
                    month = v - 1;
                    day = b;
                    year = c3;
                }
                continue;
            }
            nums.push_back(v);
            continue;
        }
        if (JsLower(c) >= u'a' && JsLower(c) <= u'z') {
            std::size_t st = i;
            while (i < n && JsLower(s[i]) >= u'a' && JsLower(s[i]) <= u'z') ++i;
            if (i < n && s[i] == u'.') ++i;   // "Sept." / "a.m"
            std::u16string w = s.substr(st, i - st);
            if (!w.empty() && w.back() == u'.') w.pop_back();
            bool known = false;
            static const char* const months[12] = {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
            static const char* const days[7] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};
            for (int m = 0; m < 12 && !known && w.size() >= 3; ++m)
                if (StartsWithNoCase(w, months[m])) { month = m; known = true; }
            for (int d = 0; d < 7 && !known && w.size() >= 3; ++d)
                if (StartsWithNoCase(w, days[d])) known = true;   // the weekday is ignored
            if (!known) {
                if (WordIs(w, "am") || WordIs(w, "a")) { ampm = 1; known = true; }
                else if (WordIs(w, "pm") || WordIs(w, "p")) { ampm = 2; known = true; }
                else if (WordIs(w, "gmt") || WordIs(w, "utc") || WordIs(w, "ut") || WordIs(w, "z")) { hasZone = true; known = true; }
                else if (WordIs(w, "t") && haveTime == false) known = true;
            }
            if (!known) return NAN;
            continue;
        }
        return NAN;
    }
    // plain numbers: the day and the year (around a month name), or the year of "M/D" written without one
    for (double v : nums) {
        if (std::isnan(day) && v >= 1 && v <= 31 && !std::isnan(month)) day = v;
        else if (std::isnan(year)) year = v;
        else return NAN;
    }
    if (std::isnan(year) || std::isnan(month)) return NAN;
    if (std::isnan(day)) day = 1;
    if (year < 50) year += 2000;
    else if (year < 100) year += 1900;
    if (month < 0 || month > 11 || day < 1 || day > 31 || h > 24 || mi > 59 || sec > 59) return NAN;
    if (ampm) {
        if (h < 1 || h > 12) return NAN;
        if (h == 12) h = 0;
        if (ampm == 2) h += 12;
    }
    double t = MakeDate(MakeDay(year, month, day), MakeTime(h, mi, sec, ms));
    t = hasZone ? t - off : UtcFromLocal(t);
    return TimeClip(t);
}

double ParseDate(Runtime& rt, const std::u16string& raw) {
    rt.Charge(raw.size() / 8 + 1);
    if (raw.size() > 512) return NAN;   // no date text is this long
    std::size_t b = 0, e = raw.size();
    while (b < e && IsJsSpace(raw[b])) ++b;
    while (e > b && IsJsSpace(raw[e - 1])) --e;
    std::u16string s = raw.substr(b, e - b);
    bool matched = false;
    double t = ParseIso(s, matched);
    if (matched) return t;
    return ParseLoose(s);
}

// The date and time fields: 0 year, 1 month, 2 date, 3 hours, 4 minutes, 5 seconds, 6 ms, 7 weekday.
double FieldOf(const Fields& f, int k) {
    switch (k) {
        case 0: return f.year;
        case 1: return f.month;
        case 2: return f.date;
        case 3: return f.hour;
        case 4: return f.minute;
        case 5: return f.second;
        case 6: return f.ms;
        default: return f.weekday;
    }
}

// new Date(y, m[, d[, h[, min[, s[, ms]]]]]) and Date.UTC: the time value of the fields (before UTC()).
double FieldsTime(Runtime& rt, CallArgs& a) {
    double v[7] = {NAN, 0, 1, 0, 0, 0, 0};
    for (int k = 0; k < 7 && k < a.argc; ++k) v[k] = rt.ToNumber(a[k]);
    if (!std::isnan(v[0])) {
        double yi = std::trunc(v[0]);
        if (yi >= 0 && yi <= 99) v[0] = 1900 + yi;
    }
    return MakeDate(MakeDay(v[0], v[1], v[2]), MakeTime(v[3], v[4], v[5], v[6]));
}

Value NewDateObject(Runtime& rt, Object* newTarget, double tv) {
    BoxObj* o = rt.Alloc<BoxObj>();
    o->cls = Cls::DateBox;
    o->prim = Value::Num(tv);
    Value pr = rt.Get(Value::Obj(newTarget), rt.atoms->prototype);
    o->proto = pr.IsObj() ? pr.O() : rt.dateProto;
    return Value::Obj(o);
}

Value DateCtor(Runtime& rt, CallArgs& a) {
    if (!a.newTarget) return Value::Str(rt.NewStringAscii(ToDateText(NowMs(), 0).c_str()));   // Date() is a string
    double tv;
    if (a.argc == 0) tv = NowMs();
    else if (a.argc == 1) {
        if (a[0].IsObj() && a[0].O()->cls == Cls::DateBox) tv = static_cast<BoxObj*>(a[0].O())->prim.n;
        else {
            Value p = rt.ToPrimitive(a[0], 0);
            tv = p.IsStr() ? ParseDate(rt, p.S()->s) : rt.ToNumber(p);
        }
        tv = TimeClip(tv);
    } else tv = TimeClip(UtcFromLocal(FieldsTime(rt, a)));
    return NewDateObject(rt, a.newTarget, tv);
}

// getters: slots {field, utc}
Value DateGet(Runtime& rt, CallArgs& a) {
    double tv = ThisTime(rt, a.thisv, "get");
    if (std::isnan(tv)) return Value::Num(NAN);
    int field = (int)a.callee->slots[0].n;
    bool utc = a.callee->slots[1].b;
    Fields f = Split(utc ? tv : LocalTime(tv));
    double r = FieldOf(f, field);
    if (field == 8) r = f.year - 1900;   // getYear (B.2.3.1)
    return Value::Num(r);
}

// setters (21.4.4.20-28): slots {first field, how many arguments it takes, utc}
Value DateSet(Runtime& rt, CallArgs& a) {
    double tv = ThisTime(rt, a.thisv, "set");
    int first = (int)a.callee->slots[0].n, count = (int)a.callee->slots[1].n;
    bool utc = a.callee->slots[2].b;
    double args[4];
    int given = a.argc < 1 ? 1 : (a.argc < count ? a.argc : count);
    for (int k = 0; k < given; ++k) args[k] = rt.ToNumber(a[k]);   // converted before the NaN check, as the spec orders it
    double t;
    if (first == 0) t = std::isnan(tv) ? 0 : (utc ? tv : LocalTime(tv));   // setFullYear starts from +0 on an invalid date
    else {
        if (std::isnan(tv)) return Value::Num(NAN);
        t = utc ? tv : LocalTime(tv);
    }
    Fields f = Split(t);
    double v[7] = {f.year, f.month, f.date, f.hour, f.minute, f.second, f.ms};
    for (int k = 0; k < given; ++k) v[first + k] = args[k];
    double nt = MakeDate(MakeDay(v[0], v[1], v[2]), MakeTime(v[3], v[4], v[5], v[6]));
    double u = TimeClip(utc ? nt : UtcFromLocal(nt));
    SetThisTime(a.thisv, u);
    return Value::Num(u);
}

// Plain en-US forms for toLocale*String with no options: "1/2/2020, 3:04:05 PM".
std::string LocaleDate(const Fields& f) { return Pad(f.month + 1, 1) + "/" + Pad(f.date, 1) + "/" + Pad(f.year, 1); }
std::string LocaleTime(const Fields& f) {
    int h12 = (int)f.hour % 12;
    if (h12 == 0) h12 = 12;
    return std::to_string(h12) + ":" + Pad(f.minute, 2) + ":" + Pad(f.second, 2) + (f.hour < 12 ? " AM" : " PM");
}

} // namespace

void InstallDateLib(Runtime& rt) {
    Object* dp = rt.NewObject(rt.objectProto);   // Date.prototype is an ordinary object (21.4.4), not a Date
    rt.dateProto = dp;
    NativeFunc* ctor = DefCtor(rt, "Date", DateCtor, 7, dp);
    DefNative(rt, ctor, "now", [](Runtime&, CallArgs&) { return Value::Num(NowMs()); }, 0);
    DefNative(rt, ctor, "parse", [](Runtime& rt, CallArgs& a) { return Value::Num(ParseDate(rt, rt.ToU16(a[0]))); }, 1);
    DefNative(rt, ctor, "UTC", [](Runtime& rt, CallArgs& a) { return Value::Num(TimeClip(FieldsTime(rt, a))); }, 7);

    static const char* const getters[][2] = {
        {"getFullYear", "getUTCFullYear"}, {"getMonth", "getUTCMonth"}, {"getDate", "getUTCDate"},
        {"getHours", "getUTCHours"}, {"getMinutes", "getUTCMinutes"}, {"getSeconds", "getUTCSeconds"},
        {"getMilliseconds", "getUTCMilliseconds"}, {"getDay", "getUTCDay"},
    };
    for (int k = 0; k < 8; ++k)
        for (int u = 0; u < 2; ++u) DefNative(rt, dp, getters[k][u], DateGet, 0)->slots = {Value::Num(k), Value::Bool(u == 1)};
    DefNative(rt, dp, "getYear", DateGet, 0)->slots = {Value::Num(8), Value::Bool(false)};
    struct SetterDef { const char* local; const char* utc; int first, count; };
    static const SetterDef setters[] = {
        {"setFullYear", "setUTCFullYear", 0, 3}, {"setMonth", "setUTCMonth", 1, 2}, {"setDate", "setUTCDate", 2, 1},
        {"setHours", "setUTCHours", 3, 4}, {"setMinutes", "setUTCMinutes", 4, 3}, {"setSeconds", "setUTCSeconds", 5, 2},
        {"setMilliseconds", "setUTCMilliseconds", 6, 1},
    };
    for (const SetterDef& s : setters)
        for (int u = 0; u < 2; ++u)
            DefNative(rt, dp, u ? s.utc : s.local, DateSet, s.count)->slots = {Value::Num(s.first), Value::Num(s.count), Value::Bool(u == 1)};
    DefNative(rt, dp, "setYear", [](Runtime& rt, CallArgs& a) {   // B.2.3.2
        double tv = ThisTime(rt, a.thisv, "setYear");
        double t = std::isnan(tv) ? 0 : LocalTime(tv);
        double y = rt.ToNumber(a[0]);
        if (std::isnan(y)) { SetThisTime(a.thisv, NAN); return Value::Num(NAN); }
        double yi = std::trunc(y);
        if (yi >= 0 && yi <= 99) yi += 1900;
        Fields f = Split(t);
        double u = TimeClip(UtcFromLocal(MakeDate(MakeDay(yi, f.month, f.date), TimeInDay(t))));
        SetThisTime(a.thisv, u);
        return Value::Num(u);
    }, 1);
    DefNative(rt, dp, "getTime", [](Runtime& rt, CallArgs& a) { return Value::Num(ThisTime(rt, a.thisv, "getTime")); }, 0);
    DefNative(rt, dp, "valueOf", [](Runtime& rt, CallArgs& a) { return Value::Num(ThisTime(rt, a.thisv, "valueOf")); }, 0);
    DefNative(rt, dp, "setTime", [](Runtime& rt, CallArgs& a) {
        ThisTime(rt, a.thisv, "setTime");
        double t = TimeClip(rt.ToNumber(a[0]));
        SetThisTime(a.thisv, t);
        return Value::Num(t);
    }, 1);
    DefNative(rt, dp, "getTimezoneOffset", [](Runtime& rt, CallArgs& a) {
        double tv = ThisTime(rt, a.thisv, "getTimezoneOffset");
        if (std::isnan(tv)) return Value::Num(NAN);
        return Value::Num((tv - LocalTime(tv)) / 60000.0);
    }, 0);
    DefNative(rt, dp, "toString", [](Runtime& rt, CallArgs& a) {
        return Value::Str(rt.NewStringAscii(ToDateText(ThisTime(rt, a.thisv, "toString"), 0).c_str()));
    }, 0);
    DefNative(rt, dp, "toDateString", [](Runtime& rt, CallArgs& a) {
        return Value::Str(rt.NewStringAscii(ToDateText(ThisTime(rt, a.thisv, "toDateString"), 1).c_str()));
    }, 0);
    DefNative(rt, dp, "toTimeString", [](Runtime& rt, CallArgs& a) {
        return Value::Str(rt.NewStringAscii(ToDateText(ThisTime(rt, a.thisv, "toTimeString"), 2).c_str()));
    }, 0);
    NativeFunc* utcString = DefNative(rt, dp, "toUTCString", [](Runtime& rt, CallArgs& a) {
        return Value::Str(rt.NewStringAscii(UtcText(ThisTime(rt, a.thisv, "toUTCString")).c_str()));
    }, 0);
    DefValue(rt, dp, "toGMTString", Value::Obj(utcString));   // B.2.3.3: the same function object
    DefNative(rt, dp, "toISOString", [](Runtime& rt, CallArgs& a) {
        double tv = ThisTime(rt, a.thisv, "toISOString");
        if (!std::isfinite(tv)) rt.ThrowRange("toISOString: invalid date");
        return Value::Str(rt.NewStringAscii(IsoText(tv).c_str()));
    }, 0);
    DefNative(rt, dp, "toJSON", [](Runtime& rt, CallArgs& a) {   // 21.4.4.37
        Object* o = rt.ToObject(a.thisv);
        Value tv = rt.ToPrimitive(Value::Obj(o), 1);
        if (tv.IsNum() && !std::isfinite(tv.n)) return Value::Null();
        Value f = rt.Get(Value::Obj(o), rt.Intern("toISOString"));
        if (!rt.IsCallable(f)) rt.ThrowType("toJSON: toISOString is not a function");
        return rt.Call(f, Value::Obj(o), nullptr, 0);
    }, 1);
    // toLocale*String: Intl.DateTimeFormat's en-US forms when it is there (InstallIntlLib replaces these)
    DefNative(rt, dp, "toLocaleString", [](Runtime& rt, CallArgs& a) {
        double tv = ThisTime(rt, a.thisv, "toLocaleString");
        if (std::isnan(tv)) return Value::Str(rt.NewStringAscii("Invalid Date"));
        Fields f = Split(LocalTime(tv));
        return Value::Str(rt.NewStringAscii((LocaleDate(f) + ", " + LocaleTime(f)).c_str()));
    }, 0);
    DefNative(rt, dp, "toLocaleDateString", [](Runtime& rt, CallArgs& a) {
        double tv = ThisTime(rt, a.thisv, "toLocaleDateString");
        if (std::isnan(tv)) return Value::Str(rt.NewStringAscii("Invalid Date"));
        return Value::Str(rt.NewStringAscii(LocaleDate(Split(LocalTime(tv))).c_str()));
    }, 0);
    DefNative(rt, dp, "toLocaleTimeString", [](Runtime& rt, CallArgs& a) {
        double tv = ThisTime(rt, a.thisv, "toLocaleTimeString");
        if (std::isnan(tv)) return Value::Str(rt.NewStringAscii("Invalid Date"));
        return Value::Str(rt.NewStringAscii(LocaleTime(Split(LocalTime(tv))).c_str()));
    }, 0);
    // 21.4.4.45 Date.prototype[@@toPrimitive]: "default" means "string" for dates
    NativeFunc* toPrim = rt.NewNative([](Runtime& rt, CallArgs& a) {
        if (!a.thisv.IsObj()) rt.ThrowType("Date.prototype[Symbol.toPrimitive] needs an object");
        std::u16string hint = a[0].IsStr() ? a[0].S()->s : u"";
        bool str;
        if (hint == u"string" || hint == u"default") str = true;
        else if (hint == u"number") str = false;
        else rt.ThrowType("Date.prototype[Symbol.toPrimitive]: bad hint");
        GcObj* order[2] = {rt.atoms->toString, rt.atoms->valueOf};
        if (!str) std::swap(order[0], order[1]);
        for (GcObj* k : order) {   // 7.1.1.1 OrdinaryToPrimitive
            Value f = rt.Get(a.thisv, k);
            if (rt.IsCallable(f)) {
                Value r = rt.Call(f, a.thisv, nullptr, 0);
                if (!r.IsObj()) return r;
            }
        }
        rt.ThrowType("this object can not be turned into a primitive value");
    }, "[Symbol.toPrimitive]", 1);
    dp->AddRaw(rt.symToPrimitive, Value::Obj(toPrim), kConfigurable);
}

// For console.log: the ISO text of a Date, or "Invalid Date".
std::string DateDisplay(Object* o) {
    double tv = static_cast<BoxObj*>(o)->prim.n;
    return std::isnan(tv) ? "Invalid Date" : IsoText(tv);
}

} // namespace okay::js
